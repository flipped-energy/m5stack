import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import vm from 'node:vm'

interface Vector {
  ipv4: string
  secret: string
  url: string
  token: string
  body: string
}

interface Stub {
  value: string
  textContent: string
  disabled: boolean
  onclick: unknown
}

interface Sent {
  url: string
  method: string
  contentType: string
  body: string
}

interface Reply {
  ok: boolean
  text: string
}

interface Page {
  input: Stub
  button: Stub
  message: Stub
  sent: Sent[]
  replaced: unknown[][]
  click: () => Promise<void> | undefined
}

const here = import.meta.dirname
const vector = JSON.parse(readFileSync(join(here, 'token_vector.json'), 'utf8')) as Vector
const html = readFileSync(join(here, '..', '..', 'components', 'flipped_provision', 'web', 'token.html'), 'utf8')
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((match) => match[1])

function stub(): Stub {
  return { value: '', textContent: '', disabled: false, onclick: null }
}

function load(hash: string, answer: (sent: Sent) => Promise<Reply>): Page {
  const input = stub()
  const button = stub()
  const message = stub()
  const elements: Record<string, Stub> = { t: input, s: button, m: message }
  const sent: Sent[] = []
  const replaced: unknown[][] = []
  const fetch = async (url: string, init: { method: string; headers: Record<string, string>; body: string }) => {
    const request: Sent = { url, method: init.method, contentType: init.headers['Content-Type'], body: init.body }
    sent.push(request)
    const reply = await answer(request)
    return { ok: reply.ok, text: async () => reply.text }
  }
  const context = vm.createContext({
    location: { hash, pathname: '/t' },
    atob,
    TextEncoder,
    fetch,
    document: { getElementById: (id: string) => elements[id] },
    history: { replaceState: (...args: unknown[]) => replaced.push(args) },
  })
  vm.runInContext(scripts[0], context)
  const click = (): Promise<void> | undefined => {
    assert.equal(typeof button.onclick, 'function')
    return (button.onclick as () => Promise<void> | undefined)()
  }
  return { input, button, message, sent, replaced, click }
}

const fragment = vector.url.slice(vector.url.indexOf('#'))
const saved = async (): Promise<Reply> => ({ ok: true, text: 'Token saved. Look at the device.' })

test('the page is one inline script with no external resource', () => {
  assert.equal(scripts.length, 1)
  assert.doesNotMatch(html, /\b(src|href)\s*=|@import|url\(/)
})

test('the page names the portal labels', () => {
  for (const label of ['APIs and MCPs', 'Expires in', 'Read only', 'Create token', 'Send to device']) {
    assert.ok(html.includes(label), label)
  }
})

test('the request body for the fixed vector is the body the firmware decrypts', async () => {
  const page = load(fragment, saved)
  page.input.value = ` ${vector.token}\n`
  await page.click()
  assert.deepEqual(page.sent, [{ url: '/t', method: 'POST', contentType: 'application/x-www-form-urlencoded', body: vector.body }])
  assert.equal(page.message.textContent, 'Token saved. Look at the device.')
  assert.equal(page.input.value, '')
  assert.deepEqual(page.replaced, [[null, '', '/t']])
  assert.equal(page.button.disabled, true)
})

test('a refusal shows the device text as received and keeps the fragment', async () => {
  const text = 'token contains byte 0x20 at position 7, outside 0x21..0x7E'
  const page = load(fragment, async () => ({ ok: false, text }))
  page.input.value = vector.token
  await page.click()
  assert.equal(page.sent.length, 1)
  assert.equal(page.message.textContent, text)
  assert.equal(page.input.value, vector.token)
  assert.deepEqual(page.replaced, [])
  assert.equal(page.button.disabled, false)
})

test('a network failure shows the browser error text', async () => {
  const page = load(fragment, async () => {
    throw new TypeError('Failed to fetch')
  })
  page.input.value = vector.token
  await page.click()
  assert.equal(page.message.textContent, 'TypeError: Failed to fetch')
  assert.equal(page.button.disabled, false)
})

test('a fragment that does not decode to 56 bytes asks for a new scan', () => {
  for (const hash of ['', '#', fragment.slice(0, -1), `${fragment}A`, `${fragment.slice(0, -1)}!`]) {
    const page = load(hash, saved)
    assert.equal(page.message.textContent, 'Scan the code on the device screen again.', hash)
    assert.equal(page.button.disabled, true)
    assert.equal(page.input.disabled, true)
  }
})

test('an input without fdk_ or longer than 48 characters is refused before sending', async () => {
  const page = load(fragment, saved)
  page.input.value = 'vectorTokenForProvisionTests'
  await page.click()
  assert.equal(page.message.textContent, 'The token must start with fdk_.')
  page.input.value = `fdk_${'a'.repeat(45)}`
  await page.click()
  assert.equal(page.message.textContent, 'The token is longer than 48 characters.')
  assert.deepEqual(page.sent, [])
  page.input.value = `fdk_${'a'.repeat(44)}`
  await page.click()
  assert.equal(page.sent.length, 1)
})
