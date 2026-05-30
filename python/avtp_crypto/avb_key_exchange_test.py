#!/usr/bin/env python3
# Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com>
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "cryptography>=43.0",
#     "pycryptodome>=3.20",
# ]
# ///
"""
Integration tests for AVB controller-to-endpoint key distribution.

Simulates the AUTH_GET_NONCE / AUTH_ADD_KEY_NONCE protocol flow between
an AVB controller and three target entities (talker, listener1, listener2),
using X25519 ECIES for encrypting the key distribution command.

The ECIES encrypt (controller) and decrypt (entity) operations can be backed
by either the Python library or the C++ CLI, enabling cross-language validation.
When a CLI path is provided, all 4 backend combinations are tested:
  python-ctrl + python-entity, python-ctrl + cpp-entity,
  cpp-ctrl + python-entity, cpp-ctrl + cpp-entity.

Optionally, one entity can use a YubiKey for hardware-backed X25519 ECDH,
validating that the protocol works with keys that never leave the hardware.

Usage:
    uv run avb_key_exchange_test.py -v                          # Python only
    uv run avb_key_exchange_test.py -v --build-dir build-Debug  # All 4 combos
    uv run avb_key_exchange_test.py -v --cli path/to/avtp_crypto_tool

    # With YubiKey hardware entity:
    uv run avb_key_exchange_test.py -v \\
        --yubikey-entity talker \\
        --yubikey-x25519-pubkey ~/yubikey_public.gpg-raw-public.txt \\
        --yubikey-x25519-keygrip 5D7B4D837571E7FCF2DBB6EE228AFF1FA1D8175A
"""

import argparse
import enum
import os
import socket
import subprocess
import sys
import unittest
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "crypto"))
import statusbar_crypto as sc


# ---------------------------------------------------------------------------
# Backend selection
# ---------------------------------------------------------------------------


class Backend(enum.Enum):
    PYTHON = "python"
    CPP = "cpp"


# Callable types for pluggable ECIES operations
EciesEncryptFn = Callable[[sc.X25519PublicKey, bytes, bytes], bytes]
EciesDecryptFn = Callable[[sc.X25519PrivateKey, bytes], bytes]


# ---------------------------------------------------------------------------
# CLI helper (matches avtp_crypto_cross_check.py pattern)
# ---------------------------------------------------------------------------


def run_cli(cli_path: Path, *args: str) -> str:
    """Run avtp_crypto_tool and return stdout, or raise on failure."""
    result = subprocess.run(
        [str(cli_path)] + list(args),
        capture_output=True,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"CLI failed: {' '.join(args)}\n  stderr: {result.stderr.strip()}"
        )
    return result.stdout.strip()


# ---------------------------------------------------------------------------
# ECIES backend factories
# ---------------------------------------------------------------------------


def python_ecies_encrypt(
    pk: sc.X25519PublicKey, plaintext: bytes, entropy: bytes
) -> bytes:
    return sc.x25519_ecies_encrypt(pk, plaintext, entropy)


def python_ecies_decrypt(sk: sc.X25519PrivateKey, ciphertext: bytes) -> bytes:
    return sc.x25519_ecies_decrypt(sk, ciphertext)


def make_cpp_ecies_encrypt(cli_path: Path) -> EciesEncryptFn:
    def cpp_encrypt(pk: sc.X25519PublicKey, plaintext: bytes, entropy: bytes) -> bytes:
        hex_out = run_cli(
            cli_path,
            "x25519_ecies_encrypt",
            pk.data.hex(),
            entropy.hex(),
            plaintext.hex(),
        )
        return bytes.fromhex(hex_out)

    return cpp_encrypt


def make_cpp_ecies_decrypt(cli_path: Path, x25519_seed: bytes) -> EciesDecryptFn:
    """Create a C++ ECIES decrypt function bound to a specific entity's X25519 seed."""

    def cpp_decrypt(_sk: sc.X25519PrivateKey, ciphertext: bytes) -> bytes:
        hex_out = run_cli(
            cli_path,
            "x25519_ecies_decrypt",
            x25519_seed.hex(),
            ciphertext.hex(),
        )
        parts = hex_out.split(" ", 1)
        if parts[0] != "ok":
            raise ValueError("x25519_ecies_decrypt failed")
        return bytes.fromhex(parts[1]) if len(parts) > 1 else b""

    return cpp_decrypt


# ---------------------------------------------------------------------------
# YubiKey ECIES backend
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class YubikeyConfig:
    """Configuration for a YubiKey-backed entity."""

    entity_name: str  # which entity uses the YubiKey (e.g. "talker")
    x25519_public_key: bytes  # 32-byte X25519 public key from hex file
    x25519_keygrip: str  # GPG keygrip for cv25519 slot
    pin: str  # card PIN (from --yubikey-pin or YUBIKEY_PIN env var)


def _assuan_decode_bytes(raw: bytes) -> bytes:
    """Decode Assuan percent-encoded D-line payload (raw bytes) to raw bytes.

    In the Assuan protocol over a socket, D-line data is mostly raw bytes,
    with only %, CR, and LF percent-encoded.
    """
    i = 0
    parts = []
    while i < len(raw):
        if raw[i : i + 1] == b"%" and i + 2 < len(raw):
            parts.append(bytes.fromhex(raw[i + 1 : i + 3].decode("ascii")))
            i += 3
        else:
            parts.append(raw[i : i + 1])
            i += 1
    return b"".join(parts)


def _parse_shared_secret(d_data: bytes) -> bytes:
    """Extract the 32-byte shared secret from scdaemon D-line data.

    The response format depends on the GnuPG version and transport:
    - S-expression: (5:value32:<32 bytes>)
    - Raw with 0x40 prefix: 0x40 + <32 bytes> (X25519 point format marker)
    - Raw 32 bytes
    """
    if d_data.startswith(b"(") and b"value" in d_data[:20]:
        idx = d_data.index(b"value")
        rest = d_data[idx + 5 :]
        colon_idx = rest.index(b":")
        data_len = int(rest[:colon_idx])
        return rest[colon_idx + 1 : colon_idx + 1 + data_len]
    # Strip 0x40 prefix (X25519 Montgomery point format marker)
    if len(d_data) == 33 and d_data[0:1] == b"\x40":
        return d_data[1:]
    return d_data


