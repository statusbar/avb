// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// AVTP cryptographic operations implementation

#include "statusbar/avtp_crypto/avtp_crypto.hpp"

#include "statusbar/crypto/aes/aes_common_internal.hpp"
#include "statusbar/crypto/aes_siv/aes256_siv.hpp"
#include "statusbar/crypto/hkdf/hkdf.hpp"
#include "statusbar/crypto/util/crypto_util_internal.hpp"

namespace statusbar::crypto::avtp {

using internal::span_copy;
using std::span;

// AAD construction helper for key wrapping

/// AAD size for key wrap/unwrap: controller_nonce(8) + target_nonce(8) + key_id(8) + key_type(1) = 25 bytes.
static constexpr size_t wrap_aad_size = (2 * nonce_size) + key_id_size + 1;

/// Build the AAD buffer for AES-256-SIV key wrapping.
static auto build_wrap_aad(Nonce const& controller_nonce, Nonce const& target_nonce, KeyId const& key_id, KeyType key_type)
    -> std::array<uint8_t, wrap_aad_size>
{
    std::array<uint8_t, wrap_aad_size> aad{};
    size_t offset = 0;
    span_copy(span(aad).subspan(offset, nonce_size), controller_nonce.data);
    offset += nonce_size;
    span_copy(span(aad).subspan(offset, nonce_size), target_nonce.data);
    offset += nonce_size;
    span_copy(span(aad).subspan(offset, key_id_size), key_id.data);
    offset += key_id_size;
    aad[offset] = static_cast<uint8_t>(key_type);
    return aad;
}

// Ed25519 key exchange verification

auto verify_ed25519_key_exchange(Ed25519KeyExchangePdu const& pdu, Ed25519PublicKey const& expected_identity)
    -> std::optional<X25519PublicKey>
{
    // Check sender identity matches what we expect
    if (pdu.sender_identity.data != expected_identity.data) {
        return std::nullopt;
    }

    // Reconstruct the signed data: message_type || sender_identity || ephemeral_pubkey
    std::array<uint8_t, 1 + Ed25519PublicKey::LENGTH + X25519PublicKey::LENGTH> signed_data{};
    signed_data[0] = pdu.message_type;
    span_copy(span(signed_data).subspan(1, Ed25519PublicKey::LENGTH), pdu.sender_identity.data);
    span_copy(span(signed_data).subspan(1 + Ed25519PublicKey::LENGTH, X25519PublicKey::LENGTH), pdu.ephemeral_pubkey.data);

    // Verify the Ed25519 signature
    if (!ed25519_verify(pdu.sender_identity, signed_data, pdu.signature)) {
        return std::nullopt;
    }

    return pdu.ephemeral_pubkey;
}

// P-256 key exchange verification

auto verify_p256_key_exchange(P256KeyExchangePdu const& pdu, P256PublicKey const& expected_identity) -> std::optional<P256PublicKey>
{
    // Check sender identity matches what we expect
    if (pdu.sender_identity.data != expected_identity.data) {
        return std::nullopt;
    }

    // Reconstruct the signed data: message_type || sender_identity || ephemeral_pubkey
    std::array<uint8_t, 1 + P256PublicKey::LENGTH + P256PublicKey::LENGTH> signed_data{};
    signed_data[0] = pdu.message_type;
    span_copy(span(signed_data).subspan(1, P256PublicKey::LENGTH), pdu.sender_identity.data);
    span_copy(span(signed_data).subspan(1 + P256PublicKey::LENGTH, P256PublicKey::LENGTH), pdu.ephemeral_pubkey.data);

    // Verify the P-256 ECDSA signature
    if (!p256_ecdsa_verify(pdu.sender_identity, signed_data, pdu.signature)) {
        return std::nullopt;
    }

    return pdu.ephemeral_pubkey;
}

// Ed25519 transport key derivation

auto build_ed25519_transport_key_info(Ed25519PublicKey const& initiator_identity, Ed25519PublicKey const& responder_identity)
    -> std::array<uint8_t, 15 + Ed25519PublicKey::LENGTH + Ed25519PublicKey::LENGTH>
{
    // info = "avtp-transport" (15 bytes, fixed) || initiator_pk (32) || responder_pk (32)
    std::array<uint8_t, 15 + Ed25519PublicKey::LENGTH + Ed25519PublicKey::LENGTH> info{};
    static constexpr uint8_t prefix[] = "avtp-transport";  // 15 bytes
    span_copy(span(info).subspan(0, 15), span<uint8_t const>(prefix, 15));
    span_copy(span(info).subspan(15, Ed25519PublicKey::LENGTH), initiator_identity.data);
    span_copy(span(info).subspan(15 + Ed25519PublicKey::LENGTH, Ed25519PublicKey::LENGTH), responder_identity.data);
    return info;
}

auto derive_ed25519_transport_key(
    span<uint8_t const, X25519PublicKey::LENGTH> shared_secret,
    Ed25519PublicKey const& initiator_identity,
    Ed25519PublicKey const& responder_identity) -> Aes256SivKey
{
    // salt = "avtp-crypto-v1"
    static constexpr uint8_t salt[] = "avtp-crypto-v1";
    auto prk = hkdf_sha256_extract(
        span<uint8_t const>(salt, sizeof(salt) - 1),  // exclude null terminator
        shared_secret);

    // info = "avtp-transport" || initiator_identity || responder_identity
    auto info = build_ed25519_transport_key_info(initiator_identity, responder_identity);

    // Expand to 64 bytes for AES-256-SIV key
    Aes256SivKey transport_key{};
    hkdf_sha256_expand(prk, info, transport_key.data);

    return transport_key;
}

// P-256 transport key derivation

auto build_p256_transport_key_info(P256PublicKey const& initiator_identity, P256PublicKey const& responder_identity)
    -> std::array<uint8_t, 19 + P256PublicKey::LENGTH + P256PublicKey::LENGTH>
{
    // info = "avtp-transport-p256" (19 bytes, fixed) || initiator_pk (64) || responder_pk (64)
    std::array<uint8_t, 19 + P256PublicKey::LENGTH + P256PublicKey::LENGTH> info{};
    static constexpr uint8_t prefix[] = "avtp-transport-p256";  // 19 bytes
    span_copy(span(info).subspan(0, 19), span<uint8_t const>(prefix, 19));
    span_copy(span(info).subspan(19, P256PublicKey::LENGTH), initiator_identity.data);
    span_copy(span(info).subspan(19 + P256PublicKey::LENGTH, P256PublicKey::LENGTH), responder_identity.data);
    return info;
}

auto derive_p256_transport_key(
    span<uint8_t const, P256PrivateKey::LENGTH> shared_secret,
    P256PublicKey const& initiator_identity,
    P256PublicKey const& responder_identity) -> Aes256SivKey
{
    // salt = "avtp-crypto-p256-v1"
    static constexpr uint8_t salt[] = "avtp-crypto-p256-v1";
    auto prk = hkdf_sha256_extract(
        span<uint8_t const>(salt, sizeof(salt) - 1),  // exclude null terminator
        shared_secret);

    // info = "avtp-transport-p256" || initiator_identity || responder_identity
    auto info = build_p256_transport_key_info(initiator_identity, responder_identity);

    // Expand to 64 bytes for AES-256-SIV key
    Aes256SivKey transport_key{};
    hkdf_sha256_expand(prk, info, transport_key.data);

    return transport_key;
}

// Per-key-type key wrapping

auto wrap_aes128_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes128Key const& key) -> Aes128WrappedKey
{
    Aes128WrappedKey wrapped{};
    span_copy(wrapped.ciphertext, key.data);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes128);
    auto siv = aes256_siv_encrypt(transport_key, wrapped.ciphertext, aad);
    span_copy(wrapped.siv_tag, siv);
    return wrapped;
}

