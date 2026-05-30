// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// AVTP wire-format PDU serialization/deserialization and AEF encrypt/decrypt.
//
// Provides:
//   1. AEF encrypt/decrypt (IEEE 1722-2016 Clause 13, enc=0 AES-SIV, enc=1 AES-GCM-SIV)
//   2. Key exchange PDU serialization (Ed25519 + X25519, P-256 ECDSA + ECDH)
//   3. AUTH_GET_NONCE command/response (IEEE 1722.1-2021 §7.4.103)
//   4. AUTH_ADD_KEY_NONCE command/response (IEEE 1722.1-2021 §7.4.104)
//   5. Per-key-type wrapped key wire formats
//
// Cryptographic operations (build, verify, derive, wrap/unwrap) are in avtp_crypto.hpp.

#pragma once

#include "statusbar/avtp/avtp_aef.hpp"
#include "statusbar/avtp_crypto/avtp_crypto.hpp"
#include "statusbar/avtp_crypto/avtp_keychain.hpp"
#include "statusbar/crypto/aes_gcm_siv/aes_gcm_siv.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace statusbar::crypto::avtp {

// AUTH_GET_NONCE payloads (IEEE 1722.1-2021 §7.4.103)

/// AUTH_GET_NONCE command payload (fields after AECP common header).
struct AuthGetNoncePayload
{
    Nonce controller_nonce;  // 8 bytes, random from controller
};

inline constexpr size_t auth_get_nonce_payload_size = nonce_size;  // 8

/// AUTH_GET_NONCE response payload.
struct AuthGetNonceResponsePayload
{
    Nonce controller_nonce;  // 8 bytes, echoed from command
    Nonce target_nonce;      // 8 bytes, random from entity
};

inline constexpr size_t auth_get_nonce_response_payload_size = 2 * nonce_size;  // 16

// AUTH_ADD_KEY_NONCE payloads (IEEE 1722.1-2021 §7.4.104)

/// AUTH_ADD_KEY_NONCE command payload header (fixed part, before variable key data).
///
/// Wire layout (28 bytes):
///   [0..7]   controller_nonce  (8 bytes)
///   [8..15]  target_nonce      (8 bytes)
///   [16..23] key_id            (8 bytes, EUI-64)
///   [24]     key_type          (4 bits) | key_length high (4 bits)
///   [25]     key_length low    (8 bits)
///   [26..27] reserved          (16 bits, zero)
struct AuthAddKeyNonceHeader
{
    Nonce controller_nonce;
    Nonce target_nonce;
    KeyId key_id;
    KeyType key_type{};
    uint16_t key_length{};
};

inline constexpr size_t auth_add_key_nonce_header_size = (2 * nonce_size) + key_id_size + 4;  // 28

/// AUTH_ADD_KEY_NONCE response payload (no key data echoed).
struct AuthAddKeyNonceResponsePayload
{
    Nonce controller_nonce;
    Nonce target_nonce;
    KeyId key_id;
};

inline constexpr size_t auth_add_key_nonce_response_payload_size = (2 * nonce_size) + key_id_size;  // 24

// Ed25519 wire-format serialization

/// Serialize an Ed25519KeyExchangePdu to a 129-byte wire buffer.
auto serialize_ed25519_key_exchange(Ed25519KeyExchangePdu const& pdu, std::span<uint8_t, ed25519_key_exchange_pdu_wire_size> out)
    -> void;

/// Deserialize an Ed25519KeyExchangePdu from a 129-byte wire buffer.
auto deserialize_ed25519_key_exchange(std::span<uint8_t const, ed25519_key_exchange_pdu_wire_size> in)
    -> std::optional<Ed25519KeyExchangePdu>;

// P-256 wire-format serialization

/// Serialize a P256KeyExchangePdu to a 193-byte wire buffer.
auto serialize_p256_key_exchange(P256KeyExchangePdu const& pdu, std::span<uint8_t, p256_key_exchange_pdu_wire_size> out) -> void;

/// Deserialize a P256KeyExchangePdu from a 193-byte wire buffer.
auto deserialize_p256_key_exchange(std::span<uint8_t const, p256_key_exchange_pdu_wire_size> in)
    -> std::optional<P256KeyExchangePdu>;

// AUTH_GET_NONCE wire-format serialization

/// Serialize an AuthGetNoncePayload to an 8-byte wire buffer.
auto serialize_auth_get_nonce(AuthGetNoncePayload const& payload, std::span<uint8_t, auth_get_nonce_payload_size> out) -> void;

