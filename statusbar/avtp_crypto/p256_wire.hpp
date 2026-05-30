// Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
// SPDX-License-Identifier: MIT

// IEEE 1722.1-2021 ECC_PUBLIC_256 and ECC_PRIVATE_256 wire format types.
//
// Provides self-describing wire format structs with full P-256 curve domain
// parameters (q, a, b, r, G) per IEEE 1722.1-2021 Tables 7-182 and 7-183.
// ECC_PUBLIC_256 includes an ECDSA chain-of-trust signature.

#pragma once

#include "statusbar/avtp_crypto/avtp_keychain.hpp"
#include "statusbar/crypto/keys.hpp"
#include "statusbar/crypto/p256/p256.hpp"
#include "statusbar/crypto/util/secure_array.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace statusbar::crypto::avtp {

// IEEE 1722.1 ECC_PUBLIC_256 wire format (Table 7-182, 336 bytes)

/// @brief Wire size of ECC_PUBLIC_256 per IEEE 1722.1-2021 Table 7-182.
inline constexpr size_t ecc_public_256_wire_size = 336;

/// @brief Size of the signed data in ECC_PUBLIC_256 (bytes 0..271 = 272 bytes).
inline constexpr size_t ecc_public_256_signed_data_size = 272;

/// @brief IEEE 1722.1 ECC_PUBLIC_256 wire format (336 bytes).
///
/// Self-describing format with full curve domain parameters (q, a, b, r, G)
/// plus the public key point (W.x, W.y) and an ECDSA chain-of-trust signature.
/// The ECDSA signature covers bytes [0..271] using ECSP-DSA/EMSA1/SHA-256.
struct EccPublic256Wire
{
    KeyId related_key_id{};                                            ///< [0..7]     Paired private key EUI-64
    std::array<uint8_t, p256_field_element_size> field_size{};         ///< [8..39]    q (field prime)
    std::array<uint8_t, p256_field_element_size> semimajor{};          ///< [40..71]   a (curve coefficient)
    std::array<uint8_t, p256_field_element_size> semiminor{};          ///< [72..103]  b (curve coefficient)
    std::array<uint8_t, p256_field_element_size> prime_divisor{};      ///< [104..135] r (group order)
    std::array<uint8_t, p256_field_element_size> generator_x{};        ///< [136..167] G.x
    std::array<uint8_t, p256_field_element_size> generator_y{};        ///< [168..199] G.y
    std::array<uint8_t, p256_field_element_size> public_x{};           ///< [200..231] W.x (public key)
    std::array<uint8_t, p256_field_element_size> public_y{};           ///< [232..263] W.y (public key)
    KeyId signature_key_id{};                                          ///< [264..271] Signing key EUI-64
    std::array<uint8_t, p256_field_element_size> ecdsa_signature_c{};  ///< [272..303] ECDSA signature r
    std::array<uint8_t, p256_field_element_size> ecdsa_signature_d{};  ///< [304..335] ECDSA signature s
};

/// @brief Build an ECC_PUBLIC_256 wire struct with P-256 curve params, public key, and ECDSA signature.
/// @param related_key_id EUI-64 of the paired private key.
/// @param public_key The P-256 public key to embed.
/// @param signature_key_id EUI-64 of the signing authority key.
/// @param signing_key The ECDSA signing key (authority's private key).
/// @return Fully populated EccPublic256Wire with ECDSA signature over bytes [0..271].
auto build_ecc_public_256_wire(
    KeyId const& related_key_id, P256PublicKey const& public_key, KeyId const& signature_key_id, P256PrivateKey const& signing_key)
    -> EccPublic256Wire;

/// @brief Verify the ECDSA signature on an ECC_PUBLIC_256 wire struct.
/// @param wire The wire struct whose signature is to be verified.
/// @param signer_public_key The public key of the signing authority.
/// @return true if the ECDSA signature over bytes [0..271] is valid.
auto verify_ecc_public_256_wire(EccPublic256Wire const& wire, P256PublicKey const& signer_public_key) -> bool;

