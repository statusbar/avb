// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

/// libFuzzer harness for stun::decode_register_request and
/// stun::decode_register_response. Splits the input byte stream into
/// (key, datagram) pairs and exercises both decoders. The decoders must
/// not crash, hang, or read out of bounds on any input. They may legally
/// return any error_code; what we look for is undefined behavior caught
/// by ASan / UBSan.

#include "statusbar/crypto/aes_siv/aes128_siv.hpp"
#include "statusbar/stun/stun_register.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

using namespace statusbar;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    constexpr size_t key_bytes = 32;
    if (size < key_bytes) {
        return 0;
    }
    crypto::Aes128SivKey key{};
    std::memcpy(key.data.data(), data, key_bytes);

    auto const datagram = std::span<uint8_t const>(data + key_bytes, size - key_bytes);

    {
        stun::RegisterRequest req{};
        (void)stun::decode_register_request(datagram, key, req);
    }
    {
        bool is_success = false;
        stun::RegisterResponseSuccess success{};
        stun::RegisterResponseError err{};
        (void)stun::decode_register_response(datagram, key, is_success, success, err);
    }
    return 0;
}
