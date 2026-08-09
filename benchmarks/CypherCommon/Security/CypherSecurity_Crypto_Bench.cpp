//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: benchmarks/CypherCommon/Security/CypherSecurity_Crypto_Bench.cpp
//  Purpose: Benchmarks CypherSecurity protocol-facing primitives.
//  Details: Measurements cover derivation, authenticated encryption, signing,
//           key exchange, streaming records, and strict text encoding.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSecurity.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;
using namespace cypher::security;

namespace
{

constexpr usize CY_SECURITY_BENCH_MAX_DATA = 64u * 1024u;

const std::array<byte, CY_SECURITY_BENCH_MAX_DATA> g_cryptoData = [] {
    std::array<byte, CY_SECURITY_BENCH_MAX_DATA> data{};
    for ( usize iByte = 0u; iByte < data.size(); ++iByte ) {
        data[iByte] = static_cast<byte>( iByte * 29u + 17u );
    }
    return data;
}();

bool EnsureSecurityReady( benchmark::State &state )
{
    if ( Security_Init() == security_status_t::OK ) {
        return true;
    }
    state.SkipWithError( "Unable to initialize CypherSecurity." );
    return false;
}

} // namespace

static void BM_SecurityKdf_Derive32( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    kdf_master_key_t master{};
    kdf_context_t context{};
    if ( SecurityKdf_GenerateMasterKey(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &master ) != security_status_t::OK ||
         SecurityKdf_ContextFromBytes( "CYPHBEN1", 8u, &context ) !=
             security_status_t::OK ) {
        state.SkipWithError( "Unable to initialize KDF benchmark." );
        return;
    }

    u64 nSubkey = 0u;
    for ( auto _ : state ) {
        kdf_subkey_t subkey{};
        security_status_t result = SecurityKdf_DeriveSubkey(
            &master,
            context,
            nSubkey++,
            32u,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &subkey );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( SecurityKdf_SubkeyBlock( &subkey ).pData );
    }
}

static void BM_Aead_Encrypt( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const usize cbPlaintext = static_cast<usize>( state.range( 0 ) );
    aead_key_t key{};
    aead_nonce_sequence_t sequence{};
    if ( AeadKey_Generate(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &key ) != security_status_t::OK ||
         AeadNonceSequence_Init( 0u, &sequence ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to initialize AEAD benchmark." );
        return;
    }
    std::array<byte, CY_SECURITY_BENCH_MAX_DATA + CY_SECURITY_AEAD_TAG_SIZE>
        ciphertext{};

    for ( auto _ : state ) {
        aead_nonce_t nonce{};
        usize cbWritten = 0u;
        const security_status_t nonceResult =
            AeadNonceSequence_Next( &sequence, &nonce );
        security_status_t result = nonceResult == security_status_t::OK
            ? Aead_Encrypt(
                  BinaryBlock_FromData( g_cryptoData.data(), cbPlaintext ),
                  {},
                  nonce,
                  &key,
                  ciphertext.data(),
                  ciphertext.size(),
                  &cbWritten )
            : nonceResult;
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( cbWritten );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbPlaintext ) );
}

static void BM_Signature_Sign( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    const usize cbMessage = static_cast<usize>( state.range( 0 ) );
    signature_keypair_t keyPair{};
    if ( SignatureKeyPair_Generate(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &keyPair ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to initialize signature benchmark." );
        return;
    }
    const binary_block_t message =
        BinaryBlock_FromData( g_cryptoData.data(), cbMessage );
    for ( auto _ : state ) {
        signature_t signature{};
        security_status_t result =
            Signature_Sign( message, &keyPair, &signature );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( signature );
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbMessage ) );
}

static void BM_KeyExchange_DeriveSession( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    key_exchange_keypair_t client{};
    key_exchange_keypair_t server{};
    if ( KeyExchangeKeyPair_Generate(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &client ) != security_status_t::OK ||
         KeyExchangeKeyPair_Generate(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &server ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to initialize key-exchange benchmark." );
        return;
    }
    for ( auto _ : state ) {
        key_exchange_session_keys_t session{};
        security_status_t result = KeyExchange_ClientSessionKeys(
            &client,
            server.publicKey,
            secure_memory_lock_policy_t::BEST_EFFORT,
            &session );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize(
            KeyExchangeSessionKeys_ReceiveBlock( &session ).pData );
    }
}

static void BM_SecretStream_RoundTrip1K( benchmark::State &state )
{
    if ( !EnsureSecurityReady( state ) ) {
        return;
    }
    secret_stream_key_t key{};
    if ( SecretStreamKey_Generate(
             secure_memory_lock_policy_t::BEST_EFFORT,
             &key ) != security_status_t::OK ) {
        state.SkipWithError( "Unable to initialize stream benchmark." );
        return;
    }
    constexpr usize cbPlaintext = 1024u;
    std::array<byte, cbPlaintext + CY_SECURITY_SECRET_STREAM_OVERHEAD_SIZE>
        ciphertext{};
    std::array<byte, cbPlaintext> plaintext{};

    for ( auto _ : state ) {
        secret_stream_push_t push{};
        secret_stream_pull_t pull{};
        secret_stream_header_t header{};
        usize cbCiphertext = 0u;
        usize cbOutput = 0u;
        secret_stream_tag_t tag{};
        security_status_t result =
            SecretStreamPush_Begin( &key, &push, &header );
        if ( result == security_status_t::OK ) {
            result = SecretStreamPush_Message(
                &push,
                BinaryBlock_FromData( g_cryptoData.data(), cbPlaintext ),
                {},
                secret_stream_tag_t::FINAL,
                ciphertext.data(),
                ciphertext.size(),
                &cbCiphertext );
        }
        if ( result == security_status_t::OK ) {
            result = SecretStreamPull_Begin( &key, header, &pull );
        }
        if ( result == security_status_t::OK ) {
            result = SecretStreamPull_Message(
                &pull,
                BinaryBlock_FromData( ciphertext.data(), cbCiphertext ),
                {},
                plaintext.data(),
                plaintext.size(),
                &cbOutput,
                &tag );
        }
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( cbOutput );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbPlaintext ) );
}

static void BM_Base64_Encode( benchmark::State &state )
{
    const usize cbBinary = static_cast<usize>( state.range( 0 ) );
    std::array<char, ( CY_SECURITY_BENCH_MAX_DATA * 4u / 3u ) + 8u> encoded{};
    for ( auto _ : state ) {
        usize cchWritten = 0u;
        security_status_t result = SecurityBase64_Encode(
            BinaryBlock_FromData( g_cryptoData.data(), cbBinary ),
            base64_variant_t::ORIGINAL_NO_PADDING,
            encoded.data(),
            encoded.size(),
            &cchWritten );
        benchmark::DoNotOptimize( result );
        benchmark::DoNotOptimize( cchWritten );
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(
        static_cast<i64>( state.iterations() ) *
        static_cast<i64>( cbBinary ) );
}

BENCHMARK( BM_SecurityKdf_Derive32 );
BENCHMARK( BM_Aead_Encrypt )->Arg( 1024 )->Arg( 64 * 1024 );
BENCHMARK( BM_Signature_Sign )->Arg( 1024 )->Arg( 64 * 1024 );
BENCHMARK( BM_KeyExchange_DeriveSession );
BENCHMARK( BM_SecretStream_RoundTrip1K );
BENCHMARK( BM_Base64_Encode )->Arg( 1024 )->Arg( 64 * 1024 );
