import { readdirSync, readFileSync, statSync } from 'node:fs'
import { join, relative } from 'node:path'

interface Finding {
  path: string
  line: number
  rule: string
  text: string
}

const common = join(import.meta.dirname, '..')
const m5stack = join(common, '..')
const skippedDirectories = new Set(['build-host', 'node_modules'])
const skippedNames = new Set(['.DS_Store'])
const notBoards = new Set(['common', 'patches'])

const boards = readdirSync(m5stack).filter((name) => !notBoards.has(name) && !name.startsWith('.') && statSync(join(m5stack, name)).isDirectory())

function filesUnder(directory: string): string[] {
  const found: string[] = []
  for (const name of readdirSync(directory)) {
    if (skippedNames.has(name)) continue
    const path = join(directory, name)
    if (statSync(path).isDirectory()) {
      if (!skippedDirectories.has(name)) found.push(...filesUnder(path))
      continue
    }
    found.push(path)
  }
  return found
}

const m5Include = /#\s*include\s*[<"](M5Unified|M5GFX|M5Core2|M5Stack|lgfx\/)/
const espInclude = /#\s*include\s*[<"](esp_|freertos\/|nvs|driver\/|sdkconfig|esp_matter|app\/|platform\/|lib\/|M5)/
const lookback = /\bUSAGE_LOOKBACK_DAYS\b/
const boardWords = boards.map((board) => ({ board, pattern: new RegExp(`\\b${board}\\b`) }))
const coreDirectory = join(common, 'components', 'flipped_core')

const findings: Finding[] = []
const files = filesUnder(common).filter((path) => path !== import.meta.filename)
for (const path of files) {
  const name = relative(common, path)
  const lines = readFileSync(path, 'utf8').split('\n')
  const inCore = path.startsWith(coreDirectory)
  lines.forEach((text, index) => {
    const push = (rule: string): void => {
      findings.push({ path: `common/${name}`, line: index + 1, rule, text: text.trim() })
    }
    if (m5Include.test(text)) push('includes an M5Unified or M5GFX header')
    if (text.includes('MALLOC_CAP_SPIRAM')) push('contains MALLOC_CAP_SPIRAM')
    if (lookback.test(text)) push('names USAGE_LOOKBACK_DAYS other than as CONFIG_FLIPPED_USAGE_LOOKBACK_DAYS')
    for (const { board, pattern } of boardWords) {
      if (pattern.test(text)) push(`names the board directory ${board}`)
    }
    if (inCore && espInclude.test(text)) push('flipped_core includes an ESP-IDF, FreeRTOS, Matter or M5 header')
  })
}

for (const finding of findings) console.log(`${finding.path}:${finding.line}: ${finding.rule}: ${finding.text}`)
if (findings.length > 0) {
  console.log(`check-common: ${findings.length} finding(s) in ${files.length} files under common/ (boards: ${boards.join(', ')})`)
  process.exit(1)
}
console.log(`check-common: ${files.length} files under common/ ok (boards: ${boards.join(', ')})`)
