// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// Tests for AVTP wire-format PDU serialization/deserialization and AEF encrypt/decrypt:
//   - Key exchange PDU roundtrip (Ed25519, P-256)
//   - AUTH_GET_NONCE roundtrip (command + response)
//   - AUTH_ADD_KEY_NONCE roundtrip (header + response)
//   - Invalid message type rejection
//   - AEF encrypt/decrypt roundtrips (AES-SIV, AES-GCM-SIV)
//   - AEF authentication failures and edge cases

#include "statusbar/avtp_crypto/avtp_crypto_pdu.hpp"

#include "statusbar/buffer/span_utils.hpp"
#include "statusbar/crypto/25519/ed25519.hpp"
#include "statusbar/crypto/25519/x25519.hpp"
#include "statusbar/crypto/p256/p256_ecdsa.hpp"
#include "statusbar/crypto/util/crypto_util_internal.hpp"
#include "statusbar/test/test.hpp"

using namespace statusbar::crypto;
using namespace statusbar::crypto::avtp;
using statusbar::make_span;
using statusbar::span_copy;
using statusbar::crypto::internal::span_compare;

// ===========================================================================
// Key exchange wire serialization roundtrips
// ===========================================================================

TEST(avtp_crypto_pdu, ed25519_key_exchange_serialization)
{
    // clang-format off
    std::array<uint8_t, 32> seed = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
    };
    std::array<uint8_t, 32> eph_seed = {
        0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,
        0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,
    };
    // clang-format on

    auto id_key = ed25519_keypair_from_seed(seed);
    auto eph = x25519_keypair_from_seed(eph_seed);

    auto pdu = build_ed25519_key_exchange(key_exchange_offer, id_key, eph.public_key);

    // Serialize
    std::array<uint8_t, ed25519_key_exchange_pdu_wire_size> wire{};
    serialize_ed25519_key_exchange(pdu, wire);

    // Deserialize
    auto pdu2_opt = deserialize_ed25519_key_exchange(wire);
    EXPECT_TRUE(pdu2_opt.has_value());
    auto& pdu2 = *pdu2_opt;

    EXPECT_TRUE(pdu2.message_type == pdu.message_type);
    EXPECT_TRUE(span_compare(pdu2.sender_identity.data, pdu.sender_identity.data));
    EXPECT_TRUE(span_compare(pdu2.ephemeral_pubkey.data, pdu.ephemeral_pubkey.data));
    EXPECT_TRUE(span_compare(pdu2.signature.data, pdu.signature.data));

    // Deserialized PDU should still verify
    auto result = verify_ed25519_key_exchange(pdu2, ed25519_public_key(id_key));
    EXPECT_TRUE(result.has_value());
}

TEST(avtp_crypto_pdu, p256_key_exchange_serialization)
{
    // clang-format off
    std::array<uint8_t, 32> seed = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
    };
    std::array<uint8_t, 32> eph_seed = {
        0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,
        0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,
    };
    // clang-format on

    auto id_key = p256_ecdsa_keypair_from_seed(seed);
    auto eph = p256_ecdsa_keypair_from_seed(eph_seed);

    auto pdu = build_p256_key_exchange(key_exchange_offer, id_key, eph.public_key);

    // Serialize
    std::array<uint8_t, p256_key_exchange_pdu_wire_size> wire{};
    serialize_p256_key_exchange(pdu, wire);

    // Deserialize
    auto pdu2_opt = deserialize_p256_key_exchange(wire);
    EXPECT_TRUE(pdu2_opt.has_value());
    auto& pdu2 = *pdu2_opt;

    EXPECT_TRUE(pdu2.message_type == pdu.message_type);
    EXPECT_TRUE(span_compare(pdu2.sender_identity.data, pdu.sender_identity.data));
    EXPECT_TRUE(span_compare(pdu2.ephemeral_pubkey.data, pdu.ephemeral_pubkey.data));
    EXPECT_TRUE(span_compare(pdu2.signature.data, pdu.signature.data));

    // Deserialized PDU should still verify
    auto result = verify_p256_key_exchange(pdu2, p256_public_key(id_key));
    EXPECT_TRUE(result.has_value());
}

// ===========================================================================
// AUTH_GET_NONCE serialization roundtrip
// ===========================================================================