def _get_gpg_agent_socket() -> str:
    """Get the gpg-agent socket path via gpgconf."""
    result = subprocess.run(
        ["gpgconf", "--list-dirs", "agent-socket"],
        capture_output=True,
        text=True,
        timeout=5,
    )
    if result.returncode != 0:
        raise RuntimeError(f"gpgconf failed: {result.stderr.strip()}")
    return result.stdout.strip()


def yubikey_x25519_ecdh(keygrip: str, peer_pk: bytes, pin: str) -> bytes:
    """Perform X25519 ECDH on the YubiKey via direct gpg-agent socket connection.

    Connects directly to the gpg-agent Unix socket to avoid gpg-connect-agent's
    output buffering in non-TTY mode. Uses loopback pinentry to provide the
    card PIN programmatically.

    Requires 'allow-loopback-pinentry' in ~/.gnupg/gpg-agent.conf.

    peer_pk: 32-byte Montgomery u-coordinate (the ephemeral public key V).
    Returns: 32-byte shared secret Z.
    """
    hex_data = "40" + peer_pk.hex().upper()
    agent_socket = _get_gpg_agent_socket()

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(10.0)
    sock.connect(agent_socket)
    # Wrap socket for buffered byte I/O
    sf = sock.makefile("rwb", buffering=0)

    def recv_line() -> bytes:
        """Read one LF-terminated line from the socket, return without LF."""
        buf = bytearray()
        while True:
            ch = sf.read(1)
            if not ch or ch == b"\n":
                return bytes(buf)
            buf.extend(ch)

    def send_line(cmd: str) -> None:
        """Send an Assuan command line (adds LF)."""
        sf.write((cmd + "\n").encode("ascii"))

    def read_response() -> list[bytes]:
        """Read Assuan lines until OK or ERR, handling INQUIRE for PIN."""
        lines: list[bytes] = []
        while True:
            line = recv_line()
            if line.startswith(b"INQUIRE"):
                send_line(f"D {pin}")
                send_line("END")
                continue
            lines.append(line)
            if line.startswith(b"OK") or line.startswith(b"ERR"):
                return lines

    try:
        # Read the initial OK greeting
        read_response()

        # Enable loopback pinentry so we can provide the PIN programmatically
        send_line("OPTION pinentry-mode=loopback")
        read_response()
        # Some agents may not support this option — that's OK if PIN is cached

        # Set the ephemeral public key data
        send_line(f"SCD SETDATA {hex_data}")
        resp = read_response()
        for line in resp:
            if line.startswith(b"ERR"):
                raise RuntimeError(
                    f"YubiKey SCD SETDATA failed: {line.decode(errors='replace')}"
                )

        # Perform PKDECRYPT — this does X25519 ECDH on-card
        send_line(f"SCD PKDECRYPT {keygrip}")
        resp = read_response()
        for line in resp:
            if line.startswith(b"ERR"):
                raise RuntimeError(
                    f"YubiKey SCD PKDECRYPT failed: {line.decode(errors='replace')}"
                )

        # Extract D-line data
        d_data = b""
        for line in resp:
            if line.startswith(b"D "):
                d_data += _assuan_decode_bytes(line[2:])

        if not d_data:
            raise RuntimeError(
                f"YubiKey X25519 ECDH: no data in response.\n"
                f"Response lines: {[l.decode(errors='replace') for l in resp]}"
            )

        shared_secret = _parse_shared_secret(d_data)

        if len(shared_secret) != 32:
            raise RuntimeError(
                f"YubiKey X25519 ECDH: expected 32-byte shared secret, "
                f"got {len(shared_secret)} bytes"
            )

        return shared_secret

    finally:
        try:
            send_line("BYE")
        except OSError:
            pass
        sf.close()
        sock.close()


def make_yubikey_ecies_decrypt(yubikey_config: YubikeyConfig) -> EciesDecryptFn:
    """Create an ECIES decrypt function that uses the YubiKey for X25519 ECDH."""

    def yubikey_decrypt(_sk: sc.X25519PrivateKey, ciphertext: bytes) -> bytes:
        def ecdh_fn(peer_pk: bytes) -> bytes:
            return yubikey_x25519_ecdh(
                yubikey_config.x25519_keygrip, peer_pk, yubikey_config.pin
            )

        return sc.x25519_ecies_decrypt_with_ecdh(ecdh_fn, ciphertext)

    return yubikey_decrypt


# ---------------------------------------------------------------------------
# Deterministic seed and KeyId helpers
# ---------------------------------------------------------------------------


def make_seed(entity_name: str, key_type: str) -> bytes:
    """Generate a deterministic 32-byte seed from entity name and key type."""
    return sc.sha256(f"avb_test:{entity_name}:{key_type}".encode())


def make_key_id(entity_index: int, key_type_index: int) -> sc.KeyId:
    """Create a structured KeyId: 0x00 entity_index key_type_index 0x00 0x00 0x00 0x00 0x01."""
    return sc.KeyId(
        data=bytes([0x00, entity_index, key_type_index, 0x00, 0x00, 0x00, 0x00, 0x01])
    )


# Key type indices within an entity
_KT_ED25519 = 0x01
_KT_X25519 = 0x02
_KT_P256 = 0x03
_KT_SESSION = 0x10


# ---------------------------------------------------------------------------
# Session key type configuration
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class SessionKeyConfig:
    """Configuration for a session key type used in key distribution."""

    key_type: sc.KeyType
    key_size: int
    key_class: type
    entry_class: type


