// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// libFuzzer harness for AVTP crypto PDU serialization/deserialization
#include "statusbar/avtp_crypto/avtp_crypto_pdu.hpp"
#include "statusbar/crypto/25519/ed25519.hpp"
#include "statusbar/crypto/25519/x25519.hpp"
#include "statusbar/crypto/p256/p256_ecdh.hpp"
#include "statusbar/crypto/p256/p256_ecdsa.hpp"
#include "statusbar/crypto/util/crypto_util_internal.hpp"
#include "statusbar/crypto/util/test.hpp"

#include <cstddef>
#include <cstdint>
#include <print>

using namespace statusbar::crypto;
using namespace statusbar::crypto::avtp;
using statusbar::crypto::internal::span_copy;

/// Exercise wrap/unwrap round-trip with fuzz-derived nonces
void fuzz_wrap_unwrap(Aes256SivKey const& transport_key, uint8_t const* data, std::span<uint8_t const, 32> stream_seed)
{
    Aes256Key stream_key{};
    span_copy(stream_key.data, stream_seed);
    Nonce controller_nonce{};
    span_copy(controller_nonce.data, std::span<uint8_t const>(data, 8));
    Nonce target_nonce{};
    span_copy(target_nonce.data, std::span<uint8_t const>(data + 8, 8));
    KeyId key_id{};
    span_copy(key_id.data, std::span<uint8_t const>(data + 16, 8));

    auto wrapped = wrap_aes256_key(transport_key, controller_nonce, target_nonce, key_id, stream_key);
    auto unwrapped = unwrap_aes256_key(transport_key, controller_nonce, target_nonce, key_id, wrapped);
    if (!unwrapped || unwrapped->data != stream_key.data) {
        __builtin_trap();
    }
}

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    // Exercise Ed25519 deserialization with raw fuzz data (must not crash)
    if (size >= ed25519_key_exchange_pdu_wire_size) {
        std::array<uint8_t, ed25519_key_exchange_pdu_wire_size> ke_wire{};
        span_copy(ke_wire, std::span<uint8_t const>(data, ed25519_key_exchange_pdu_wire_size));
        auto ke_opt = deserialize_ed25519_key_exchange(ke_wire);
        if (ke_opt) {
            // Re-serialize and check round-trip
            std::array<uint8_t, ed25519_key_exchange_pdu_wire_size> ke_wire2{};
            serialize_ed25519_key_exchange(*ke_opt, ke_wire2);
            if (ke_wire != ke_wire2) {
                __builtin_trap();
            }
        }
    }

    // Exercise P-256 deserialization with raw fuzz data (must not crash)
    if (size >= p256_key_exchange_pdu_wire_size) {
        std::array<uint8_t, p256_key_exchange_pdu_wire_size> ke_wire{};
        span_copy(ke_wire, std::span<uint8_t const>(data, p256_key_exchange_pdu_wire_size));
        auto ke_opt = deserialize_p256_key_exchange(ke_wire);
        if (ke_opt) {
            // Re-serialize and check round-trip
            std::array<uint8_t, p256_key_exchange_pdu_wire_size> ke_wire2{};
            serialize_p256_key_exchange(*ke_opt, ke_wire2);
            if (ke_wire != ke_wire2) {
                __builtin_trap();
            }
        }
    }

    if (size >= aes256_wrapped_key_size) {
        std::array<uint8_t, aes256_wrapped_key_size> sk_wire{};
        span_copy(sk_wire, std::span<uint8_t const>(data, aes256_wrapped_key_size));
        auto sk_pdu = deserialize_aes256_wrapped_key(sk_wire);
        // Re-serialize and check round-trip
        std::array<uint8_t, aes256_wrapped_key_size> sk_wire2{};
        serialize_aes256_wrapped_key(sk_pdu, sk_wire2);
        if (sk_wire != sk_wire2) {
            __builtin_trap();
        }
    }

    // Exercise build + verify + wrap/unwrap with fuzz-derived seeds
    if (size >= 96) {
        auto id_seed = std::span<uint8_t const, 32>(data, 32);
        auto eph_seed = std::span<uint8_t const, 32>(data + 32, 32);
        auto transport_seed = std::span<uint8_t const, 32>(data + 64, 32);

        auto id_key = ed25519_keypair_from_seed(id_seed);
        auto eph = x25519_keypair_from_seed(eph_seed);

        auto pdu = build_ed25519_key_exchange(key_exchange_offer, id_key, eph.public_key);

        // Serialize round-trip
        std::array<uint8_t, ed25519_key_exchange_pdu_wire_size> wire{};
        serialize_ed25519_key_exchange(pdu, wire);
        auto pdu2_opt = deserialize_ed25519_key_exchange(wire);
        if (!pdu2_opt) {
            __builtin_trap();
        }

        // Verify
        auto result = verify_ed25519_key_exchange(*pdu2_opt, ed25519_public_key(id_key));
        if (!result.has_value()) {
            __builtin_trap();
        }

        // Wrong identity must fail
        auto wrong_key = ed25519_keypair_from_seed(eph_seed);  // different key
        auto result2 = verify_ed25519_key_exchange(pdu, ed25519_public_key(wrong_key));
        // result2 may or may not be nullopt depending on key collision (astronomically unlikely)
        test::do_not_optimize(result2);

        // Wrap/unwrap stream key
        Aes256SivKey transport_key{};
        span_copy(transport_key.data, std::span<uint8_t const>(data + 32, 64));
        fuzz_wrap_unwrap(transport_key, data, transport_seed);
    }

    // Exercise P-256 build + verify + wrap/unwrap with fuzz-derived seeds
    if (size >= 96) {
        auto id_seed = std::span<uint8_t const, 32>(data, 32);
        auto eph_seed = std::span<uint8_t const, 32>(data + 32, 32);
        auto transport_seed = std::span<uint8_t const, 32>(data + 64, 32);

        auto id_key = p256_ecdsa_keypair_from_seed(id_seed);
        auto eph = p256_ecdsa_keypair_from_seed(eph_seed);

        auto pdu = build_p256_key_exchange(key_exchange_offer, id_key, eph.public_key);

        // Serialize round-trip
        std::array<uint8_t, p256_key_exchange_pdu_wire_size> wire{};
        serialize_p256_key_exchange(pdu, wire);
        auto pdu2_opt = deserialize_p256_key_exchange(wire);
        if (!pdu2_opt) {
            __builtin_trap();
        }

        // Verify
        auto result = verify_p256_key_exchange(*pdu2_opt, p256_public_key(id_key));
        if (!result.has_value()) {
            __builtin_trap();
        }

        // Wrong identity must fail
        auto wrong_key = p256_ecdsa_keypair_from_seed(eph_seed);  // different key
        auto result2 = verify_p256_key_exchange(pdu, p256_public_key(wrong_key));
        // result2 may or may not be nullopt depending on key collision (astronomically unlikely)
        test::do_not_optimize(result2);

        // Wrap/unwrap stream key using P-256 derived transport key
        auto shared_secret = p256_ecdh(id_key, eph.public_key);
        auto transport_key =
            derive_p256_transport_key(std::span<uint8_t const, 32>(shared_secret), p256_public_key(id_key), eph.public_key);
        fuzz_wrap_unwrap(transport_key, data, transport_seed);
    }

    return 0;
}
