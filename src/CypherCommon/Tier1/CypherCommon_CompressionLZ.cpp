//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionLZ.cpp
//  Purpose: Implements the deterministic internal Cypher LZ codec.
//  Details: The byte format uses a versioned frame header, literal runs, and
//           bounded 16-bit backward matches. Decoding rejects malformed frames
//           before reading or writing outside caller-owned buffers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Compression LZ Implementation Notes

Compression is a storage optimization, not an integrity or security boundary. Every decoder
receives an explicit output limit and must reject truncated, oversized, or inconsistent streams.
================
*/

#include "CypherCommon_CompressionLZ.h"

namespace cypher::common
{

namespace
{

inline constexpr byte CY_LZ_MAGIC[]{ 'C', 'Y', 'L', 'Z' }; // Identifies the private frame format.
inline constexpr u8 CY_LZ_VERSION = 1u;                    // On-disk decoder contract version.
inline constexpr usize CY_LZ_HEADER_BYTES = 16u;           // Fixed magic/version/size header.
inline constexpr usize CY_LZ_HASH_BITS = 12u;              // 4096-entry stack hash table.
inline constexpr usize CY_LZ_HASH_COUNT = 1u << CY_LZ_HASH_BITS;
inline constexpr usize CY_LZ_MAX_DISTANCE = CY_U16_MAX;    // Match distance stored as little-endian u16.
inline constexpr usize CY_LZ_MIN_MATCH = 3u;               // Shortest match that earns a tag.
inline constexpr usize CY_LZ_MAX_MATCH = 130u;             // Seven tag bits plus minimum length.
inline constexpr usize CY_LZ_MAX_LITERAL = 128u;           // Literal tag stores length minus one.

CYPHER_NODISCARD compression_result_t Fail(
    compression_status_t status,
    usize cbRequired = 0u ) noexcept
{
    return { status, 0u, 0u, cbRequired };
}

CYPHER_NODISCARD bool_t IsValidBuffers(
    binary_block_t input,
    byte_span_t output ) noexcept
{
    return BinaryBlock_IsValid( input ) && Span_IsValid( output );
}

void WriteU16LE( byte *pDest, u16 value ) noexcept
{
    pDest[0] = static_cast<byte>( value & 0xFFu );
    pDest[1] = static_cast<byte>( value >> 8u );
}

void WriteU64LE( byte *pDest, u64 value ) noexcept
{
    for ( usize iByte = 0u; iByte < 8u; ++iByte ) {
        pDest[iByte] = static_cast<byte>( value >> ( iByte * 8u ) );
    }
}

CYPHER_NODISCARD u16 ReadU16LE( const byte *pSource ) noexcept
{
    return static_cast<u16>(
        static_cast<u16>( pSource[0] ) |
        static_cast<u16>( static_cast<u16>( pSource[1] ) << 8u ) );
}

CYPHER_NODISCARD u64 ReadU64LE( const byte *pSource ) noexcept
{
    u64 value = 0u;
    for ( usize iByte = 0u; iByte < 8u; ++iByte ) {
        value |= static_cast<u64>( pSource[iByte] ) << ( iByte * 8u );
    }
    return value;
}

CYPHER_NODISCARD usize HashTriplet( const byte *pData ) noexcept
{
    const u32 value =
        static_cast<u32>( pData[0] ) |
        ( static_cast<u32>( pData[1] ) << 8u ) |
        ( static_cast<u32>( pData[2] ) << 16u );
    // Multiplicative hashing keeps the high bits of three-byte patterns well distributed.
    return static_cast<usize>(
        ( value * 2654435761u ) >> ( 32u - CY_LZ_HASH_BITS ) );
}

void WriteHeader( byte *pDest, usize cbInput ) noexcept
{
    // Header fields are written explicitly so host endianness and padding never leak to disk.
    Cy_MemCopy( pDest, CY_LZ_MAGIC, sizeof( CY_LZ_MAGIC ) );
    pDest[4] = CY_LZ_VERSION;
    pDest[5] = 0u;
    WriteU16LE( pDest + 6u, static_cast<u16>( CY_LZ_HEADER_BYTES ) );
    WriteU64LE( pDest + 8u, static_cast<u64>( cbInput ) );
}

CYPHER_NODISCARD bool_t ValidateHeader(
    binary_block_t input,
    usize &cbOutput ) noexcept
{
    if ( input.cbSize < CY_LZ_HEADER_BYTES ||
         !Cy_MemEqual( input.pData, CY_LZ_MAGIC, sizeof( CY_LZ_MAGIC ) ) ||
         input.pData[4] != CY_LZ_VERSION ||
         input.pData[5] != 0u ||
         ReadU16LE( input.pData + 6u ) != CY_LZ_HEADER_BYTES ) {
        return CY_FALSE;
    }

    const u64 cbDecoded = ReadU64LE( input.pData + 8u );
    if ( cbDecoded > static_cast<u64>( CY_USIZE_MAX ) ) {
        return CY_FALSE;
    }
    cbOutput = static_cast<usize>( cbDecoded );
    return CY_TRUE;
}

void WriteLiteralRun(
    const byte *pInput,
    usize iBegin,
    usize cbLiteral,
    byte *pOutput,
    usize &iOutput ) noexcept
{
    pOutput[iOutput++] = static_cast<byte>( cbLiteral - 1u );
    Cy_MemCopy( pOutput + iOutput, pInput + iBegin, cbLiteral );
    iOutput += cbLiteral;
}

} // namespace

usize CompressionLZ_CompressBound( usize cbInput ) noexcept
{
    const usize nLiteralHeaders =
        cbInput / CY_LZ_MAX_LITERAL +
        ( ( cbInput % CY_LZ_MAX_LITERAL ) != 0u ? 1u : 0u );
    if ( cbInput > CY_USIZE_MAX - CY_LZ_HEADER_BYTES ||
         nLiteralHeaders > CY_USIZE_MAX - CY_LZ_HEADER_BYTES - cbInput ) {
        return 0u;
    }
    return CY_LZ_HEADER_BYTES + cbInput + nLiteralHeaders;
}

compression_result_t CompressionLZ_Compress(
    binary_block_t input,
    byte_span_t output ) noexcept
{
    if ( !IsValidBuffers( input, output ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }

    const usize cbBound = CompressionLZ_CompressBound( input.cbSize );
    if ( cbBound == 0u ) {
        return Fail( compression_status_t::INTERNAL_ERROR );
    }
    if ( output.nCount < cbBound ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbBound );
    }

    WriteHeader( output.pData, input.cbSize );
    usize iOutput = CY_LZ_HEADER_BYTES;
    if ( input.cbSize == 0u ) {
        return {
            compression_status_t::OK,
            0u,
            CY_LZ_HEADER_BYTES,
            CY_LZ_HEADER_BYTES
        };
    }

    // One most-recent position per hash gives bounded memory and deterministic output.
    usize lastPositions[CY_LZ_HASH_COUNT]{};
    for ( usize &position : lastPositions ) {
        position = CY_USIZE_MAX;
    }

    usize iCursor = 0u;
    usize iLiteral = 0u;
    while ( iCursor < input.cbSize ) {
        usize cbMatch = 0u;
        usize cbDistance = 0u;
        if ( input.cbSize - iCursor >= CY_LZ_MIN_MATCH ) {
            const usize iHash = HashTriplet( input.pData + iCursor );
            const usize iCandidate = lastPositions[iHash];
            lastPositions[iHash] = iCursor;
            if ( iCandidate != CY_USIZE_MAX &&
                 iCursor > iCandidate &&
                 iCursor - iCandidate <= CY_LZ_MAX_DISTANCE ) {
                const usize cbMaximum =
                    ( input.cbSize - iCursor ) < CY_LZ_MAX_MATCH
                    ? input.cbSize - iCursor
                    : CY_LZ_MAX_MATCH;
                while ( cbMatch < cbMaximum &&
                        input.pData[iCandidate + cbMatch] ==
                            input.pData[iCursor + cbMatch] ) {
                    ++cbMatch;
                }
                if ( cbMatch >= CY_LZ_MIN_MATCH ) {
                    cbDistance = iCursor - iCandidate;
                } else {
                    cbMatch = 0u;
                }
            }
        }

        if ( cbMatch != 0u ) {
            // Flush pending literals before emitting the backward-reference token.
            const usize cbLiteral = iCursor - iLiteral;
            if ( cbLiteral != 0u ) {
                WriteLiteralRun(
                    input.pData,
                    iLiteral,
                    cbLiteral,
                    output.pData,
                    iOutput );
            }
            output.pData[iOutput++] = static_cast<byte>(
                0x80u | static_cast<byte>( cbMatch - CY_LZ_MIN_MATCH ) );
            WriteU16LE(
                output.pData + iOutput,
                static_cast<u16>( cbDistance ) );
            iOutput += 2u;

            // Seed positions inside the match so later matches can begin there.
            const usize iMatchEnd = iCursor + cbMatch;
            for ( usize iPosition = iCursor + 1u;
                  iPosition + CY_LZ_MIN_MATCH <= iMatchEnd;
                  ++iPosition ) {
                lastPositions[HashTriplet( input.pData + iPosition )] = iPosition;
            }
            iCursor = iMatchEnd;
            iLiteral = iCursor;
            continue;
        }

        ++iCursor;
        if ( iCursor - iLiteral == CY_LZ_MAX_LITERAL ) {
            WriteLiteralRun(
                input.pData,
                iLiteral,
                CY_LZ_MAX_LITERAL,
                output.pData,
                iOutput );
            iLiteral = iCursor;
        }
    }

    if ( iLiteral < input.cbSize ) {
        WriteLiteralRun(
            input.pData,
            iLiteral,
            input.cbSize - iLiteral,
            output.pData,
            iOutput );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        iOutput,
        iOutput
    };
}

compression_result_t CompressionLZ_Decompress(
    binary_block_t input,
    byte_span_t output ) noexcept
{
    if ( !IsValidBuffers( input, output ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }

    usize cbDecoded = 0u;
    if ( !ValidateHeader( input, cbDecoded ) ) {
        return Fail( compression_status_t::CORRUPT_INPUT );
    }
    if ( output.nCount < cbDecoded ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbDecoded );
    }

    usize iInput = CY_LZ_HEADER_BYTES;
    usize iOutput = 0u;
    while ( iOutput < cbDecoded ) {
        if ( iInput >= input.cbSize ) {
            return Fail( compression_status_t::CORRUPT_INPUT, cbDecoded );
        }

        // High tag bit selects match; low seven bits encode length minus its base.
        const byte tag = input.pData[iInput++];
        if ( ( tag & 0x80u ) == 0u ) {
            const usize cbLiteral = static_cast<usize>( tag ) + 1u;
            if ( cbLiteral > input.cbSize - iInput ||
                 cbLiteral > cbDecoded - iOutput ) {
                return Fail( compression_status_t::CORRUPT_INPUT, cbDecoded );
            }
            Cy_MemCopy(
                output.pData + iOutput,
                input.pData + iInput,
                cbLiteral );
            iInput += cbLiteral;
            iOutput += cbLiteral;
            continue;
        }

        if ( input.cbSize - iInput < 2u ) {
            return Fail( compression_status_t::CORRUPT_INPUT, cbDecoded );
        }
        const usize cbMatch =
            static_cast<usize>( tag & 0x7Fu ) + CY_LZ_MIN_MATCH;
        const usize cbDistance = ReadU16LE( input.pData + iInput );
        iInput += 2u;
        if ( cbDistance == 0u || cbDistance > iOutput ||
             cbMatch > cbDecoded - iOutput ) {
            return Fail( compression_status_t::CORRUPT_INPUT, cbDecoded );
        }
        // Copy bytewise because valid LZ matches may overlap their own output.
        const usize iMatch = iOutput - cbDistance;
        for ( usize iByte = 0u; iByte < cbMatch; ++iByte ) {
            output.pData[iOutput++] = output.pData[iMatch + iByte];
        }
    }
    if ( iInput != input.cbSize ) {
        return Fail( compression_status_t::CORRUPT_INPUT, cbDecoded );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        cbDecoded,
        cbDecoded
    };
}

} // namespace cypher::common