TEST(avtp_crypto_pdu, auth_get_nonce_serialization)
{
    // AuthGetNoncePayload roundtrip
    AuthGetNoncePayload cmd{};
    cmd.controller_nonce.data = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};

    std::array<uint8_t, auth_get_nonce_payload_size> wire{};
    serialize_auth_get_nonce(cmd, wire);

    auto cmd2 = deserialize_auth_get_nonce(wire);
    EXPECT_TRUE(span_compare(cmd2.controller_nonce.data, cmd.controller_nonce.data));

    // AuthGetNonceResponsePayload roundtrip
    AuthGetNonceResponsePayload resp{};
    resp.controller_nonce.data = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};
    resp.target_nonce.data = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7};

    std::array<uint8_t, auth_get_nonce_response_payload_size> resp_wire{};
    serialize_auth_get_nonce_response(resp, resp_wire);

    auto resp2 = deserialize_auth_get_nonce_response(resp_wire);
    EXPECT_TRUE(span_compare(resp2.controller_nonce.data, resp.controller_nonce.data));
    EXPECT_TRUE(span_compare(resp2.target_nonce.data, resp.target_nonce.data));
}

// ===========================================================================
// AUTH_ADD_KEY_NONCE header and response serialization roundtrip
// ===========================================================================

TEST(avtp_crypto_pdu, auth_add_key_nonce_header_serialization)
{
    // AuthAddKeyNonceHeader roundtrip
    AuthAddKeyNonceHeader hdr{};
    hdr.controller_nonce.data = {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7};
    hdr.target_nonce.data = {0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7};
    hdr.key_id.data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    hdr.key_type = KeyType::aes256;
    hdr.key_length = 32;

    std::array<uint8_t, auth_add_key_nonce_header_size> hdr_wire{};
    serialize_auth_add_key_nonce_header(hdr, hdr_wire);

    auto hdr2_opt = deserialize_auth_add_key_nonce_header(hdr_wire);
    EXPECT_TRUE(hdr2_opt.has_value());
    auto& hdr2 = *hdr2_opt;

    EXPECT_TRUE(span_compare(hdr2.controller_nonce.data, hdr.controller_nonce.data));
    EXPECT_TRUE(span_compare(hdr2.target_nonce.data, hdr.target_nonce.data));
    EXPECT_TRUE(span_compare(hdr2.key_id.data, hdr.key_id.data));
    EXPECT_TRUE(hdr2.key_type == hdr.key_type);
    EXPECT_TRUE(hdr2.key_length == hdr.key_length);

    // AuthAddKeyNonceResponsePayload roundtrip
    AuthAddKeyNonceResponsePayload resp{};
    resp.controller_nonce.data = {0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7};
    resp.target_nonce.data = {0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7};
    resp.key_id.data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

    std::array<uint8_t, auth_add_key_nonce_response_payload_size> resp_wire{};
    serialize_auth_add_key_nonce_response(resp, resp_wire);

    auto resp2 = deserialize_auth_add_key_nonce_response(resp_wire);
    EXPECT_TRUE(span_compare(resp2.controller_nonce.data, resp.controller_nonce.data));
    EXPECT_TRUE(span_compare(resp2.target_nonce.data, resp.target_nonce.data));
    EXPECT_TRUE(span_compare(resp2.key_id.data, resp.key_id.data));
}

// ===========================================================================
// P-256 deserialization failure: invalid message type
// ===========================================================================

TEST(avtp_crypto_pdu, p256_deserialize_invalid_message_type)
{
    // clang-format off
    std::array<uint8_t, 32> seed = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
    };
    std::array<uint8_t, 32> eph_seed = {
        0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,0x50,
        0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,0x60,
    };
    // clang-format on

    auto id_key = p256_ecdsa_keypair_from_seed(seed);
    auto eph = p256_ecdsa_keypair_from_seed(eph_seed);

    auto pdu = build_p256_key_exchange(key_exchange_offer, id_key, eph.public_key);

    std::array<uint8_t, p256_key_exchange_pdu_wire_size> wire{};
    serialize_p256_key_exchange(pdu, wire);

    // Set message type to 0x00 (below valid range)
    wire[0] = 0x00;
    auto result1 = deserialize_p256_key_exchange(wire);
    EXPECT_TRUE(!result1.has_value());

    // Set message type to 0x04 (above valid range)
    wire[0] = 0x04;
    auto result2 = deserialize_p256_key_exchange(wire);
    EXPECT_TRUE(!result2.has_value());

    // Set message type to 0xFF (way above valid range)
    wire[0] = 0xFF;
    auto result3 = deserialize_p256_key_exchange(wire);
    EXPECT_TRUE(!result3.has_value());
}

