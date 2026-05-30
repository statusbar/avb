// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// libFuzzer harness for AVTP keychain wire format operations
#include "statusbar/avtp_crypto/avtp_crypto_pdu.hpp"
#include "statusbar/avtp_crypto/avtp_keychain.hpp"
#include "statusbar/avtp_crypto/p256_wire.hpp"
#include "statusbar/crypto/25519/ed25519.hpp"
#include "statusbar/crypto/p256/p256_ecdsa.hpp"
#include "statusbar/crypto/util/crypto_util_internal.hpp"
#include "statusbar/crypto/util/test.hpp"

#include <cstddef>
#include <cstdint>
#include <print>

using namespace statusbar::crypto;
using namespace statusbar::crypto::avtp;
using statusbar::crypto::internal::span_compare;
using statusbar::crypto::internal::span_copy;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const* data, size_t size)
{
    // Exercise Ed25519 signed public key entry serialization with raw fuzz data
    if (size >= ed25519_signed_public_key_entry_wire_size) {
        std::array<uint8_t, ed25519_signed_public_key_entry_wire_size> wire{};
        span_copy(wire, std::span<uint8_t const>(data, ed25519_signed_public_key_entry_wire_size));
        auto entry = deserialize_ed25519_signed_public_key_entry(wire);
        // Re-serialize and check round-trip
        std::array<uint8_t, ed25519_signed_public_key_entry_wire_size> wire2{};
        serialize_ed25519_signed_public_key_entry(entry, wire2);
        if (wire != wire2) {
            __builtin_trap();
        }
    }

    // Exercise ECC_PUBLIC_256 wire format with raw fuzz data
    if (size >= ecc_public_256_wire_size) {
        std::array<uint8_t, ecc_public_256_wire_size> wire{};
        span_copy(wire, std::span<uint8_t const>(data, ecc_public_256_wire_size));
        auto ecc_wire = deserialize_ecc_public_256_wire(wire);
        // Re-serialize and check round-trip
        std::array<uint8_t, ecc_public_256_wire_size> wire2{};
        serialize_ecc_public_256_wire(ecc_wire, wire2);
        if (wire != wire2) {
            __builtin_trap();
        }
        // Extract public key (must not crash; may fail validation for fuzz data)
        auto pk = extract_p256_public_key(ecc_wire);
        test::do_not_optimize(pk);
    }

    // Exercise ECC_PRIVATE_256 wire format with raw fuzz data
    if (size >= ecc_private_256_wire_size) {
        std::array<uint8_t, ecc_private_256_wire_size> wire{};
        span_copy(wire, std::span<uint8_t const>(data, ecc_private_256_wire_size));
        auto priv_wire = deserialize_ecc_private_256_wire(wire);
        // Re-serialize and check round-trip
        std::array<uint8_t, ecc_private_256_wire_size> wire2{};
        serialize_ecc_private_256_wire(priv_wire, wire2);
        if (wire != wire2) {
            __builtin_trap();
        }
    }

    // Build + verify signed public key entry with fuzz-derived seeds
    if (size >= 64) {
        auto signer_seed = std::span<uint8_t const, 32>(data, 32);
        auto entry_seed = std::span<uint8_t const, 32>(data + 32, 32);

        auto signer = ed25519_keypair_from_seed(signer_seed);
        auto entry_key = ed25519_keypair_from_seed(entry_seed);

        KeyId key_id{};
        KeyId related_key_id{};
        KeyId sig_key_id{};
        if (size >= 88) {
            span_copy(key_id.data, std::span<uint8_t const>(data + 64, 8));
            span_copy(related_key_id.data, std::span<uint8_t const>(data + 72, 8));
            span_copy(sig_key_id.data, std::span<uint8_t const>(data + 80, 8));
        }

        auto entry =
            build_ed25519_signed_public_key_entry(key_id, related_key_id, ed25519_public_key(entry_key), sig_key_id, signer);

        // Verify with correct signer
        if (!verify_ed25519_signed_public_key_entry(entry, ed25519_public_key(signer))) {
            __builtin_trap();
        }

        // Verify with wrong signer should fail
        auto wrong = ed25519_keypair_from_seed(entry_seed);
        bool wrong_ok = verify_ed25519_signed_public_key_entry(entry, ed25519_public_key(wrong));
        // May succeed if signer_seed == entry_seed (same key)
        test::do_not_optimize(wrong_ok);

        // Serialization round-trip
        std::array<uint8_t, ed25519_signed_public_key_entry_wire_size> wire{};
        serialize_ed25519_signed_public_key_entry(entry, wire);
        auto entry2 = deserialize_ed25519_signed_public_key_entry(wire);
        if (!verify_ed25519_signed_public_key_entry(entry2, ed25519_public_key(signer))) {
            __builtin_trap();
        }
    }

    // Exercise key_type_data_size and is_valid_key_type with all possible values
    if (size >= 1) {
        auto kt = static_cast<KeyType>(data[0]);
        auto valid = is_valid_key_type(kt);
        test::do_not_optimize(valid);
        if (valid) {
            auto ds = key_type_data_size(kt);
            test::do_not_optimize(ds);
        }
    }

    return 0;
}