SESSION_KEY_CONFIGS = [
    SessionKeyConfig(
        sc.KeyType.aes128_siv,
        sc.aes128_siv_key_size,
        sc.Aes128SivKey,
        sc.Aes128SivKeyEntry,
    ),
    SessionKeyConfig(
        sc.KeyType.aes256_siv,
        sc.aes256_siv_key_size,
        sc.Aes256SivKey,
        sc.Aes256SivKeyEntry,
    ),
    SessionKeyConfig(
        sc.KeyType.aes128, sc.aes128_key_size, sc.Aes128Key, sc.Aes128KeyEntry
    ),
    SessionKeyConfig(
        sc.KeyType.aes256, sc.aes256_key_size, sc.Aes256Key, sc.Aes256KeyEntry
    ),
]

# Lookup table for Entity to reconstruct session keys by type
_KEY_TYPE_TO_CLASSES: dict[sc.KeyType, tuple[type, type]] = {
    cfg.key_type: (cfg.key_class, cfg.entry_class) for cfg in SESSION_KEY_CONFIGS
}


# ---------------------------------------------------------------------------
# AvbEntity: holds an entity's identity keys and keychains
# ---------------------------------------------------------------------------


@dataclass
class AvbEntity:
    """An AVB entity with identity keys and keychains."""

    name: str
    ed25519_sk: sc.Ed25519PrivateKey
    x25519_sk: sc.X25519PrivateKey
    x25519_seed: bytes  # original 32-byte seed (needed by C++ CLI)
    p256_sk: sc.P256PrivateKey
    ed25519_key_id: sc.KeyId
    x25519_key_id: sc.KeyId
    p256_key_id: sc.KeyId
    keychains: sc.KeyChains


def create_entity(name: str, entity_index: int) -> AvbEntity:
    """Create an entity with 3 keypairs and populated entity_private + entity_public keychains."""
    ed25519_sk = sc.ed25519_keypair_from_seed(make_seed(name, "ed25519"))
    x25519_seed = make_seed(name, "x25519")
    x25519_sk = sc.x25519_keypair_from_seed(x25519_seed)
    p256_sk = sc.p256_ecdsa_keypair_from_seed(make_seed(name, "p256"))

    ed_kid = make_key_id(entity_index, _KT_ED25519)
    x_kid = make_key_id(entity_index, _KT_X25519)
    p_kid = make_key_id(entity_index, _KT_P256)

    keychains = sc.KeyChains()

    # entity_private: all 3 private keys
    keychains.entity_private.append(
        sc.Ed25519PrivateKeyEntry(key_id=ed_kid, private_key=ed25519_sk)
    )
    keychains.entity_private.append(
        sc.X25519PrivateKeyEntry(key_id=x_kid, private_key=x25519_sk)
    )
    keychains.entity_private.append(
        sc.P256PrivateKeyEntry(key_id=p_kid, private_key=p256_sk)
    )

    # entity_public: self-signed public keys
    keychains.entity_public.append(
        sc.build_ed25519_signed_public_key_entry(
            key_id=ed_kid,
            related_key_id=ed_kid,
            public_key=sc.ed25519_public_key(ed25519_sk),
            signature_key_id=ed_kid,
            signing_key=ed25519_sk,
        )
    )
    keychains.entity_public.append(
        sc.build_x25519_signed_public_key_entry(
            key_id=x_kid,
            related_key_id=ed_kid,
            public_key=x25519_sk.public_key,
            signature_key_id=ed_kid,
            signing_key=ed25519_sk,
        )
    )
    keychains.entity_public.append(
        sc.build_p256_signed_public_key_entry(
            key_id=p_kid,
            related_key_id=p_kid,
            public_key=sc.p256_public_key(p256_sk),
            signature_key_id=p_kid,
            signing_key=p256_sk,
        )
    )

    return AvbEntity(
        name=name,
        ed25519_sk=ed25519_sk,
        x25519_sk=x25519_sk,
        x25519_seed=x25519_seed,
        p256_sk=p256_sk,
        ed25519_key_id=ed_kid,
        x25519_key_id=x_kid,
        p256_key_id=p_kid,
        keychains=keychains,
    )


def create_hardware_entity(
    name: str, entity_index: int, yubikey_config: YubikeyConfig
) -> AvbEntity:
    """Create an entity with a YubiKey-backed X25519 key.

    Ed25519 and P-256 are still software-generated (same deterministic seeds as
    create_entity). The X25519 public key comes from the YubiKey's exported hex
    file. The X25519 private key data is a dummy (never used -- the EciesDecryptFn
    closure bypasses it via yubikey_x25519_ecdh).
    """
    ed25519_sk = sc.ed25519_keypair_from_seed(make_seed(name, "ed25519"))
    p256_sk = sc.p256_ecdsa_keypair_from_seed(make_seed(name, "p256"))

    # X25519: real public key from YubiKey, dummy private key
    x25519_pk = sc.X25519PublicKey(data=yubikey_config.x25519_public_key)
    dummy_seed = bytes(32)
    dummy_x25519_sk = sc.x25519_keypair_from_seed(dummy_seed)
    x25519_sk = sc.X25519PrivateKey(data=dummy_x25519_sk.data, public_key=x25519_pk)

    ed_kid = make_key_id(entity_index, _KT_ED25519)
    x_kid = make_key_id(entity_index, _KT_X25519)
    p_kid = make_key_id(entity_index, _KT_P256)

    keychains = sc.KeyChains()

    # entity_private: all 3 keys (X25519 private is dummy, never used)
    keychains.entity_private.append(
        sc.Ed25519PrivateKeyEntry(key_id=ed_kid, private_key=ed25519_sk)
    )
    keychains.entity_private.append(
        sc.X25519PrivateKeyEntry(key_id=x_kid, private_key=x25519_sk)
    )
    keychains.entity_private.append(
        sc.P256PrivateKeyEntry(key_id=p_kid, private_key=p256_sk)
    )

    # entity_public: signed with Ed25519 key (X25519 uses the real hardware public key)
    keychains.entity_public.append(
        sc.build_ed25519_signed_public_key_entry(
            key_id=ed_kid,
            related_key_id=ed_kid,
            public_key=sc.ed25519_public_key(ed25519_sk),
            signature_key_id=ed_kid,
            signing_key=ed25519_sk,
        )
    )
    keychains.entity_public.append(
        sc.build_x25519_signed_public_key_entry(
            key_id=x_kid,
            related_key_id=ed_kid,
            public_key=x25519_pk,
            signature_key_id=ed_kid,
            signing_key=ed25519_sk,
        )
    )
    keychains.entity_public.append(
        sc.build_p256_signed_public_key_entry(
            key_id=p_kid,
            related_key_id=p_kid,
            public_key=sc.p256_public_key(p256_sk),
            signature_key_id=p_kid,
            signing_key=p256_sk,
        )
    )

    return AvbEntity(
        name=name,
        ed25519_sk=ed25519_sk,
        x25519_sk=x25519_sk,
        x25519_seed=dummy_seed,
        p256_sk=p256_sk,
        ed25519_key_id=ed_kid,
        x25519_key_id=x_kid,
        p256_key_id=p_kid,
        keychains=keychains,
    )


