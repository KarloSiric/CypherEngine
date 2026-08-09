<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/security_model.md
//  Purpose: Defines the CypherSecurity threat, ownership, and protocol model.
//  Details: This document records algorithm choices, key lifetimes, required
//           caller policy, and responsibilities intentionally owned elsewhere.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherSecurity Model

## Scope

CypherSecurity is the engine's cryptographic primitive and secret-ownership layer.
It wraps libsodium behind Cypher-owned types so engine, game, server, package, and
tool code do not depend directly on backend types or return codes.

The subsystem does not invent cryptographic algorithms. Cypher owns validation,
memory policy, domain separation, nonce sequencing, state transitions, and API
contracts. libsodium owns the audited primitive implementations and operating-system
entropy integration.

## Primitive Set

| Need | Cypher API | Primitive |
| --- | --- | --- |
| Cryptographic entropy | `SecurityRandom_*` | OS-backed libsodium random source |
| General digest | `SecurityDigest_*` | BLAKE2b |
| Hash-table flooding resistance | `SecurityShortHash_*` | SipHash-2-4 |
| Password storage | `PasswordHash_*` | Argon2id encoded strings |
| Secret memory | `SecureMemory_*` | Guard pages, zeroing, lock/protection policy |
| Domain-separated keys | `SecurityKdf_*` | BLAKE2b KDF |
| Message encryption | `Aead_*` | XChaCha20-Poly1305 AEAD |
| Public signatures | `Signature_*` | Ed25519 / Ed25519ph |
| Session agreement | `KeyExchange_*` | X25519 with directional session keys |
| Large encrypted streams | `SecretStream*` | XChaCha20-Poly1305 secretstream |
| Text transport | `SecurityHex_*`, `SecurityBase64_*` | Strict canonical encoding |

Fast non-cryptographic hashes in Tier1 are not substitutes for CypherSecurity.
CRC, FNV, xxHash, and stable content hashes provide lookup, compatibility, or
corruption-detection behavior; they do not provide authentication.

## Secret Ownership

- Encryption, signing, derivation, exchange, and session secrets use
  `secure_memory_t`. Small process-random hash-flooding seeds may remain value
  types because they are replaceable table policy rather than persisted credentials.
- Secret allocations are fixed-size, zeroed on release, and surrounded by backend
  guard pages.
- A created key is writable only while it is generated or imported. Successful
  construction changes it to read-only.
- Secret-bearing objects cannot be copied or moved.
- Borrowed secret views expire when the owning object changes protection or is
  destroyed. They must never be cached.
- Each object may be read concurrently after initialization. Mutation, access-state
  changes, and destruction require external synchronization.
- `REQUIRE_LOCKED` fails when the operating system cannot lock the allocation.
  `BEST_EFFORT` keeps the guarded allocation but exposes its lock result.
- Memory locking is defense in depth. It does not erase copies placed by callers in
  ordinary stack, heap, register, log, crash-dump, or serialized storage.

Keys should be created at the narrowest owning scope, derived per purpose, and
destroyed as soon as that scope ends. Rotation policy belongs to the protocol or
asset system that owns the key epoch. KDF contexts and subkey identifiers must be
stable, unique per purpose, and versioned when a protocol changes meaning.

## Nonce And Counter Rules

XChaCha20-Poly1305 requires a nonce never to repeat under the same key.

- Independent records may use `AeadNonce_Generate`.
- Ordered protocols may use a 16-byte random/persisted prefix plus a 64-bit counter.
- A persisted key must persist the matching next-counter state atomically.
- Resetting a counter while retaining the key and prefix is forbidden.
- Counter exhaustion is terminal. Rotate the key and create a new sequence.
- A failed encryption does not authorize nonce reuse unless the caller can prove no
  ciphertext escaped. The conservative rule is always to consume the nonce.

Secretstream owns its record nonce evolution internally. Records must be pulled in
the emitted order. Authentication failure destroys the pull state; callers cannot
continue from a potentially desynchronized stream.

## Authentication Rules

- AEAD authenticates ciphertext and optional public associated data. Associated
  data must include protocol identity, version, direction, and sequence information
  required to prevent cross-context acceptance.
- Ed25519 one-shot signatures and Ed25519ph multipart signatures are different
  constructions and are intentionally not interchangeable.
- Key exchange derives matching directional keys, but it does not authenticate who
  owns the peer public key. A handshake must bind that key to a trusted identity,
  certificate, pre-shared secret, or signature.
- Base64 and hexadecimal are encodings only. They provide no secrecy, integrity, or
  authenticity.
- Password verification uses stored Argon2id encoded records. Passwords must never
  be stored using a fast digest.

## Failure Rules

- Invalid public contracts return `INVALID_ARGUMENT` and may assert in diagnostic
  builds.
- Authentication failures return `AUTHENTICATION_FAILED`; unacceptable X25519 peer
  keys return `PEER_KEY_REJECTED`.
- Capacity checks occur before cryptographic state mutation.
- Candidate plaintext is cleared after failed authentication.
- Partially initialized keys and streams are destroyed transactionally.
- Output lengths are committed only after successful operations.
- Error messages and logs must never contain passwords, private keys, session keys,
  nonces tied to private protocol state, or candidate plaintext.

## Responsibilities Outside CypherSecurity

CypherNetwork must provide handshake state machines, transcript binding, packet
sequence numbers, replay windows, disconnect policy, rate limiting, key epochs, and
rekey coordination. It may consume KX, signatures, KDF, and AEAD from this layer.

CypherPak and the resource pipeline must define signature manifests, trusted signer
keys, format versions, rollback policy, and exactly which bytes are signed.

CAC must define telemetry, attestation, server authority, evidence policy, privacy,
and enforcement. Cryptography can protect CAC messages but cannot make a client
machine trustworthy.

Platform/tool work may later add OS keychain integration, offline key-generation
tools, certificate/trust-store management, and signing services. Raw production
signing keys must not live in source control or ordinary project configuration.

## Verification Standard

Every primitive requires deterministic vectors where standards provide them,
round-trip tests, malformed-input tests, state/lifecycle tests, tamper tests,
capacity tests, concurrent read tests where supported, sanitizer execution, and
Release benchmarks for operations whose cost matters. CI must exercise the same
public contracts on Windows, Linux, and macOS.