/// Deserialize an AuthGetNoncePayload from an 8-byte wire buffer.
auto deserialize_auth_get_nonce(std::span<uint8_t const, auth_get_nonce_payload_size> in) -> AuthGetNoncePayload;

/// Serialize an AuthGetNonceResponsePayload to a 16-byte wire buffer.
auto serialize_auth_get_nonce_response(
    AuthGetNonceResponsePayload const& payload, std::span<uint8_t, auth_get_nonce_response_payload_size> out) -> void;

/// Deserialize an AuthGetNonceResponsePayload from a 16-byte wire buffer.
auto deserialize_auth_get_nonce_response(std::span<uint8_t const, auth_get_nonce_response_payload_size> in)
    -> AuthGetNonceResponsePayload;

// AUTH_ADD_KEY_NONCE wire-format serialization

/// Serialize an AuthAddKeyNonceHeader to a 28-byte wire buffer.
auto serialize_auth_add_key_nonce_header(
    AuthAddKeyNonceHeader const& header, std::span<uint8_t, auth_add_key_nonce_header_size> out) -> void;

/// Deserialize an AuthAddKeyNonceHeader from a 28-byte wire buffer.
auto deserialize_auth_add_key_nonce_header(std::span<uint8_t const, auth_add_key_nonce_header_size> in)
    -> std::optional<AuthAddKeyNonceHeader>;

/// Serialize an AuthAddKeyNonceResponsePayload to a 24-byte wire buffer.
auto serialize_auth_add_key_nonce_response(
    AuthAddKeyNonceResponsePayload const& payload, std::span<uint8_t, auth_add_key_nonce_response_payload_size> out) -> void;

/// Deserialize an AuthAddKeyNonceResponsePayload from a 24-byte wire buffer.
auto deserialize_auth_add_key_nonce_response(std::span<uint8_t const, auth_add_key_nonce_response_payload_size> in)
    -> AuthAddKeyNonceResponsePayload;

// Wrapped key wire-format serialization

auto serialize_aes128_wrapped_key(Aes128WrappedKey const& wk, std::span<uint8_t, aes128_wrapped_key_size> out) -> void;
auto deserialize_aes128_wrapped_key(std::span<uint8_t const, aes128_wrapped_key_size> in) -> Aes128WrappedKey;

auto serialize_aes256_wrapped_key(Aes256WrappedKey const& wk, std::span<uint8_t, aes256_wrapped_key_size> out) -> void;
auto deserialize_aes256_wrapped_key(std::span<uint8_t const, aes256_wrapped_key_size> in) -> Aes256WrappedKey;

auto serialize_aes128_siv_wrapped_key(Aes128SivWrappedKey const& wk, std::span<uint8_t, aes128_siv_wrapped_key_size> out) -> void;
auto deserialize_aes128_siv_wrapped_key(std::span<uint8_t const, aes128_siv_wrapped_key_size> in) -> Aes128SivWrappedKey;

auto serialize_aes256_siv_wrapped_key(Aes256SivWrappedKey const& wk, std::span<uint8_t, aes256_siv_wrapped_key_size> out) -> void;
auto deserialize_aes256_siv_wrapped_key(std::span<uint8_t const, aes256_siv_wrapped_key_size> in) -> Aes256SivWrappedKey;

// AEF encryption mode constants (IEEE 1722-2016 Clause 13 enc field)
// Values sourced from statusbar::avtp::AefEncMode enum.

/// AEF encryption mode: AES-SIV (RFC 5297). No nonce required.
/// Wire format: encrypted_payload = siv_tag(16) || ciphertext.
inline constexpr uint8_t aef_enc_aes_siv = static_cast<uint8_t>(statusbar::avtp::AefEncMode::aes_siv);

/// AEF encryption mode: AES-GCM-SIV (RFC 8452). 12-byte nonce required.
/// Wire format: encrypted_payload = nonce(12) || ciphertext || tag(16).
inline constexpr uint8_t aef_enc_aes_gcm_siv = static_cast<uint8_t>(statusbar::avtp::AefEncMode::aes_gcm_siv);

/// Overhead bytes for AES-SIV encrypted payload: 16 (SIV tag).
inline constexpr size_t aef_siv_overhead = 16;

/// Overhead bytes for AES-GCM-SIV encrypted payload: 12 (nonce) + 16 (tag) = 28.
inline constexpr size_t aef_gcm_siv_overhead = aes_gcm_siv_nonce_size + aes_gcm_siv_tag_size;

/// Nonce counter type for AES-GCM-SIV. 12-byte big-endian counter,
/// randomly initialized by the caller, incremented by aef_encrypt after each use.
using AefNonceCounter = std::array<uint8_t, aes_gcm_siv_nonce_size>;