// ===========================================================================
// Helper: build a SessionKeyChain with a single key entry
// ===========================================================================

static auto make_siv128_keychain(KeyId const& kid) -> std::pair<SessionKeyChain, Aes128SivKey>
{
    // clang-format off
    Aes128SivKey key{};
    key.data = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
        0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
    };
    // clang-format on
    SessionKeyChain chain;
    chain.push_back(Aes128SivKeyEntry{kid, key});
    return {chain, key};
}

static auto make_siv256_keychain(KeyId const& kid) -> std::pair<SessionKeyChain, Aes256SivKey>
{
    Aes256SivKey key{};
    for (size_t i = 0; i < Aes256SivKey::LENGTH; ++i) {
        key.data[i] = static_cast<uint8_t>(i + 0x30);
    }
    SessionKeyChain chain;
    chain.push_back(Aes256SivKeyEntry{kid, key});
    return {chain, key};
}

static auto make_aes128_keychain(KeyId const& kid) -> std::pair<SessionKeyChain, Aes128Key>
{
    // clang-format off
    Aes128Key key{};
    key.data = {
        0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,
    };
    // clang-format on
    SessionKeyChain chain;
    chain.push_back(Aes128KeyEntry{kid, key});
    return {chain, key};
}

static auto make_aes256_keychain(KeyId const& kid) -> std::pair<SessionKeyChain, Aes256Key>
{
    Aes256Key key{};
    for (size_t i = 0; i < Aes256Key::LENGTH; ++i) {
        key.data[i] = static_cast<uint8_t>(i + 0xC0);
    }
    SessionKeyChain chain;
    chain.push_back(Aes256KeyEntry{kid, key});
    return {chain, key};
}

static KeyId const test_kid = {{{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07}}};

// clang-format off
static std::array<uint8_t, 64> const test_plaintext = {
    0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0x00,
    0x11,0x21,0x31,0x41,0x51,0x61,0x71,0x81,0x91,0xA1,0xB1,0xC1,0xD1,0xE1,0xF1,0x01,
    0x12,0x22,0x32,0x42,0x52,0x62,0x72,0x82,0x92,0xA2,0xB2,0xC2,0xD2,0xE2,0xF2,0x02,
    0x13,0x23,0x33,0x43,0x53,0x63,0x73,0x83,0x93,0xA3,0xB3,0xC3,0xD3,0xE3,0xF3,0x03,
};
// clang-format on

static std::array<uint8_t, 8> const test_aad = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};

// ===========================================================================
// AEF encrypt/decrypt roundtrip tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_siv_aes128_roundtrip)
{
    auto [chain, key] = make_siv128_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, nullptr);
    EXPECT_TRUE(enc_result.has_value());
    EXPECT_TRUE(enc_result->size() == 64 + aef_siv_overhead);

    std::array<uint8_t, 64> dec_buf{};
    auto dec_result = aef_decrypt(aef_enc_aes_siv, chain, test_kid, *enc_result, dec_buf, test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(dec_result->size() == 64);
    EXPECT_TRUE(span_compare(*dec_result, std::span<uint8_t const>(test_plaintext)));
}

TEST(avtp_crypto_pdu, aef_siv_aes256_roundtrip)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, nullptr);
    EXPECT_TRUE(enc_result.has_value());

    std::array<uint8_t, 64> dec_buf{};
    auto dec_result = aef_decrypt(aef_enc_aes_siv, chain, test_kid, *enc_result, dec_buf, test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(span_compare(*dec_result, std::span<uint8_t const>(test_plaintext)));
}

TEST(avtp_crypto_pdu, aef_gcm_siv_aes128_roundtrip)
{
    auto [chain, key] = make_aes128_keychain(test_kid);
    (void)key;

    AefNonceCounter nonce = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};

    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, &nonce);
    EXPECT_TRUE(enc_result.has_value());
    EXPECT_TRUE(enc_result->size() == 64 + aef_gcm_siv_overhead);

    std::array<uint8_t, 64> dec_buf{};
    auto dec_result = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, *enc_result, dec_buf, test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(span_compare(*dec_result, std::span<uint8_t const>(test_plaintext)));
}

