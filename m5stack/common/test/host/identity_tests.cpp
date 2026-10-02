#include <array>
#include <cstdint>
#include <string>
#include <utility>

#include "flipped/core/identity.h"
#include "suite.h"

using namespace flipped::core;

namespace {

std::string hex(const std::array<uint8_t, 32> &digest)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const uint8_t byte : digest) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

}

bool runIdentityTests()
{
    Suite suite("identity_tests");

    suite.run("sha256 known digests", []() -> std::string {
        const std::array<std::pair<std::string, const char *>, 8> cases{{
            {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
            {std::string(55, 'a'), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
            {std::string(63, 'a'), "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
            {std::string(64, 'a'), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
            {std::string(65, 'a'), "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
            {std::string(1000, 'a'), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
        }};
        for (const auto &[input, expected] : cases) {
            const std::string actual = hex(sha256(input));
            if (actual != expected) {
                return std::to_string(input.size()) + "-byte input gives " + actual + ", expected " + expected;
            }
        }
        return "";
    });

    suite.run("instanceKey is the account number, with the NMI when configured", []() -> std::string {
        if (instanceKey("36200000000001", std::nullopt) != "36200000000001") {
            return "without NMI: " + instanceKey("36200000000001", std::nullopt);
        }
        if (instanceKey("36200000000001", std::string("4102000000")) != "36200000000001:4102000000") {
            return "with NMI: " + instanceKey("36200000000001", std::string("4102000000"));
        }
        if (instanceKey("36200000000001", std::string()) != "36200000000001") {
            return "with an empty NMI: " + instanceKey("36200000000001", std::string());
        }
        return "";
    });

    suite.run("<h> equals the first 16 hex characters of SHA-256 of instanceKey", []() -> std::string {
        const std::array<std::pair<const char *, const char *>, 3> cases{{
            {"36200000000001", "b016d710ad0e9769"},
            {"36200000000001:4102000000", "16561ad652a6616f"},
            {"36200000000001:4102000001", "1dbd37079a0ec860"},
        }};
        for (const auto &[key, expected] : cases) {
            if (instanceHash(key) != expected) {
                return std::string(key) + " gives " + instanceHash(key) + ", expected " + expected;
            }
        }
        return "";
    });

    suite.run("a different NMI gives a different <h>", []() -> std::string {
        const std::string first = instanceHash(instanceKey("36200000000001", std::string("4102000000")));
        const std::string second = instanceHash(instanceKey("36200000000001", std::string("4102000001")));
        const std::string none = instanceHash(instanceKey("36200000000001", std::nullopt));
        if (first == second || first == none || second == none) {
            return first + " " + second + " " + none;
        }
        return "";
    });

    suite.run("every UniqueID is at most 32 characters", []() -> std::string {
        const std::string h = instanceHash("36200000000001:4102000000");
        for (const SwitchIdentity &identity : SWITCH_IDENTITIES) {
            const std::string id = uniqueId(h, identity.suffix);
            if (id != "FE-" + h + "-" + identity.suffix || id.size() != 22 || id.size() > UNIQUE_ID_MAX_CHARS) {
                return std::string(identity.key) + " gives " + id;
            }
        }
        return "";
    });

    suite.run("stable ids follow flipped:<instanceKey>:<key>", []() -> std::string {
        const std::string id = stableId("36200000000001:4102000000", SWITCH_IDENTITIES[0].key);
        if (id != "flipped:36200000000001:4102000000:peak_rate") {
            return id;
        }
        return "";
    });

    return suite.finish();
}