/// @brief Extract a P256PublicKey from an ECC_PUBLIC_256 wire struct.
///
/// Validates that the wire struct's curve domain parameters (q, a, b, r, G) match
/// the canonical P-256 values. Returns false if the parameters indicate a different curve,
/// which could be a curve-substitution attack.
///
/// @param wire The wire struct to extract the public key from.
/// @return The P256PublicKey, or std::nullopt if curve parameters don't match P-256.
auto extract_p256_public_key(EccPublic256Wire const& wire) -> std::optional<P256PublicKey>;

/// @brief Serialize an EccPublic256Wire to a 336-byte buffer.
/// @param wire The wire struct to serialize.
/// @param out 336-byte output buffer.
auto serialize_ecc_public_256_wire(EccPublic256Wire const& wire, std::span<uint8_t, ecc_public_256_wire_size> out) -> void;

/// @brief Deserialize an EccPublic256Wire from a 336-byte buffer.
/// @param in 336-byte input buffer.
/// @return The deserialized EccPublic256Wire.
auto deserialize_ecc_public_256_wire(std::span<uint8_t const, ecc_public_256_wire_size> in) -> EccPublic256Wire;

// IEEE 1722.1 ECC_PRIVATE_256 wire format (Table 7-183, 232 bytes)

/// @brief Wire size of ECC_PRIVATE_256 per IEEE 1722.1-2021 Table 7-183.
inline constexpr size_t ecc_private_256_wire_size = 232;

/// @brief IEEE 1722.1 ECC_PRIVATE_256 wire format (232 bytes).
///
/// Self-describing format with full curve domain parameters (q, a, b, r, G)
/// plus the private scalar s.
struct EccPrivate256Wire
{
    KeyId related_key_id{};                                         ///< [0..7]     Paired public key EUI-64
    std::array<uint8_t, p256_field_element_size> field_size{};      ///< [8..39]    q
    std::array<uint8_t, p256_field_element_size> semimajor{};       ///< [40..71]   a
    std::array<uint8_t, p256_field_element_size> semiminor{};       ///< [72..103]  b
    std::array<uint8_t, p256_field_element_size> prime_divisor{};   ///< [104..135] r
    std::array<uint8_t, p256_field_element_size> generator_x{};     ///< [136..167] G.x
    std::array<uint8_t, p256_field_element_size> generator_y{};     ///< [168..199] G.y
    std::array<uint8_t, p256_field_element_size> private_scalar{};  ///< [200..231] s (private key)

    /// Securely zero private key material on destruction.
    ~EccPrivate256Wire() { internal::secure_zero(*this); }
};

/// @brief Build an ECC_PRIVATE_256 wire struct with P-256 curve params and private scalar.
/// @param related_key_id EUI-64 of the paired public key.
/// @param private_key The P-256 private key scalar to embed.
/// @return Fully populated EccPrivate256Wire with curve domain parameters.
auto build_ecc_private_256_wire(KeyId const& related_key_id, P256PrivateKey const& private_key) -> EccPrivate256Wire;

/// @brief Extract a P256PrivateKey from an ECC_PRIVATE_256 wire struct.
///
/// Validates that the wire struct's curve domain parameters match canonical P-256 values
/// and that the private scalar is in [1, n-1].
///
/// @param wire The wire struct to extract the private key from.
/// @return The P256PrivateKey, or std::nullopt if curve params don't match or scalar is invalid.
auto extract_p256_private_key(EccPrivate256Wire const& wire) -> std::optional<P256PrivateKey>;

/// @brief Serialize an EccPrivate256Wire to a 232-byte buffer.
/// @param wire The wire struct to serialize.
/// @param out 232-byte output buffer.
auto serialize_ecc_private_256_wire(EccPrivate256Wire const& wire, std::span<uint8_t, ecc_private_256_wire_size> out) -> void;

/// @brief Deserialize an EccPrivate256Wire from a 232-byte buffer.
/// @param in 232-byte input buffer.
/// @return The deserialized EccPrivate256Wire.
auto deserialize_ecc_private_256_wire(std::span<uint8_t const, ecc_private_256_wire_size> in) -> EccPrivate256Wire;

}  // namespace statusbar::crypto::avtp