def install_controller_keys(controller: AvbEntity, target: AvbEntity) -> None:
    """Add controller's public keys to target's 'controllers' keychain."""
    target.keychains.controllers.append(
        sc.build_ed25519_signed_public_key_entry(
            key_id=controller.ed25519_key_id,
            related_key_id=controller.ed25519_key_id,
            public_key=sc.ed25519_public_key(controller.ed25519_sk),
            signature_key_id=controller.ed25519_key_id,
            signing_key=controller.ed25519_sk,
        )
    )
    target.keychains.controllers.append(
        sc.build_x25519_signed_public_key_entry(
            key_id=controller.x25519_key_id,
            related_key_id=controller.ed25519_key_id,
            public_key=controller.x25519_sk.public_key,
            signature_key_id=controller.ed25519_key_id,
            signing_key=controller.ed25519_sk,
        )
    )
    target.keychains.controllers.append(
        sc.build_p256_signed_public_key_entry(
            key_id=controller.p256_key_id,
            related_key_id=controller.p256_key_id,
            public_key=sc.p256_public_key(controller.p256_sk),
            signature_key_id=controller.p256_key_id,
            signing_key=controller.p256_sk,
        )
    )


# ---------------------------------------------------------------------------
# Protocol classes with pluggable ECIES backend
# ---------------------------------------------------------------------------


class Controller:
    """Controller side of the AUTH key distribution protocol."""

    def __init__(self, entity: AvbEntity, ecies_encrypt_fn: EciesEncryptFn):
        self.entity = entity
        self.ecies_encrypt_fn = ecies_encrypt_fn

    def create_session_key(self, config: SessionKeyConfig) -> tuple[sc.KeyId, object]:
        """Generate a deterministic session key of the given type and add to transport keychain."""
        session_key_id = make_key_id(0x01, _KT_SESSION)
        key_data = sc.sha256(b"avb_test:session_key:" + config.key_type.name.encode())
        if config.key_size > 32:
            key_data = key_data + sc.sha256(
                b"avb_test:session_key_ext:" + config.key_type.name.encode()
            )
        key_data = key_data[: config.key_size]
        session_key = config.key_class(data=key_data)
        self.entity.keychains.transport.append(
            config.entry_class(key_id=session_key_id, key=session_key)
        )
        return session_key_id, session_key

    def build_auth_get_nonce(self, nonce_entropy: bytes) -> tuple[bytes, sc.Nonce]:
        """Build serialized AUTH_GET_NONCE command. Returns (wire_bytes, controller_nonce)."""
        controller_nonce = sc.Nonce(data=nonce_entropy[:8])
        payload = sc.AuthGetNoncePayload(controller_nonce=controller_nonce)
        return sc.serialize_auth_get_nonce(payload), controller_nonce

    def process_auth_get_nonce_response(
        self, response_bytes: bytes, expected_nonce: sc.Nonce
    ) -> sc.AuthGetNonceResponsePayload:
        """Parse AUTH_GET_NONCE response and verify controller_nonce echo."""
        response = sc.deserialize_auth_get_nonce_response(response_bytes)
        if response.controller_nonce.data != expected_nonce.data:
            raise ValueError("controller_nonce mismatch in AUTH_GET_NONCE response")
        return response

    def build_auth_add_key_nonce(
        self,
        target_x25519_pk: sc.X25519PublicKey,
        controller_nonce: sc.Nonce,
        target_nonce: sc.Nonce,
        session_key_id: sc.KeyId,
        session_key: object,
        ecies_entropy: bytes,
        session_key_config: SessionKeyConfig,
    ) -> bytes:
        """Build and X25519-ECIES-encrypt an AUTH_ADD_KEY_NONCE command."""
        header = sc.AuthAddKeyNonceHeader(
            controller_nonce=controller_nonce,
            target_nonce=target_nonce,
            key_id=session_key_id,
            key_type=session_key_config.key_type,
            key_length=session_key_config.key_size,
        )
        plaintext = sc.serialize_auth_add_key_nonce_header(header) + session_key.data
        return self.ecies_encrypt_fn(target_x25519_pk, plaintext, ecies_entropy)


