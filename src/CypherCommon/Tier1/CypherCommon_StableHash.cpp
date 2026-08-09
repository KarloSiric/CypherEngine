//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StableHash.cpp
//  Purpose: Implements versioned deterministic hashing for persisted identifiers.
//  Details: Typed values are serialized directly into a streaming XXH3 state using
//           a fixed little-endian contract independent of host and compiler layout.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StableHash.h"

#include <bit>

namespace cypher::common
{

namespace
{

enum class stable_hash_value_kind_t : u8 {
    BOOL = 1u,
    U8,
    U16,
    U32,
    U64,
    I8,
    I16,
    I32,
    I64,
    F32,
    F64,
    BYTES,
    STRING,
    HASH64,
    HASH128
};

CYPHER_NODISCARD bool_t StableHash_WriteRaw(
    stable_hash_builder_t *pBuilder,
    const void *pData,
    usize cbData ) noexcept
{
    const bool_t bActive = StableHash_IsActive( pBuilder );
    CY_ASSERT_MSG( bActive, "StableHash write requires an active builder." );
    if ( !bActive ) {
        return CY_FALSE;
    }
    return HashXXH3_StreamUpdate(
        &pBuilder->stream,
        BinaryBlock_FromData( pData, cbData ) );
}

template <typename unsigned_t>
void StableHash_StoreLittle(
    byte *pDestination,
    unsigned_t value ) noexcept
{
    for ( usize iByte = 0u; iByte < sizeof( unsigned_t ); ++iByte ) {
        pDestination[iByte] = static_cast<byte>(
            value >> static_cast<u32>( iByte * 8u ) );
    }
}

template <typename unsigned_t>
CYPHER_NODISCARD bool_t StableHash_WriteUnsigned(
    stable_hash_builder_t *pBuilder,
    stable_hash_value_kind_t kind,
    unsigned_t value ) noexcept
{
    byte canonical[1u + sizeof( unsigned_t )]{};
    canonical[0] = static_cast<byte>( kind );
    StableHash_StoreLittle( canonical + 1u, value );
    return StableHash_WriteRaw( pBuilder, canonical, sizeof( canonical ) );
}

CYPHER_NODISCARD bool_t StableHash_WriteRange(
    stable_hash_builder_t *pBuilder,
    stable_hash_value_kind_t kind,
    const void *pData,
    usize cbData ) noexcept
{
    byte header[9]{};
    header[0] = static_cast<byte>( kind );
    StableHash_StoreLittle( header + 1u, static_cast<u64>( cbData ) );
    return StableHash_WriteRaw( pBuilder, header, sizeof( header ) ) &&
           StableHash_WriteRaw( pBuilder, pData, cbData );
}

CYPHER_NODISCARD u32 StableHash_CanonicalF32Bits( f32 value ) noexcept
{
    u32 nBits = value == 0.0f ? 0u : std::bit_cast<u32>( value );
    const bool_t bNan =
        ( nBits & 0x7F800000u ) == 0x7F800000u &&
        ( nBits & 0x007FFFFFu ) != 0u;
    if ( bNan ) {
        nBits = 0x7FC00000u;
    }
    return nBits;
}

CYPHER_NODISCARD u64 StableHash_CanonicalF64Bits( f64 value ) noexcept
{
    u64 nBits = value == 0.0 ? 0u : std::bit_cast<u64>( value );
    const bool_t bNan =
        ( nBits & 0x7FF0000000000000ull ) == 0x7FF0000000000000ull &&
        ( nBits & 0x000FFFFFFFFFFFFFull ) != 0u;
    if ( bNan ) {
        nBits = 0x7FF8000000000000ull;
    }
    return nBits;
}

} // namespace

bool_t StableHash_Begin(
    stable_hash_builder_t *pBuilder,
    stable_hash_domain_t nDomain,
    u32 nSchemaVersion ) noexcept
{
    const bool_t bValidBuilder = pBuilder != nullptr;
    CY_ASSERT_MSG( bValidBuilder, "StableHash_Begin requires builder storage." );
    if ( !bValidBuilder ) {
        return CY_FALSE;
    }

    pBuilder->bActive = CY_FALSE;
    if ( !HashXXH3_StreamInit(
             &pBuilder->stream,
             hash_xxh3_stream_mode_t::HASH_64,
             0u ) ) {
        return CY_FALSE;
    }
    pBuilder->bActive = CY_TRUE;

    byte header[24]{
        static_cast<byte>( 'C' ),
        static_cast<byte>( 'Y' ),
        static_cast<byte>( 'S' ),
        static_cast<byte>( 'H' )
    };
    StableHash_StoreLittle( header + 4u, CY_STABLE_HASH_CONTRACT_VERSION );
    StableHash_StoreLittle( header + 8u, nDomain );
    StableHash_StoreLittle( header + 16u, nSchemaVersion );
    if ( !StableHash_WriteRaw( pBuilder, header, sizeof( header ) ) ) {
        pBuilder->bActive = CY_FALSE;
        return CY_FALSE;
    }
    return CY_TRUE;
}

bool_t StableHash_IsActive(
    const stable_hash_builder_t *pBuilder ) noexcept
{
    return pBuilder != nullptr &&
           pBuilder->bActive &&
           HashXXH3_StreamIsValid( &pBuilder->stream );
}

bool_t StableHash_WriteBool(
    stable_hash_builder_t *pBuilder,
    bool_t value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::BOOL,
        static_cast<u8>( value ? 1u : 0u ) );
}