auto wrap_aes256_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes256Key const& key) -> Aes256WrappedKey
{
    Aes256WrappedKey wrapped{};
    span_copy(wrapped.ciphertext, key.data);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes256);
    auto siv = aes256_siv_encrypt(transport_key, wrapped.ciphertext, aad);
    span_copy(wrapped.siv_tag, siv);
    return wrapped;
}

auto wrap_aes128_siv_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes128SivKey const& key) -> Aes128SivWrappedKey
{
    Aes128SivWrappedKey wrapped{};
    span_copy(wrapped.ciphertext, key.data);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes128_siv);
    auto siv = aes256_siv_encrypt(transport_key, wrapped.ciphertext, aad);
    span_copy(wrapped.siv_tag, siv);
    return wrapped;
}

auto wrap_aes256_siv_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes256SivKey const& key) -> Aes256SivWrappedKey
{
    Aes256SivWrappedKey wrapped{};
    span_copy(wrapped.ciphertext, key.data);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes256_siv);
    auto siv = aes256_siv_encrypt(transport_key, wrapped.ciphertext, aad);
    span_copy(wrapped.siv_tag, siv);
    return wrapped;
}

// Per-key-type key unwrapping

auto unwrap_aes128_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes128WrappedKey const& wrapped) -> std::optional<Aes128Key>
{
    std::array<uint8_t, Aes128Key::LENGTH> buf{};
    span_copy(buf, wrapped.ciphertext);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes128);
    span<uint8_t const, aes_block_size> const tag{wrapped.siv_tag.data(), aes_block_size};
    if (!aes256_siv_decrypt(transport_key, buf, tag, aad)) {
        return std::nullopt;
    }
    Aes128Key key{};
    span_copy(key.data, buf);
    return key;
}