class Entity:
    """Entity (talker/listener) side of the AUTH key distribution protocol."""

    def __init__(self, entity: AvbEntity, ecies_decrypt_fn: EciesDecryptFn):
        self.entity = entity
        self.ecies_decrypt_fn = ecies_decrypt_fn
        self._target_nonce: Optional[sc.Nonce] = None

    def process_auth_get_nonce(
        self, command_bytes: bytes, nonce_entropy: bytes
    ) -> bytes:
        """Process AUTH_GET_NONCE command, generate target nonce, return serialized response."""
        cmd = sc.deserialize_auth_get_nonce(command_bytes)
        self._target_nonce = sc.Nonce(data=nonce_entropy[:8])
        response = sc.AuthGetNonceResponsePayload(
            controller_nonce=cmd.controller_nonce,
            target_nonce=self._target_nonce,
        )
        return sc.serialize_auth_get_nonce_response(response)

    def process_auth_add_key_nonce(
        self, encrypted_command: bytes
    ) -> sc.AuthAddKeyNonceResponsePayload:
        """Decrypt AUTH_ADD_KEY_NONCE, verify nonces, install session key, return response."""
        # Find X25519 private key
        x25519_entry = None
        for entry in self.entity.keychains.entity_private:
            if isinstance(entry, sc.X25519PrivateKeyEntry):
                x25519_entry = entry
                break
        if x25519_entry is None:
            raise ValueError("no X25519 private key in entity_private")

        # Decrypt via pluggable backend
        plaintext = self.ecies_decrypt_fn(x25519_entry.private_key, encrypted_command)

        # Parse header
        header = sc.deserialize_auth_add_key_nonce_header(
            plaintext[: sc.auth_add_key_nonce_header_size]
        )
        if header is None:
            raise ValueError("failed to parse AUTH_ADD_KEY_NONCE header")

        # Verify target nonce
        if self._target_nonce is None:
            raise ValueError("no target nonce set (AUTH_GET_NONCE not processed)")
        if header.target_nonce.data != self._target_nonce.data:
            raise ValueError("target_nonce mismatch in AUTH_ADD_KEY_NONCE")

        # Extract session key
        key_data = plaintext[sc.auth_add_key_nonce_header_size :]
        if len(key_data) != header.key_length:
            raise ValueError(
                f"key data length {len(key_data)} != header.key_length {header.key_length}"
            )
        if header.key_type not in _KEY_TYPE_TO_CLASSES:
            raise ValueError(f"unsupported key_type {header.key_type}")

        key_cls, entry_cls = _KEY_TYPE_TO_CLASSES[header.key_type]
        session_key = key_cls(data=key_data)
        self.entity.keychains.transport.append(
            entry_cls(key_id=header.key_id, key=session_key)
        )

        return sc.AuthAddKeyNonceResponsePayload(
            controller_nonce=header.controller_nonce,
            target_nonce=header.target_nonce,
            key_id=header.key_id,
        )


# ---------------------------------------------------------------------------
# Protocol orchestration
# ---------------------------------------------------------------------------


def run_key_distribution(
    controller: AvbEntity,
    ctrl_protocol: Controller,
    target: AvbEntity,
    target_protocol: Entity,
    session_key_id: sc.KeyId,
    session_key: object,
    nonce_entropy: bytes,
    ecies_entropy: bytes,
    session_key_config: SessionKeyConfig,
) -> sc.AuthAddKeyNonceResponsePayload:
    """Execute the complete AUTH_GET_NONCE -> AUTH_ADD_KEY_NONCE protocol."""
    # Step 1: Controller sends AUTH_GET_NONCE
    cmd_bytes, controller_nonce = ctrl_protocol.build_auth_get_nonce(nonce_entropy)

    # Step 2: Entity responds with AUTH_GET_NONCE_RESPONSE
    target_nonce_entropy = sc.sha256(nonce_entropy + target.name.encode())
    response_bytes = target_protocol.process_auth_get_nonce(
        cmd_bytes, target_nonce_entropy
    )

    # Step 3: Controller processes response
    response = ctrl_protocol.process_auth_get_nonce_response(
        response_bytes, controller_nonce
    )

    # Step 4: Controller builds encrypted AUTH_ADD_KEY_NONCE
    encrypted_cmd = ctrl_protocol.build_auth_add_key_nonce(
        target.x25519_sk.public_key,
        response.controller_nonce,
        response.target_nonce,
        session_key_id,
        session_key,
        ecies_entropy,
        session_key_config,
    )

    # Step 5: Entity decrypts, installs key, responds
    return target_protocol.process_auth_add_key_nonce(encrypted_cmd)


# ---------------------------------------------------------------------------
# Test cases: setup/keychain tests (always pure Python, run once)
# ---------------------------------------------------------------------------


