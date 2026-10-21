#pragma once

#include <cstdint>
#include <string>

namespace fa {

// Password used for encrypted archives (set by the CLI). Empty means none.
void set_password(const std::string& password);
const std::string& password();
bool password_is_set();

// Adds the derived key (:k) to an encryption stage that carries a salt (:s) but
// no key, using the current password. Returns false if the password is missing
// or the stage is malformed.
bool derive_encryption_key(std::string& method, std::string& error);

// Builds an encryption stage for writing: generates salt and IV, derives the
// key and returns two texts: `with_key` (used to compress) and `stored` (what
// goes into the archive directory, without the key).
// `algorithm` is "aes", "aes-128", "blowfish", "serpent" or "twofish".
bool make_encryption_stage(const std::string& algorithm, const std::string& password,
                           std::string& with_key, std::string& stored, std::string& error);

}  // namespace fa