TEST(avtp_crypto_pdu, aef_gcm_siv_aes256_roundtrip)
{
    auto [chain, key] = make_aes256_keychain(test_kid);
    (void)key;

    AefNonceCounter nonce = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0};

    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, &nonce);
    EXPECT_TRUE(enc_result.has_value());

    std::array<uint8_t, 64> dec_buf{};
    auto dec_result = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, *enc_result, dec_buf, test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(span_compare(*dec_result, std::span<uint8_t const>(test_plaintext)));
}

TEST(avtp_crypto_pdu, aef_siv_empty_plaintext)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    std::array<uint8_t, aef_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_siv, chain, test_kid, std::span<uint8_t const>{}, enc_buf, test_aad, nullptr);
    EXPECT_TRUE(enc_result.has_value());
    EXPECT_TRUE(enc_result->size() == aef_siv_overhead);

    std::array<uint8_t, 1> dec_buf{};  // won't be used (0-byte plaintext)
    auto dec_result = aef_decrypt(aef_enc_aes_siv, chain, test_kid, *enc_result, std::span<uint8_t>(dec_buf.data(), 0), test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(dec_result->size() == 0);
}

TEST(avtp_crypto_pdu, aef_gcm_siv_empty_plaintext)
{
    auto [chain, key] = make_aes256_keychain(test_kid);
    (void)key;

    AefNonceCounter nonce = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE};

    std::array<uint8_t, aef_gcm_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, std::span<uint8_t const>{}, enc_buf, test_aad, &nonce);
    EXPECT_TRUE(enc_result.has_value());
    EXPECT_TRUE(enc_result->size() == aef_gcm_siv_overhead);

    std::array<uint8_t, 1> dec_buf{};
    auto dec_result =
        aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, *enc_result, std::span<uint8_t>(dec_buf.data(), 0), test_aad);
    EXPECT_TRUE(dec_result.has_value());
    EXPECT_TRUE(dec_result->size() == 0);
}

// ===========================================================================
// AEF authentication failure tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_siv_tampered)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, nullptr);
    EXPECT_TRUE(enc_result.has_value());

    // Tamper with ciphertext byte
    {
        std::array<uint8_t, 64 + aef_siv_overhead> tampered{};
        span_copy(make_span(tampered), *enc_result);
        tampered[aef_siv_overhead + 5] ^= 0xFF;
        std::array<uint8_t, 64> dec_buf{};
        auto r = aef_decrypt(aef_enc_aes_siv, chain, test_kid, tampered, dec_buf, test_aad);
        EXPECT_TRUE(!r.has_value());
    }
    // Tamper with SIV tag byte
    {
        std::array<uint8_t, 64 + aef_siv_overhead> tampered{};
        span_copy(make_span(tampered), *enc_result);
        tampered[3] ^= 0xFF;
        std::array<uint8_t, 64> dec_buf{};
        auto r = aef_decrypt(aef_enc_aes_siv, chain, test_kid, tampered, dec_buf, test_aad);
        EXPECT_TRUE(!r.has_value());
    }
}

TEST(avtp_crypto_pdu, aef_gcm_siv_tampered)
{
    auto [chain, key] = make_aes256_keychain(test_kid);
    (void)key;

    AefNonceCounter nonce = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
    auto enc_result = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, &nonce);
    EXPECT_TRUE(enc_result.has_value());

    // Tamper with ciphertext byte
    {
        std::array<uint8_t, 64 + aef_gcm_siv_overhead> tampered{};
        span_copy(make_span(tampered), *enc_result);
        tampered[aes_gcm_siv_nonce_size + 5] ^= 0xFF;
        std::array<uint8_t, 64> dec_buf{};
        auto r = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, tampered, dec_buf, test_aad);
        EXPECT_TRUE(!r.has_value());
    }
    // Tamper with tag byte (last 16 bytes)
    {
        std::array<uint8_t, 64 + aef_gcm_siv_overhead> tampered{};
        span_copy(make_span(tampered), *enc_result);
        tampered[enc_result->size() - 3] ^= 0xFF;
        std::array<uint8_t, 64> dec_buf{};
        auto r = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, tampered, dec_buf, test_aad);
        EXPECT_TRUE(!r.has_value());
    }
    // Tamper with nonce byte (first 12 bytes)
    {
        std::array<uint8_t, 64 + aef_gcm_siv_overhead> tampered{};
        span_copy(make_span(tampered), *enc_result);
        tampered[2] ^= 0xFF;
        std::array<uint8_t, 64> dec_buf{};
        auto r = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, tampered, dec_buf, test_aad);
        EXPECT_TRUE(!r.has_value());
    }
}