auto unwrap_aes256_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes256WrappedKey const& wrapped) -> std::optional<Aes256Key>
{
    std::array<uint8_t, Aes256Key::LENGTH> buf{};
    span_copy(buf, wrapped.ciphertext);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes256);
    span<uint8_t const, aes_block_size> const tag{wrapped.siv_tag.data(), aes_block_size};
    if (!aes256_siv_decrypt(transport_key, buf, tag, aad)) {
        return std::nullopt;
    }
    Aes256Key key{};
    span_copy(key.data, buf);
    return key;
}

auto unwrap_aes128_siv_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes128SivWrappedKey const& wrapped) -> std::optional<Aes128SivKey>
{
    std::array<uint8_t, Aes128SivKey::LENGTH> buf{};
    span_copy(buf, wrapped.ciphertext);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes128_siv);
    span<uint8_t const, aes_block_size> const tag{wrapped.siv_tag.data(), aes_block_size};
    if (!aes256_siv_decrypt(transport_key, buf, tag, aad)) {
        return std::nullopt;
    }
    Aes128SivKey key{};
    span_copy(key.data, buf);
    return key;
}

auto unwrap_aes256_siv_key(
    Aes256SivKey const& transport_key,
    Nonce const& controller_nonce,
    Nonce const& target_nonce,
    KeyId const& key_id,
    Aes256SivWrappedKey const& wrapped) -> std::optional<Aes256SivKey>
{
    std::array<uint8_t, Aes256SivKey::LENGTH> buf{};
    span_copy(buf, wrapped.ciphertext);
    auto aad = build_wrap_aad(controller_nonce, target_nonce, key_id, KeyType::aes256_siv);
    span<uint8_t const, aes_block_size> const tag{wrapped.siv_tag.data(), aes_block_size};
    if (!aes256_siv_decrypt(transport_key, buf, tag, aad)) {
        return std::nullopt;
    }
    Aes256SivKey key{};
    span_copy(key.data, buf);
    return key;
}

}  // namespace statusbar::crypto::avtp
