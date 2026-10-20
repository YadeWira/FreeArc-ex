#include "fa/crypto.h"

#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

extern "C" {
// From the legacy library (C_Encryption.cpp); PKCS#5 PBKDF2 with HMAC-SHA512.
void Pbkdf2Hmac(const unsigned char* pwd, int pwdSize, const unsigned char* salt, int saltSize,
                int numIterations, unsigned char* key, int keySize);
}

namespace fa {
namespace {

std::string g_password;

std::string to_hex(const std::vector<unsigned char>& bytes)
{
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

bool from_hex(const std::string& text, std::vector<unsigned char>& out)
{
    if (text.size() % 2 != 0) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

// Splits an encryption stage ("aes-256/ctr:n1000:r0:i...:s...") into the head and
// its parameters.
struct Stage {
    std::string head;  // "aes-256/ctr"
    std::vector<std::string> params;

    explicit Stage(const std::string& text)
    {
        const std::size_t colon = text.find(':');
        head = text.substr(0, colon);
        std::size_t start = (colon == std::string::npos) ? text.size() : colon + 1;
        while (start < text.size()) {
            const std::size_t end = text.find(':', start);
            params.push_back(text.substr(start, end == std::string::npos ? std::string::npos
                                                                        : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    std::string find(char key) const
    {
        for (const auto& p : params) {
            if (!p.empty() && p[0] == key) return p.substr(1);
        }
        return {};
    }

    std::string str() const
    {
        std::string out = head;
        for (const auto& p : params) out += ":" + p;
        return out;
    }

    void set(const std::string& param)
    {
        params.push_back(param);
    }
};

unsigned key_bytes_from_head(const std::string& head, unsigned fallback_bits)
{
    const std::size_t dash = head.find('-');
    if (dash != std::string::npos) {
        const int bits = std::atoi(head.c_str() + dash + 1);
        if (bits > 0) return static_cast<unsigned>(bits / 8);
    }
    return fallback_bits / 8;
}

std::vector<unsigned char> random_bytes(std::size_t count)
{
    std::vector<unsigned char> out(count);
    std::random_device rd;
    for (auto& b : out) b = static_cast<unsigned char>(rd() & 0xFF);
    return out;
}

}  // namespace

void set_password(const std::string& p)
{
    g_password = p;
}

const std::string& password()
{
    return g_password;
}

bool password_is_set()
{
    return !g_password.empty();
}

bool derive_encryption_key(std::string& method, std::string& error)
{
    // Only the stages with a salt and without a key need derivation.
    std::string result;
    std::size_t start = 0;
    bool changed = false;
    while (start <= method.size()) {
        const std::size_t end = method.find('+', start);
        std::string stage = method.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
        Stage parsed(stage);
        const bool looks_encrypted = parsed.head.find('/') != std::string::npos;
        if (looks_encrypted && !parsed.find('s').empty() && parsed.find('k').empty()) {
            if (!password_is_set()) {
                error = "the archive is encrypted: a password is required (-p)";
                return false;
            }
            const std::string salt_hex = parsed.find('s');
            std::vector<unsigned char> salt;
            if (!from_hex(salt_hex, salt)) {
                error = "invalid salt in encryption stage";
                return false;
            }
            const unsigned key_size = key_bytes_from_head(parsed.head, 256);
            int iterations = 1000;
            const std::string n = parsed.find('n');
            if (!n.empty()) iterations = std::atoi(n.c_str());

            std::vector<unsigned char> derived(key_size + 2);
            Pbkdf2Hmac(reinterpret_cast<const unsigned char*>(g_password.data()),
                       static_cast<int>(g_password.size()), salt.data(),
                       static_cast<int>(salt.size()), iterations, derived.data(),
                       static_cast<int>(derived.size()));

            // The two bytes after the key are the check-code: if they do not
            // match, the password is wrong (this is the only way to tell before
            // attempting decryption).
            const std::string expected_code = parsed.find('c');
            if (!expected_code.empty() &&
                expected_code != to_hex(std::vector<unsigned char>(derived.begin() + key_size,
                                                                  derived.end()))) {
                error = "wrong password (check-code mismatch)";
                return false;
            }

            std::vector<unsigned char> key(derived.begin(), derived.begin() + key_size);
            parsed.set("k" + to_hex(key));
            stage = parsed.str();
            changed = true;
        }
        if (!result.empty()) result += '+';
        result += stage;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (changed) method = result;
    return true;
}

bool make_encryption_stage(unsigned key_bits, const std::string& pwd, std::string& with_key,
                           std::string& stored, std::string& error)
{
    const unsigned key_size = key_bits / 8;
    const unsigned iv_size = 16;
    const int iterations = 1000;

    const auto salt = random_bytes(key_size);
    const auto iv = random_bytes(iv_size);
    std::vector<unsigned char> derived(key_size + 2);
    Pbkdf2Hmac(reinterpret_cast<const unsigned char*>(pwd.data()), static_cast<int>(pwd.size()),
               salt.data(), static_cast<int>(salt.size()), iterations, derived.data(),
               static_cast<int>(derived.size()));

    const std::vector<unsigned char> key(derived.begin(), derived.begin() + key_size);
    const std::vector<unsigned char> check(derived.begin() + key_size, derived.end());

    const std::string head = (key_bits == 256 ? "aes-256" : "aes-" + std::to_string(key_bits)) +
                             "/ctr";
    const std::string common = head + ":n" + std::to_string(iterations) + ":r0:i" + to_hex(iv) +
                               ":s" + to_hex(salt) + ":c" + to_hex(check);

    stored = common;
    with_key = common + ":k" + to_hex(key);
    (void)error;
    return true;
}

}  // namespace fa