/// Compute the required output buffer size for aef_encrypt.
/// Returns 0 if enc is invalid.
constexpr auto aef_encrypted_size(uint8_t enc, size_t plaintext_size) -> size_t
{
    if (enc == aef_enc_aes_siv) {
        return aef_siv_overhead + plaintext_size;
    }
    if (enc == aef_enc_aes_gcm_siv) {
        return aef_gcm_siv_overhead + plaintext_size;
    }
    return 0;
}

/// Compute the plaintext size from an encrypted payload for aef_decrypt.
/// Returns 0 if encrypted_payload_size is too small or enc is invalid.
constexpr auto aef_plaintext_size(uint8_t enc, size_t encrypted_payload_size) -> size_t
{
    if (enc == aef_enc_aes_siv && encrypted_payload_size >= aef_siv_overhead) {
        return encrypted_payload_size - aef_siv_overhead;
    }
    if (enc == aef_enc_aes_gcm_siv && encrypted_payload_size >= aef_gcm_siv_overhead) {
        return encrypted_payload_size - aef_gcm_siv_overhead;
    }
    return 0;
}

// AEF encrypt/decrypt (IEEE 1722-2016 Clause 13)

/// @brief Encrypt plaintext into an AEF encrypted_payload.
///
/// Looks up the key by key_id in the keychain, validates the key type matches
/// the enc mode, and dispatches to the appropriate crypto primitive.
///
/// enc=0 (AES-SIV): encrypted_payload = siv_tag(16) || ciphertext.
///   Key must be Aes128SivKeyEntry or Aes256SivKeyEntry.
///   nonce_counter is ignored (may be nullptr).
///
/// enc=1 (AES-GCM-SIV): encrypted_payload = nonce(12) || ciphertext || tag(16).
///   Key must be Aes128KeyEntry or Aes256KeyEntry.
///   nonce_counter must be non-null; the current value is used then incremented.
///
/// @param enc            Encryption mode (0 = AES-SIV, 1 = AES-GCM-SIV).
/// @param keychain       Session keychain to search for the key.
/// @param key_id         Key identifier to look up.
/// @param plaintext      Input plaintext data.
/// @param encrypted_payload_out  Output buffer (must be >= aef_encrypted_size(enc, plaintext.size())).
/// @param aad            Optional additional authenticated data.
/// @param nonce_counter  Pointer to 12-byte nonce counter (required for enc=1, ignored for enc=0).
///                       Incremented (big-endian) after use.
/// @return Span into encrypted_payload_out covering the full encrypted payload, or nullopt on error.
auto aef_encrypt(
    uint8_t enc,
    SessionKeyChain const& keychain,
    KeyId const& key_id,
    std::span<uint8_t const> plaintext,
    std::span<uint8_t> encrypted_payload_out,
    std::span<uint8_t const> aad,
    AefNonceCounter* nonce_counter) -> std::optional<std::span<uint8_t const>>;

/// @brief Decrypt an AEF encrypted_payload to plaintext.
///
/// Looks up the key by key_id in the keychain, extracts metadata (SIV tag or
/// nonce+tag) from the encrypted payload, and dispatches to the appropriate
/// crypto primitive.
///
/// enc=0 (AES-SIV): encrypted_payload = siv_tag(16) || ciphertext.
///   Key must be Aes128SivKeyEntry or Aes256SivKeyEntry.
///
/// enc=1 (AES-GCM-SIV): encrypted_payload = nonce(12) || ciphertext || tag(16).
///   Key must be Aes128KeyEntry or Aes256KeyEntry.
///
/// @param enc            Encryption mode (0 = AES-SIV, 1 = AES-GCM-SIV).
/// @param keychain       Session keychain to search for the key.
/// @param key_id         Key identifier to look up.
/// @param encrypted_payload  Input encrypted payload.
/// @param plaintext_out  Output buffer (must be >= aef_plaintext_size(enc, encrypted_payload.size())).
/// @param aad            Optional additional authenticated data (must match what was used for encrypt).
/// @return Span into plaintext_out covering the decrypted plaintext, or nullopt on auth/key failure.
auto aef_decrypt(
    uint8_t enc,
    SessionKeyChain const& keychain,
    KeyId const& key_id,
    std::span<uint8_t const> encrypted_payload,
    std::span<uint8_t> plaintext_out,
    std::span<uint8_t const> aad = {}) -> std::optional<std::span<uint8_t const>>;

}  // namespace statusbar::crypto::avtp