// ===========================================================================
// AEF AAD mismatch tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_wrong_aad)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 3> aad_a = {0x01, 0x02, 0x03};
    std::array<uint8_t, 3> aad_b = {0x04, 0x05, 0x06};

    // AES-SIV: encrypt with aad_a, decrypt with aad_b → fail
    {
        std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
        auto enc = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, aad_a, nullptr);
        EXPECT_TRUE(enc.has_value());
        std::array<uint8_t, 64> dec_buf{};
        auto dec = aef_decrypt(aef_enc_aes_siv, chain, test_kid, *enc, dec_buf, aad_b);
        EXPECT_TRUE(!dec.has_value());
    }

    // AES-GCM-SIV: encrypt with aad_a, decrypt with aad_b → fail
    {
        auto [chain2, key2] = make_aes128_keychain(test_kid);
        (void)key2;
        AefNonceCounter nonce = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
        auto enc = aef_encrypt(aef_enc_aes_gcm_siv, chain2, test_kid, test_plaintext, enc_buf, aad_a, &nonce);
        EXPECT_TRUE(enc.has_value());
        std::array<uint8_t, 64> dec_buf{};
        auto dec = aef_decrypt(aef_enc_aes_gcm_siv, chain2, test_kid, *enc, dec_buf, aad_b);
        EXPECT_TRUE(!dec.has_value());
    }
}

// ===========================================================================
// AEF key error tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_key_errors)
{
    // Wrong key type: enc=0 with Aes128KeyEntry (not SIV)
    {
        auto [chain, key] = make_aes128_keychain(test_kid);
        (void)key;
        std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
        auto r = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, {}, nullptr);
        EXPECT_TRUE(!r.has_value());
    }
    // Wrong key type: enc=1 with Aes128SivKeyEntry
    {
        auto [chain, key] = make_siv128_keychain(test_kid);
        (void)key;
        AefNonceCounter nonce{};
        std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
        auto r = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf, {}, &nonce);
        EXPECT_TRUE(!r.has_value());
    }
    // Key not found
    {
        auto [chain, key] = make_siv256_keychain(test_kid);
        (void)key;
        KeyId bad_kid = {{{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}};
        std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
        auto r = aef_encrypt(aef_enc_aes_siv, chain, bad_kid, test_plaintext, enc_buf, {}, nullptr);
        EXPECT_TRUE(!r.has_value());

        std::array<uint8_t, 64> dec_buf{};
        auto r2 = aef_decrypt(aef_enc_aes_siv, chain, bad_kid, enc_buf, dec_buf);
        EXPECT_TRUE(!r2.has_value());
    }
}

// ===========================================================================
// AEF nonce management tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_gcm_siv_nonce_increment)
{
    auto [chain, key] = make_aes128_keychain(test_kid);
    (void)key;

    AefNonceCounter nonce = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    AefNonceCounter nonce_before = nonce;

    // First encrypt
    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf1{};
    auto enc1 = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf1, test_aad, &nonce);
    EXPECT_TRUE(enc1.has_value());

    // Nonce should have been incremented
    EXPECT_TRUE(nonce[11] == 0x02);

    // Second encrypt
    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf2{};
    auto enc2 = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf2, test_aad, &nonce);
    EXPECT_TRUE(enc2.has_value());
    EXPECT_TRUE(nonce[11] == 0x03);

    // The two outputs should differ (different nonces → different ciphertext)
    EXPECT_TRUE(!span_compare(*enc1, *enc2));

    // Both should decrypt correctly
    std::array<uint8_t, 64> dec_buf{};
    auto dec1 = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, *enc1, dec_buf, test_aad);
    EXPECT_TRUE(dec1.has_value());
    EXPECT_TRUE(span_compare(*dec1, std::span<uint8_t const>(test_plaintext)));

    auto dec2 = aef_decrypt(aef_enc_aes_gcm_siv, chain, test_kid, *enc2, dec_buf, test_aad);
    EXPECT_TRUE(dec2.has_value());
    EXPECT_TRUE(span_compare(*dec2, std::span<uint8_t const>(test_plaintext)));
}