class TestAvbSetup(unittest.TestCase):
    """Pure Python tests for entity keychain setup and signature verification."""

    def setUp(self):
        self.controller = create_entity("controller", 0x01)
        self.talker = create_entity("talker", 0x02)
        self.listener1 = create_entity("listener1", 0x03)
        self.listener2 = create_entity("listener2", 0x04)
        self.targets = [self.talker, self.listener1, self.listener2]
        for target in self.targets:
            install_controller_keys(self.controller, target)

    def test_entity_keychains_populated(self):
        """Verify all entities have correct keychain sizes after setup."""
        for entity in [self.controller] + self.targets:
            self.assertEqual(len(entity.keychains.entity_private), 3)
            self.assertEqual(len(entity.keychains.entity_public), 3)
        for target in self.targets:
            self.assertEqual(len(target.keychains.controllers), 3)
        self.assertEqual(len(self.controller.keychains.controllers), 0)

    def test_self_signed_public_keys_verify(self):
        """Verify all self-signed public key entries pass signature verification."""
        for entity in [self.controller] + self.targets:
            ed_entry = entity.keychains.entity_public[0]
            self.assertTrue(
                sc.verify_ed25519_signed_public_key_entry(
                    ed_entry, sc.ed25519_public_key(entity.ed25519_sk)
                )
            )
            x_entry = entity.keychains.entity_public[1]
            self.assertTrue(
                sc.verify_x25519_signed_public_key_entry(
                    x_entry, sc.ed25519_public_key(entity.ed25519_sk)
                )
            )
            p_entry = entity.keychains.entity_public[2]
            self.assertTrue(
                sc.verify_p256_signed_public_key_entry(
                    p_entry, sc.p256_public_key(entity.p256_sk)
                )
            )

    def test_controller_keys_in_targets_verify(self):
        """Verify controller's public keys in each target's controllers keychain."""
        ctrl_ed_pk = sc.ed25519_public_key(self.controller.ed25519_sk)
        ctrl_p256_pk = sc.p256_public_key(self.controller.p256_sk)
        for target in self.targets:
            self.assertTrue(
                sc.verify_ed25519_signed_public_key_entry(
                    target.keychains.controllers[0], ctrl_ed_pk
                )
            )
            self.assertTrue(
                sc.verify_x25519_signed_public_key_entry(
                    target.keychains.controllers[1], ctrl_ed_pk
                )
            )
            self.assertTrue(
                sc.verify_p256_signed_public_key_entry(
                    target.keychains.controllers[2], ctrl_p256_pk
                )
            )

    def test_keychains_find_methods(self):
        """Verify KeyChains.find_* methods return correct entries."""
        entity = self.talker
        # find_private_key_entry
        ed_priv = entity.keychains.find_private_key_entry(entity.ed25519_key_id)
        self.assertIsNotNone(ed_priv)
        self.assertIsInstance(ed_priv, sc.Ed25519PrivateKeyEntry)
        x_priv = entity.keychains.find_private_key_entry(entity.x25519_key_id)
        self.assertIsNotNone(x_priv)
        self.assertIsInstance(x_priv, sc.X25519PrivateKeyEntry)
        p_priv = entity.keychains.find_private_key_entry(entity.p256_key_id)
        self.assertIsNotNone(p_priv)
        self.assertIsInstance(p_priv, sc.P256PrivateKeyEntry)

        # find_public_key_entry (searches entity_public)
        ed_pub = entity.keychains.find_public_key_entry(entity.ed25519_key_id)
        self.assertIsNotNone(ed_pub)
        self.assertIsInstance(ed_pub, sc.Ed25519SignedPublicKeyEntry)

        # find_public_key_entry (searches controllers for controller's key)
        ctrl_pub = entity.keychains.find_public_key_entry(
            self.controller.ed25519_key_id
        )
        self.assertIsNotNone(ctrl_pub)
        self.assertIsInstance(ctrl_pub, sc.Ed25519SignedPublicKeyEntry)

        # find_session_key_entry returns None when transport is empty
        self.assertIsNone(
            entity.keychains.find_session_key_entry(make_key_id(0x01, _KT_SESSION))
        )

        # Nonexistent key returns None
        bogus_id = sc.KeyId(data=bytes(8))
        self.assertIsNone(entity.keychains.find_private_key_entry(bogus_id))
        self.assertIsNone(entity.keychains.find_public_key_entry(bogus_id))

    def test_auth_serialization_roundtrip(self):
        """AuthAddKeyNonceHeader roundtrips through serialize/deserialize."""
        nonce1 = sc.Nonce(data=bytes(range(1, 9)))
        nonce2 = sc.Nonce(data=bytes(range(9, 17)))
        kid = make_key_id(0x01, _KT_SESSION)
        header = sc.AuthAddKeyNonceHeader(
            controller_nonce=nonce1,
            target_nonce=nonce2,
            key_id=kid,
            key_type=sc.KeyType.aes256_siv,
            key_length=sc.aes256_siv_key_size,
        )
        wire = sc.serialize_auth_add_key_nonce_header(header)
        self.assertEqual(len(wire), sc.auth_add_key_nonce_header_size)
        parsed = sc.deserialize_auth_add_key_nonce_header(wire)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed.controller_nonce.data, nonce1.data)
        self.assertEqual(parsed.target_nonce.data, nonce2.data)
        self.assertEqual(parsed.key_id.data, kid.data)
        self.assertEqual(parsed.key_type, sc.KeyType.aes256_siv)
        self.assertEqual(parsed.key_length, sc.aes256_siv_key_size)


# ---------------------------------------------------------------------------
# Test cases: protocol tests (parameterized by controller/entity backend)
# ---------------------------------------------------------------------------


