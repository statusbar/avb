// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Tests for IEEE 1722.1 ECC wire formats:
//   - EccPublic256Wire (build, verify, tamper detection, serialization roundtrip)
//   - EccPrivate256Wire (build, extract, serialization roundtrip)
//   - P-256 ECDSA chain-of-trust (manufacturer self-sign, manufacturer signs endpoint)

#include "statusbar/avtp_crypto/p256_wire.hpp"

#include "statusbar/crypto/p256/p256_ecdsa.hpp"
#include "statusbar/crypto/util/crypto_util_internal.hpp"
#include "statusbar/test/test.hpp"

using namespace statusbar::crypto;
using namespace statusbar::crypto::avtp;
using statusbar::crypto::internal::span_compare;

//
// ECC_PUBLIC_256 wire format tests (IEEE 1722.1-2021 Table 7-182)
//

TEST(p256_wire, ecc_public_256_wire)
{
    // Generate a manufacturer signing key and an entity key
    uint8_t mfr_seed[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                            0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
    auto mfr_sk = p256_ecdsa_keypair_from_seed(mfr_seed);

    uint8_t entity_seed[32] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
                               0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    auto entity_sk = p256_ecdsa_keypair_from_seed(entity_seed);

    KeyId entity_priv_kid{};
    entity_priv_kid.data = {0x91, 0xE0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x02};
    KeyId mfr_key_id{};
    mfr_key_id.data = {0x00, 0x1B, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x01};

    // Build wire struct
    auto wire = build_ecc_public_256_wire(entity_priv_kid, entity_sk.public_key, mfr_key_id, mfr_sk);

    // Verify fields
    EXPECT_TRUE(wire.related_key_id == entity_priv_kid);
    EXPECT_TRUE(wire.signature_key_id == mfr_key_id);

    // Extract public key and verify it matches
    auto extracted_pk = extract_p256_public_key(wire);
    EXPECT_TRUE(extracted_pk.has_value());
    EXPECT_TRUE(extracted_pk->data == entity_sk.public_key.data);

    // Verify ECDSA signature
    EXPECT_TRUE(verify_ecc_public_256_wire(wire, mfr_sk.public_key));

    // --- Wrong signer ---
    EXPECT_TRUE(!verify_ecc_public_256_wire(wire, entity_sk.public_key));

    // --- Tamper with public_x ---
    {
        auto tampered = wire;
        tampered.public_x[0] ^= 0x01;
        EXPECT_TRUE(!verify_ecc_public_256_wire(tampered, mfr_sk.public_key));
    }

    // --- Tamper with related_key_id ---
    {
        auto tampered = wire;
        tampered.related_key_id.data[7] ^= 0xFF;
        EXPECT_TRUE(!verify_ecc_public_256_wire(tampered, mfr_sk.public_key));
    }

    // --- Tamper with signature ---
    {
        auto tampered = wire;
        tampered.ecdsa_signature_c[0] ^= 0x01;
        EXPECT_TRUE(!verify_ecc_public_256_wire(tampered, mfr_sk.public_key));
    }

    // --- Serialization roundtrip ---
    std::array<uint8_t, ecc_public_256_wire_size> buf{};
    serialize_ecc_public_256_wire(wire, buf);
    auto recovered = deserialize_ecc_public_256_wire(buf);

    EXPECT_TRUE(recovered.related_key_id == wire.related_key_id);
    EXPECT_TRUE(recovered.field_size == wire.field_size);
    EXPECT_TRUE(recovered.public_x == wire.public_x);
    EXPECT_TRUE(recovered.public_y == wire.public_y);
    EXPECT_TRUE(recovered.ecdsa_signature_c == wire.ecdsa_signature_c);
    EXPECT_TRUE(recovered.ecdsa_signature_d == wire.ecdsa_signature_d);

    // Deserialized should still verify
    EXPECT_TRUE(verify_ecc_public_256_wire(recovered, mfr_sk.public_key));

    // --- Curve parameter substitution attack ---
    {
        auto tampered = wire;
        tampered.field_size[31] ^= 0x01;  // Change curve prime
        auto pk = extract_p256_public_key(tampered);
        EXPECT_TRUE(!pk.has_value());
    }
    {
        auto tampered = wire;
        tampered.generator_x[0] ^= 0x01;  // Change generator
        auto pk = extract_p256_public_key(tampered);
        EXPECT_TRUE(!pk.has_value());
    }
}

//
// ECC_PRIVATE_256 wire format tests (IEEE 1722.1-2021 Table 7-183)
//

TEST(p256_wire, ecc_private_256_wire)
{
    uint8_t seed[32] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C};
    auto sk = p256_ecdsa_keypair_from_seed(seed);

    KeyId related_kid{};
    related_kid.data = {0x91, 0xE0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x01};

    // Build wire struct
    auto wire = build_ecc_private_256_wire(related_kid, sk);

    EXPECT_TRUE(wire.related_key_id == related_kid);
    EXPECT_TRUE(span_compare(wire.private_scalar, sk.data));

    // Extract and verify public key is re-derived correctly
    auto extracted = extract_p256_private_key(wire);
    EXPECT_TRUE(extracted.has_value());
    EXPECT_TRUE(extracted->public_key.data == sk.public_key.data);
    EXPECT_TRUE(span_compare(extracted->data, sk.data));

    // Serialization roundtrip
    std::array<uint8_t, ecc_private_256_wire_size> buf{};
    serialize_ecc_private_256_wire(wire, buf);
    auto recovered = deserialize_ecc_private_256_wire(buf);

    EXPECT_TRUE(recovered.related_key_id == wire.related_key_id);
    EXPECT_TRUE(recovered.private_scalar == wire.private_scalar);
    EXPECT_TRUE(recovered.field_size == wire.field_size);
    EXPECT_TRUE(recovered.generator_x == wire.generator_x);

    // --- Curve parameter substitution attack ---
    {
        auto tampered = wire;
        tampered.semiminor[0] ^= 0x01;  // Change curve b coefficient
        auto priv = extract_p256_private_key(tampered);
        EXPECT_TRUE(!priv.has_value());
    }
}