TEST(avtp_crypto_pdu, aef_gcm_siv_null_nonce)
{
    auto [chain, key] = make_aes128_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 64 + aef_gcm_siv_overhead> enc_buf{};
    auto r = aef_encrypt(aef_enc_aes_gcm_siv, chain, test_kid, test_plaintext, enc_buf, test_aad, nullptr);
    EXPECT_TRUE(!r.has_value());
}

// ===========================================================================
// AEF edge case tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_invalid_enc)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    std::array<uint8_t, 128> enc_buf{};
    auto r = aef_encrypt(2, chain, test_kid, test_plaintext, enc_buf, {}, nullptr);
    EXPECT_TRUE(!r.has_value());

    std::array<uint8_t, 64> dec_buf{};
    auto r2 = aef_decrypt(2, chain, test_kid, enc_buf, dec_buf);
    EXPECT_TRUE(!r2.has_value());
}

TEST(avtp_crypto_pdu, aef_size_helpers)
{
    EXPECT_TRUE(aef_encrypted_size(aef_enc_aes_siv, 64) == 64 + 16);
    EXPECT_TRUE(aef_encrypted_size(aef_enc_aes_gcm_siv, 64) == 64 + 28);
    EXPECT_TRUE(aef_encrypted_size(2, 64) == 0);

    EXPECT_TRUE(aef_plaintext_size(aef_enc_aes_siv, 80) == 64);
    EXPECT_TRUE(aef_plaintext_size(aef_enc_aes_gcm_siv, 92) == 64);
    EXPECT_TRUE(aef_plaintext_size(aef_enc_aes_siv, 10) == 0);
    EXPECT_TRUE(aef_plaintext_size(2, 100) == 0);
}

TEST(avtp_crypto_pdu, aef_buffer_too_small)
{
    auto [chain, key] = make_siv256_keychain(test_kid);
    (void)key;

    // Encrypt buffer 1 byte too small
    std::array<uint8_t, 64 + aef_siv_overhead - 1> small_buf{};
    auto r = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, small_buf, {}, nullptr);
    EXPECT_TRUE(!r.has_value());

    // Decrypt buffer too small
    std::array<uint8_t, 64 + aef_siv_overhead> enc_buf{};
    auto enc = aef_encrypt(aef_enc_aes_siv, chain, test_kid, test_plaintext, enc_buf, {}, nullptr);
    EXPECT_TRUE(enc.has_value());

    std::array<uint8_t, 63> small_dec{};
    auto r2 = aef_decrypt(aef_enc_aes_siv, chain, test_kid, *enc, small_dec);
    EXPECT_TRUE(!r2.has_value());
}

// ===========================================================================
// AEF size helper tests
// ===========================================================================

TEST(avtp_crypto_pdu, aef_encrypted_size_siv)
{
    EXPECT_EQ(aef_encrypted_size(aef_enc_aes_siv, 100), aef_siv_overhead + 100);
    EXPECT_EQ(aef_encrypted_size(aef_enc_aes_siv, 0), aef_siv_overhead);
}

TEST(avtp_crypto_pdu, aef_encrypted_size_gcm_siv)
{
    EXPECT_EQ(aef_encrypted_size(aef_enc_aes_gcm_siv, 100), aef_gcm_siv_overhead + 100);
    EXPECT_EQ(aef_encrypted_size(aef_enc_aes_gcm_siv, 0), aef_gcm_siv_overhead);
}

TEST(avtp_crypto_pdu, aef_encrypted_size_invalid_enc)
{
    EXPECT_EQ(aef_encrypted_size(99, 100), static_cast<size_t>(0));
}

TEST(avtp_crypto_pdu, aef_plaintext_size_siv)
{
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_siv, aef_siv_overhead + 100), static_cast<size_t>(100));
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_siv, aef_siv_overhead), static_cast<size_t>(0));
}

TEST(avtp_crypto_pdu, aef_plaintext_size_gcm_siv)
{
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_gcm_siv, aef_gcm_siv_overhead + 100), static_cast<size_t>(100));
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_gcm_siv, aef_gcm_siv_overhead), static_cast<size_t>(0));
}