class TestAvbKeyExchange(unittest.TestCase):
    """Protocol tests parameterized by controller_backend, entity_backend, and session key type.

    Subclasses override class attributes via make_test_class().
    The default is Python for both backends, AES-256-SIV for session key.
    """

    controller_backend: Backend = Backend.PYTHON
    entity_backend: Backend = Backend.PYTHON
    session_key_config: SessionKeyConfig = SESSION_KEY_CONFIGS[1]  # aes256_siv
    cli_path: Optional[Path] = None
    yubikey_config: Optional[YubikeyConfig] = None

    def _make_encrypt_fn(self) -> EciesEncryptFn:
        if self.controller_backend == Backend.CPP:
            return make_cpp_ecies_encrypt(self.cli_path)
        return python_ecies_encrypt

    def _make_decrypt_fn(self, entity: AvbEntity) -> EciesDecryptFn:
        if (
            self.yubikey_config is not None
            and entity.name == self.yubikey_config.entity_name
        ):
            return make_yubikey_ecies_decrypt(self.yubikey_config)
        if self.entity_backend == Backend.CPP:
            return make_cpp_ecies_decrypt(self.cli_path, entity.x25519_seed)
        return python_ecies_decrypt

    def _create_entity(self, name: str, index: int) -> AvbEntity:
        if self.yubikey_config is not None and name == self.yubikey_config.entity_name:
            return create_hardware_entity(name, index, self.yubikey_config)
        return create_entity(name, index)

    def setUp(self):
        self.controller = create_entity("controller", 0x01)  # always software
        self.talker = self._create_entity("talker", 0x02)
        self.listener1 = self._create_entity("listener1", 0x03)
        self.listener2 = self._create_entity("listener2", 0x04)
        self.targets = [self.talker, self.listener1, self.listener2]
        for target in self.targets:
            install_controller_keys(self.controller, target)

    def test_full_key_distribution(self):
        """Run complete AUTH flow for all 3 targets, verify response key_id."""
        encrypt_fn = self._make_encrypt_fn()
        ctrl_protocol = Controller(self.controller, encrypt_fn)
        session_key_id, session_key = ctrl_protocol.create_session_key(
            self.session_key_config
        )

        for target in self.targets:
            decrypt_fn = self._make_decrypt_fn(target)
            target_protocol = Entity(target, decrypt_fn)
            nonce_entropy = sc.sha256(f"avb_test:nonce:{target.name}".encode())
            ecies_entropy = sc.sha256(f"avb_test:ecies_entropy:{target.name}".encode())
            response = run_key_distribution(
                self.controller,
                ctrl_protocol,
                target,
                target_protocol,
                session_key_id,
                session_key,
                nonce_entropy,
                ecies_entropy,
                self.session_key_config,
            )
            self.assertEqual(response.key_id.data, session_key_id.data)

    def test_all_entities_have_same_session_key(self):
        """After distribution, all entities have the identical session key."""
        encrypt_fn = self._make_encrypt_fn()
        ctrl_protocol = Controller(self.controller, encrypt_fn)
        session_key_id, session_key = ctrl_protocol.create_session_key(
            self.session_key_config
        )

        for target in self.targets:
            decrypt_fn = self._make_decrypt_fn(target)
            target_protocol = Entity(target, decrypt_fn)
            nonce_entropy = sc.sha256(f"avb_test:nonce:{target.name}".encode())
            ecies_entropy = sc.sha256(f"avb_test:ecies_entropy:{target.name}".encode())
            run_key_distribution(
                self.controller,
                ctrl_protocol,
                target,
                target_protocol,
                session_key_id,
                session_key,
                nonce_entropy,
                ecies_entropy,
                self.session_key_config,
            )

        # Controller has the key
        ctrl_entry = self.controller.keychains.find_session_key_entry(session_key_id)
        self.assertIsNotNone(ctrl_entry)
        self.assertEqual(ctrl_entry.key.data, session_key.data)

        # All targets have the same key
        for target in self.targets:
            entry = target.keychains.find_session_key_entry(session_key_id)
            self.assertIsNotNone(entry, f"{target.name} missing session key")
            self.assertIsInstance(entry, self.session_key_config.entry_class)
            self.assertEqual(
                entry.key.data,
                session_key.data,
                f"{target.name} has different session key",
            )

    def test_nonce_replay_protection(self):
        """AUTH_ADD_KEY_NONCE with wrong target_nonce is rejected."""
        encrypt_fn = self._make_encrypt_fn()
        ctrl_protocol = Controller(self.controller, encrypt_fn)
        session_key_id, session_key = ctrl_protocol.create_session_key(
            self.session_key_config
        )

        target = self.talker
        nonce_entropy = sc.sha256(f"avb_test:nonce:{target.name}".encode())
        ecies_entropy = sc.sha256(f"avb_test:ecies_entropy:{target.name}".encode())

        # Normal nonce exchange
        cmd_bytes, controller_nonce = ctrl_protocol.build_auth_get_nonce(nonce_entropy)
        decrypt_fn = self._make_decrypt_fn(target)
        target_protocol = Entity(target, decrypt_fn)
        target_nonce_entropy = sc.sha256(nonce_entropy + target.name.encode())
        response_bytes = target_protocol.process_auth_get_nonce(
            cmd_bytes, target_nonce_entropy
        )
        ctrl_protocol.process_auth_get_nonce_response(response_bytes, controller_nonce)

        # Build AUTH_ADD_KEY_NONCE with WRONG target nonce
        wrong_target_nonce = sc.Nonce(data=bytes(8))
        header = sc.AuthAddKeyNonceHeader(
            controller_nonce=controller_nonce,
            target_nonce=wrong_target_nonce,
            key_id=session_key_id,
            key_type=self.session_key_config.key_type,
            key_length=self.session_key_config.key_size,
        )
        plaintext = sc.serialize_auth_add_key_nonce_header(header) + session_key.data
        encrypted = encrypt_fn(target.x25519_sk.public_key, plaintext, ecies_entropy)

        with self.assertRaises(ValueError):
            target_protocol.process_auth_add_key_nonce(encrypted)

    def test_ecies_to_wrong_entity_fails(self):
        """Message encrypted for listener1 cannot be decrypted by listener2."""
        encrypt_fn = self._make_encrypt_fn()
        ctrl_protocol = Controller(self.controller, encrypt_fn)
        session_key_id, session_key = ctrl_protocol.create_session_key(
            self.session_key_config
        )

        nonce_entropy = sc.sha256(b"avb_test:nonce:listener1")
        ecies_entropy = sc.sha256(b"avb_test:ecies_entropy:listener1")

        # Run nonce exchange with listener1
        l1_decrypt_fn = self._make_decrypt_fn(self.listener1)
        l1_protocol = Entity(self.listener1, l1_decrypt_fn)
        cmd_bytes, controller_nonce = ctrl_protocol.build_auth_get_nonce(nonce_entropy)
        l1_nonce_entropy = sc.sha256(nonce_entropy + b"listener1")
        response_bytes = l1_protocol.process_auth_get_nonce(cmd_bytes, l1_nonce_entropy)
        response = ctrl_protocol.process_auth_get_nonce_response(
            response_bytes, controller_nonce
        )

        # Encrypt for listener1
        encrypted = ctrl_protocol.build_auth_add_key_nonce(
            self.listener1.x25519_sk.public_key,
            response.controller_nonce,
            response.target_nonce,
            session_key_id,
            session_key,
            ecies_entropy,
            self.session_key_config,
        )

        # Listener2 tries to decrypt — should fail (HMAC mismatch)
        l2_decrypt_fn = self._make_decrypt_fn(self.listener2)
        l2_protocol = Entity(self.listener2, l2_decrypt_fn)
        l2_protocol._target_nonce = response.target_nonce
        with self.assertRaises(ValueError):
            l2_protocol.process_auth_add_key_nonce(encrypted)


# ---------------------------------------------------------------------------
# Backend combination runner
# ---------------------------------------------------------------------------


