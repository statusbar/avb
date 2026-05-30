<!-- Copyright 2026 Jeff Koftinoff <jeff.koftinoff@statusbar.com> -->

# YubiKey 5 Nano Key Setup for AVB Crypto

Step-by-step instructions for generating and managing cryptographic keys on a YubiKey 5 Nano
connected to a Raspberry Pi 5 (or similar Linux system). This creates:

- **Ed25519** signing + authentication keys (OpenPGP applet)
- **X25519** (cv25519) encryption key for ECIES key distribution (OpenPGP applet)
- **P-256** key for IEEE 1722/1363a ECDSA signing and ECIES (PIV applet)

## Prerequisites

### Required packages

**Debian / Raspberry Pi OS / Ubuntu:**

```bash
sudo apt install yubikey-manager gnupg scdaemon pcscd pcsc-tools pinentry-curses
```

**Fedora / RHEL / CentOS Stream:**

```bash
sudo dnf install yubikey-manager gnupg2 pcsc-lite pcsc-tools pcsc-lite-ccid pinentry-curses
```

On Fedora, `scdaemon` is included in the `gnupg2` package and `pcscd` is provided by `pcsc-lite`.

### GPG agent configuration

Force pinentry to use the terminal (required on headless systems like Raspberry Pi)
and enable loopback pinentry for programmatic PIN entry (required by the AVB test):

```bash
mkdir -p ~/.gnupg
cat >> ~/.gnupg/gpg-agent.conf << 'EOF'
pinentry-program /usr/bin/pinentry-curses
allow-loopback-pinentry
EOF
```

### scdaemon configuration

Force scdaemon to use PC/SC instead of direct CCID access (fixes "No such device" errors):

```bash
echo "disable-ccid" >> ~/.gnupg/scdaemon.conf
```

### Verify the YubiKey is detected

```bash
# Check PC/SC layer sees the device (Ctrl+C to stop)
pcsc_scan

# Check ykman sees the device
ykman info
```

**Important**: Kill `pcsc_scan` before proceeding — it holds an exclusive lock on the reader and
will block GPG and ykman from accessing the card.

## Part 1: OpenPGP Keys (Ed25519 + X25519)

### 1.1 Verify GPG can see the card

```bash
gpgconf --kill gpg-agent
gpg --card-status
```

You should see the card info with `Key attributes: rsa2048 rsa2048 rsa2048` (factory defaults).

### 1.2 Switch key attributes to Curve25519

```bash
gpg --card-edit
```

At the `gpg/card>` prompt:

```bash
admin
key-attr
```

For each of the three slots (Signature, Encryption, Authentication):

1. Select `(2) ECC`
2. Select `(1) Curve 25519 *default*`
3. Enter admin PIN when prompted (default: `12345678`)

This configures:

- Signature → ed25519
- Encryption → cv25519 (X25519)
- Authentication → ed25519

### 1.3 Generate keys on-card

Still in `gpg --card-edit`:

```bash
generate
```

Prompts:

- **Make off-card backup of encryption key?** → `n` (keys exist only on the YubiKey)
- **Key validity** → `0` (no expiration), confirm with `y`
- **Real name** → your name
- **Email** → your email
- **Comment** → e.g. `Yubi1` (to identify this YubiKey)
- Confirm with `O` (Okay)
- Enter admin PIN (`12345678`) and user PIN (`123456`) when prompted

Type `quit` to exit.

### 1.4 Verify the keys

```bash
gpg --card-status
```

Should show ed25519 keys in Signature and Authentication slots, cv25519 in Encryption slot.

### 1.5 Extract the raw X25519 public key

```bash
# Show packet details to find the cv25519 public key
gpg --export your@email.com | gpg --list-packets --verbose 2>&1 | grep -A 5 "cv25519"
```

The `pkey[1]` field contains `40` followed by the 32-byte X25519 public key in hex.
Save just the 32-byte key (without the `40` prefix) for use in AVB key distribution.

### 1.6 List keys with details

```bash
gpg --list-keys --with-colons --with-keygrip your@email.com
```

## Part 2: PIV P-256 Key (IEEE 1722/1363a Compatible)

### 2.1 Generate P-256 key in PIV signature slot (9c)

```bash
ykman piv keys generate -a ECCP256 9c pubkey_p256.pem
```

Press Enter for the default management key (or enter your custom one).

### 2.2 Create a self-signed certificate

PIV requires a certificate in each key slot:

```bash
ykman piv certificates generate -s "CN=Your Name AVB" -a SHA256 9c pubkey_p256.pem
```

Enter the management key (blank for default) and PIN (`123456` default).

### 2.3 View the public key

```bash
openssl ec -pubin -in pubkey_p256.pem -text -noout
```

Shows the uncompressed point (04 || X || Y) with 32-byte X and Y coordinates.

### 2.4 Extract raw uncompressed public key

```bash
openssl ec -pubin -in pubkey_p256.pem -outform der | tail -c 65 | xxd -p > p256-public-key-avb.txt
```

The file contains the 65-byte uncompressed point: `04` + 32-byte X + 32-byte Y.