TEST(avtp_crypto_pdu, aef_plaintext_size_too_small)
{
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_siv, aef_siv_overhead - 1), static_cast<size_t>(0));
    EXPECT_EQ(aef_plaintext_size(aef_enc_aes_gcm_siv, aef_gcm_siv_overhead - 1), static_cast<size_t>(0));
}

TEST(avtp_crypto_pdu, aef_plaintext_size_invalid_enc)
{
    EXPECT_EQ(aef_plaintext_size(99, 200), static_cast<size_t>(0));
}

// ===========================================================================
// Wrapped key wire-format serialization roundtrips
// ===========================================================================

TEST(avtp_crypto_pdu, aes128_wrapped_key_roundtrip)
{
    Aes128WrappedKey wk{};
    for (size_t i = 0; i < wk.ciphertext.size(); ++i) {
        wk.ciphertext[i] = static_cast<uint8_t>(i + 1);
    }
    for (size_t i = 0; i < wk.siv_tag.size(); ++i) {
        wk.siv_tag[i] = static_cast<uint8_t>(0xA0 + i);
    }

    std::array<uint8_t, aes128_wrapped_key_size> wire{};
    serialize_aes128_wrapped_key(wk, wire);

    auto decoded = deserialize_aes128_wrapped_key(wire);
    EXPECT_TRUE(span_compare(decoded.ciphertext, wk.ciphertext));
    EXPECT_TRUE(span_compare(decoded.siv_tag, wk.siv_tag));
}

TEST(avtp_crypto_pdu, aes256_wrapped_key_roundtrip)
{
    Aes256WrappedKey wk{};
    for (size_t i = 0; i < wk.ciphertext.size(); ++i) {
        wk.ciphertext[i] = static_cast<uint8_t>(i + 0x10);
    }
    for (size_t i = 0; i < wk.siv_tag.size(); ++i) {
        wk.siv_tag[i] = static_cast<uint8_t>(0xB0 + i);
    }

    std::array<uint8_t, aes256_wrapped_key_size> wire{};
    serialize_aes256_wrapped_key(wk, wire);

    auto decoded = deserialize_aes256_wrapped_key(wire);
    EXPECT_TRUE(span_compare(decoded.ciphertext, wk.ciphertext));
    EXPECT_TRUE(span_compare(decoded.siv_tag, wk.siv_tag));
}

TEST(avtp_crypto_pdu, aes128_siv_wrapped_key_roundtrip)
{
    Aes128SivWrappedKey wk{};
    for (size_t i = 0; i < wk.ciphertext.size(); ++i) {
        wk.ciphertext[i] = static_cast<uint8_t>(i + 0x20);
    }
    for (size_t i = 0; i < wk.siv_tag.size(); ++i) {
        wk.siv_tag[i] = static_cast<uint8_t>(0xC0 + i);
    }

    std::array<uint8_t, aes128_siv_wrapped_key_size> wire{};
    serialize_aes128_siv_wrapped_key(wk, wire);

    auto decoded = deserialize_aes128_siv_wrapped_key(wire);
    EXPECT_TRUE(span_compare(decoded.ciphertext, wk.ciphertext));
    EXPECT_TRUE(span_compare(decoded.siv_tag, wk.siv_tag));
}

TEST(avtp_crypto_pdu, aes256_siv_wrapped_key_roundtrip)
{
    Aes256SivWrappedKey wk{};
    for (size_t i = 0; i < wk.ciphertext.size(); ++i) {
        wk.ciphertext[i] = static_cast<uint8_t>(i + 0x30);
    }
    for (size_t i = 0; i < wk.siv_tag.size(); ++i) {
        wk.siv_tag[i] = static_cast<uint8_t>(0xD0 + i);
    }

    std::array<uint8_t, aes256_siv_wrapped_key_size> wire{};
    serialize_aes256_siv_wrapped_key(wk, wire);

    auto decoded = deserialize_aes256_siv_wrapped_key(wire);
    EXPECT_TRUE(span_compare(decoded.ciphertext, wk.ciphertext));
    EXPECT_TRUE(span_compare(decoded.siv_tag, wk.siv_tag));
}

// ===========================================================================

TEST_MAIN(statusbar_avtp_crypto, avtp_crypto_pdu_test)