def make_test_class(
    ctrl_backend: Backend,
    entity_backend: Backend,
    session_key_config: SessionKeyConfig,
    cli_path: Optional[Path],
    yubikey_config: Optional[YubikeyConfig] = None,
) -> type:
    """Create a TestAvbKeyExchange subclass configured for the given backends and key type."""
    name = (
        f"TestAvbKeyExchange_ctrl_{ctrl_backend.value}"
        f"_entity_{entity_backend.value}"
        f"_key_{session_key_config.key_type.name}"
    )
    if yubikey_config is not None:
        name += f"_yubikey_{yubikey_config.entity_name}"

    cls = type(
        name,
        (TestAvbKeyExchange,),
        {
            "controller_backend": ctrl_backend,
            "entity_backend": entity_backend,
            "session_key_config": session_key_config,
            "cli_path": cli_path,
            "yubikey_config": yubikey_config,
        },
    )
    return cls


def find_cli(args) -> Optional[Path]:
    """Locate avtp_crypto_tool binary from CLI arguments."""
    if args.cli:
        p = Path(args.cli)
        if p.is_file():
            return p
        print(f"WARNING: --cli path not found: {p}", file=sys.stderr)
        return None

    if args.build_dir is None:
        return None

    # Resolve build_dir: if relative, resolve relative to repo root
    # (matches avtp_crypto_cross_check.py convention)
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        script_dir = Path(__file__).resolve().parent
        repo_root = script_dir.parent.parent.parent.parent
        build_dir = repo_root / build_dir

    # Try two possible locations (standalone vs top-level build)
    candidates = [
        build_dir / "statusbar" / "avtp_crypto" / "statusbar-avtp-crypto-tool",
        build_dir / "statusbar" / "avtp_crypto" / "avtp_crypto_tool",  # legacy
        build_dir
        / "external"
        / "statusbar_crypto"
        / "statusbar"
        / "avtp_crypto"
        / "statusbar-avtp-crypto-tool",
    ]
    for c in candidates:
        if c.is_file():
            return c

    print(
        f"WARNING: avtp_crypto_tool not found in {build_dir}",
        file=sys.stderr,
    )
    return None


def _load_yubikey_config(args) -> Optional[YubikeyConfig]:
    """Build YubikeyConfig from CLI arguments, or None if not specified."""
    if args.yubikey_entity is None:
        return None
    if args.yubikey_x25519_pubkey is None or args.yubikey_x25519_keygrip is None:
        print(
            "ERROR: --yubikey-entity requires --yubikey-x25519-pubkey and "
            "--yubikey-x25519-keygrip",
            file=sys.stderr,
        )
        sys.exit(1)
    pubkey_hex = args.yubikey_x25519_pubkey.read_text().strip()
    x25519_pk = bytes.fromhex(pubkey_hex)
    if len(x25519_pk) != 32:
        print(
            f"ERROR: X25519 public key must be 32 bytes, got {len(x25519_pk)}",
            file=sys.stderr,
        )
        sys.exit(1)
    valid_entities = {"talker", "listener1", "listener2"}
    if args.yubikey_entity not in valid_entities:
        print(
            f"ERROR: --yubikey-entity must be one of {valid_entities}",
            file=sys.stderr,
        )
        sys.exit(1)
    return YubikeyConfig(
        entity_name=args.yubikey_entity,
        x25519_public_key=x25519_pk,
        x25519_keygrip=args.yubikey_x25519_keygrip,
        pin=args.yubikey_pin,
    )


def _add_tests(
    suite: unittest.TestSuite,
    loader: unittest.TestLoader,
    test_cls: type,
    test_names: list[str],
) -> None:
    """Add tests from a test class to the suite, optionally filtered by name."""
    if test_names:
        for test_name in test_names:
            try:
                suite.addTests(loader.loadTestsFromName(test_name, test_cls))
            except AttributeError:
                pass
    else:
        suite.addTests(loader.loadTestsFromTestCase(test_cls))


def main():
    parser = argparse.ArgumentParser(description="AVB key exchange integration tests")
    parser.add_argument(
        "--cli", type=Path, default=None, help="Path to avtp_crypto_tool binary"
    )
    parser.add_argument(
        "--build-dir", type=Path, default=None, help="Build directory to search for CLI"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Verbose test output"
    )
    parser.add_argument(
        "--yubikey-entity",
        type=str,
        default=None,
        help="Entity name to use YubiKey for (talker, listener1, or listener2)",
    )
    parser.add_argument(
        "--yubikey-x25519-pubkey",
        type=Path,
        default=None,
        help="Path to hex file with 32-byte X25519 public key",
    )
    parser.add_argument(
        "--yubikey-x25519-keygrip",
        type=str,
        default=None,
        help="GPG keygrip for the X25519 encryption slot",
    )
    parser.add_argument(
        "--yubikey-pin",
        type=str,
        default=os.environ.get("YUBIKEY_PIN", "123456"),
        help="YubiKey card PIN (default: $YUBIKEY_PIN or factory 123456)",
    )
    parser.add_argument("tests", nargs="*", help="Specific test names to run")
    args = parser.parse_args()

    cli_path = find_cli(args)
    yubikey_config = _load_yubikey_config(args)
    verbosity = 2 if args.verbose else 1

    # Always run pure Python setup tests
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(TestAvbSetup))

    # Build list of backend combinations
    backends = [Backend.PYTHON]
    if cli_path is not None:
        backends.append(Backend.CPP)

    for ctrl_backend in backends:
        for entity_backend in backends:
            for sk_config in SESSION_KEY_CONFIGS:
                # Standard software-only test
                test_cls = make_test_class(
                    ctrl_backend, entity_backend, sk_config, cli_path
                )
                _add_tests(suite, loader, test_cls, args.tests)

                # YubiKey test (if configured)
                if yubikey_config is not None:
                    test_cls = make_test_class(
                        ctrl_backend,
                        entity_backend,
                        sk_config,
                        cli_path,
                        yubikey_config,
                    )
                    _add_tests(suite, loader, test_cls, args.tests)

    runner = unittest.TextTestRunner(verbosity=verbosity)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