bool_t StableHash_WriteU8(
    stable_hash_builder_t *pBuilder,
    u8 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::U8,
        value );
}

bool_t StableHash_WriteU16(
    stable_hash_builder_t *pBuilder,
    u16 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::U16,
        value );
}

bool_t StableHash_WriteU32(
    stable_hash_builder_t *pBuilder,
    u32 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::U32,
        value );
}

bool_t StableHash_WriteU64(
    stable_hash_builder_t *pBuilder,
    u64 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::U64,
        value );
}

bool_t StableHash_WriteI8(
    stable_hash_builder_t *pBuilder,
    i8 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::I8,
        static_cast<u8>( value ) );
}

bool_t StableHash_WriteI16(
    stable_hash_builder_t *pBuilder,
    i16 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::I16,
        static_cast<u16>( value ) );
}

bool_t StableHash_WriteI32(
    stable_hash_builder_t *pBuilder,
    i32 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::I32,
        static_cast<u32>( value ) );
}

bool_t StableHash_WriteI64(
    stable_hash_builder_t *pBuilder,
    i64 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::I64,
        static_cast<u64>( value ) );
}

bool_t StableHash_WriteF32(
    stable_hash_builder_t *pBuilder,
    f32 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::F32,
        StableHash_CanonicalF32Bits( value ) );
}

bool_t StableHash_WriteF64(
    stable_hash_builder_t *pBuilder,
    f64 value ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::F64,
        StableHash_CanonicalF64Bits( value ) );
}

bool_t StableHash_WriteBytes(
    stable_hash_builder_t *pBuilder,
    binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "StableHash bytes require a valid borrowed range." );
    return bValidData
        ? StableHash_WriteRange(
              pBuilder,
              stable_hash_value_kind_t::BYTES,
              data.pData,
              data.cbSize )
        : CY_FALSE;
}

bool_t StableHash_WriteString(
    stable_hash_builder_t *pBuilder,
    string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "StableHash string requires a valid string view." );
    return bValidText
        ? StableHash_WriteRange(
              pBuilder,
              stable_hash_value_kind_t::STRING,
              text.pData,
              text.cchLength )
        : CY_FALSE;
}

bool_t StableHash_WriteHash64(
    stable_hash_builder_t *pBuilder,
    hash64_t hash ) noexcept
{
    return StableHash_WriteUnsigned(
        pBuilder,
        stable_hash_value_kind_t::HASH64,
        hash );
}

bool_t StableHash_WriteHash128(
    stable_hash_builder_t *pBuilder,
    hash128_t hash ) noexcept
{
    byte canonical[17]{};
    canonical[0] = static_cast<byte>( stable_hash_value_kind_t::HASH128 );
    StableHash_StoreLittle( canonical + 1u, hash.low );
    StableHash_StoreLittle( canonical + 9u, hash.high );
    return StableHash_WriteRaw( pBuilder, canonical, sizeof( canonical ) );
}

bool_t StableHash_End(
    stable_hash_builder_t *pBuilder,
    hash64_t *pHashOut ) noexcept
{
    const bool_t bActive = StableHash_IsActive( pBuilder );
    const bool_t bValidOutput = pHashOut != nullptr;
    CY_ASSERT_MSG( bActive, "StableHash_End requires an active builder." );
    CY_ASSERT_MSG( bValidOutput, "StableHash_End requires output storage." );
    if ( !bActive || !bValidOutput ) {
        return CY_FALSE;
    }

    if ( !HashXXH3_StreamDigest64( &pBuilder->stream, pHashOut ) ) {
        return CY_FALSE;
    }
    pBuilder->bActive = CY_FALSE;
    return CY_TRUE;
}

} // namespace cypher::common