//
// ECC chain of trust: manufacturer signs entity key (P-256 ECDSA)
//

TEST(p256_wire, ecc_chain_of_trust)
{
    // Manufacturer root key
    uint8_t mfr_seed[32] = {};
    for (int i = 0; i < 32; ++i) {
        mfr_seed[i] = static_cast<uint8_t>(i + 0x10);
    }
    auto mfr_sk = p256_ecdsa_keypair_from_seed(mfr_seed);

    KeyId mfr_pub_kid{};
    mfr_pub_kid.data = {0x00, 0x1B, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x01};
    KeyId mfr_priv_kid{};
    mfr_priv_kid.data = {0x00, 0x1B, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x02};

    // Manufacturer self-signs its own public key
    auto mfr_wire = build_ecc_public_256_wire(mfr_priv_kid, mfr_sk.public_key, mfr_pub_kid, mfr_sk);
    EXPECT_TRUE(verify_ecc_public_256_wire(mfr_wire, mfr_sk.public_key));

    // Entity key
    uint8_t entity_seed[32] = {};
    for (int i = 0; i < 32; ++i) {
        entity_seed[i] = static_cast<uint8_t>(i + 0xA0);
    }
    auto entity_sk = p256_ecdsa_keypair_from_seed(entity_seed);

    KeyId entity_pub_kid{};
    entity_pub_kid.data = {0x91, 0xE0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x10};
    KeyId entity_priv_kid{};
    entity_priv_kid.data = {0x91, 0xE0, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x11};

    // Manufacturer signs entity's public key
    auto entity_wire = build_ecc_public_256_wire(entity_priv_kid, entity_sk.public_key, mfr_pub_kid, mfr_sk);
    EXPECT_TRUE(verify_ecc_public_256_wire(entity_wire, mfr_sk.public_key));

    // Verify with wrong key fails
    EXPECT_TRUE(!verify_ecc_public_256_wire(entity_wire, entity_sk.public_key));
}

//

TEST_MAIN(statusbar_avtp_crypto, p256_wire_test)