For IEEE 1363a EC2OSP-X encoding (Clause 17 EECF), use only the 32-byte X coordinate
(bytes 1-32 after the `04` prefix).

## Part 3: Using the Keys

### ECDSA signing with P-256 (IEEE 1722 Clause 16)

The YubiKey performs ECDSA signing on-card via the PIV applet:

```bash
# Sign a SHA-256 digest (32 bytes) with the PIV signature slot
ykman piv keys sign -a SHA256 9c < digest.bin
```

### ECDH for ECIES (IEEE 1722 Clause 17)

The YubiKey performs the raw P-256 scalar multiplication on-card. Your software then
completes the ECIES scheme: KDF2 + SHA-256, AES-CBC-IV0, DHAES MAC.

### X25519 decryption (new AVB key distribution)

The YubiKey performs X25519 ECDH on-card via the OpenPGP applet's scdaemon interface.

**Programmatic use (AVB test):** The test connects directly to the gpg-agent Unix socket
(`gpgconf --list-dirs agent-socket`) and sends Assuan protocol commands (`SCD SETDATA`,
`SCD PKDECRYPT`). This avoids `gpg-connect-agent`, which block-buffers output when stdin
is a pipe, making interactive INQUIRE/PIN exchange impossible.

The PKDECRYPT response returns a 33-byte value: a `0x40` prefix (X25519 Montgomery point
format marker) followed by the 32-byte shared secret.

**Manual testing:**

```bash
# Quick verification that scdaemon can talk to the card
gpg-connect-agent 'SCD SERIALNO' /bye

# Interactive ECDH (works from a terminal, not from scripts)
gpg-connect-agent
> SCD SETDATA 40<peer_public_key_hex>
> SCD PKDECRYPT <keygrip>
> BYE
```

Or use `gpg --decrypt` for OpenPGP-formatted ciphertext.

## Resetting Keys (Factory Reset)

### Reset OpenPGP applet (erases ALL OpenPGP keys)

```bash
ykman openpgp reset
```

This erases all three OpenPGP key slots (signature, encryption, authentication),
resets PINs to defaults (PIN: `123456`, Admin PIN: `12345678`), and restores
key attributes to rsa2048. The attestation key is not affected.

**There is no way to delete individual OpenPGP keys** — you must reset the entire applet.

### Reset PIV applet (erases ALL PIV keys)

```bash
ykman piv reset
```

This erases all PIV key slots and certificates, resets PIN (`123456`),
PUK (`12345678`), and management key to factory defaults.

### Delete a single PIV key slot (firmware 5.7+)

```bash
# Delete key from slot 9c only
ykman piv keys delete 9c

# Delete certificate from slot 9c only (key remains)
ykman piv certificates delete 9c
```

Requires the management key and PIN. Other PIV slots are unaffected.

### Full experiment reset (start from scratch)

To completely erase both applets and start over:

```bash
ykman openpgp reset --force
ykman piv reset --force
gpgconf --kill gpg-agent
rm -rf ~/.gnupg/private-keys-v1.d/*
```

The `rm` command removes GPG's local key stubs that reference the old on-card keys.
You may also want to delete the associated GPG public keys:

```bash
gpg --delete-keys your@email.com
```

## Default Credentials Reference

| Credential | Default Value |
|---|---|
| OpenPGP User PIN | `123456` |
| OpenPGP Admin PIN | `12345678` |
| PIV PIN | `123456` |
| PIV PUK | `12345678` |
| PIV Management Key | `010203040506070801020304050607080102030405060708` |

**Important**: Change all PINs before production use. The YubiKey 5 Nano has no touch sensor,
so PIN entry is the only user-presence verification.

## Key Summary

| Applet | Slot | Algorithm | AVB Use |
|---|---|---|---|
| OpenPGP | Signature | Ed25519 | Signing control messages |
| OpenPGP | Encryption | X25519 (cv25519) | ECIES key distribution (new scheme) |
| OpenPGP | Authentication | Ed25519 | Device authentication |
| PIV | 9c (Signature) | NIST P-256 | IEEE 1722 Clause 16 ECDSA + Clause 17 ECIES |

## Troubleshooting

| Problem | Fix |
|---|---|
| `gpg: No such device` | Add `disable-ccid` to `~/.gnupg/scdaemon.conf`, then `gpgconf --kill gpg-agent` |
| PIN prompt not visible | Add `pinentry-program /usr/bin/pinentry-curses` to `~/.gnupg/gpg-agent.conf` |
| `No SmartCard daemon` | `sudo apt install scdaemon pcscd` and `sudo systemctl start pcscd` |
| `pcsc_scan` blocks GPG | Kill `pcsc_scan` — it holds an exclusive lock on the reader |
| PIN timeout during `key-attr` | PIN dialog appeared as GUI popup; install and configure `pinentry-curses` |
| `gpg-connect-agent` hangs in scripts | `gpg-connect-agent` block-buffers when stdin is a pipe (non-TTY); connect directly to the gpg-agent Unix socket instead |
| `allow-loopback-pinentry` missing | Add `allow-loopback-pinentry` to `~/.gnupg/gpg-agent.conf`, then `gpgconf --kill gpg-agent` |
