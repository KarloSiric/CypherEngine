//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Diff.cpp
//  Purpose: Implements deterministic binary delta generation and application.
//  Details: Version one emits source-copy and target-literal operations around the
//           common prefix/suffix. Apply validates structure and target hash first.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Diff.h"

#include <limits>

namespace cypher::common
{

namespace
{

constexpr byte DIFF_MAGIC[]{ 'C', 'Y', 'B', 'D' }; // Binary-diff stream signature.
constexpr byte DIFF_VERSION = 1u;                  // Current operation encoding version.

enum class diff_op_t : byte {
    COPY = 1u,    // Copy a source range: offset, length.
    LITERAL = 2u, // Copy literal bytes embedded in the operation stream.
    END = 0xFFu   // Required end marker; no trailing bytes are permitted.
};

bool_t DiffIsInitialized( const binary_diff_t *pDiff ) noexcept
{
    return pDiff != nullptr &&
           Blob_IsValid( &pDiff->encodedOps ) &&
           Allocator_IsValid( pDiff->encodedOps.pAllocator );
}

bool_t AppendByte( blob_t &blob, byte value ) noexcept
{
    return Blob_Append( &blob, { &value, 1u } );
}

bool_t AppendVarU64( blob_t &blob, u64 value ) noexcept
{
    // Seven payload bits per byte; the high bit means another byte follows.
    byte encoded[10]{};
    usize cbEncoded = 0u;
    do {
        byte next = static_cast<byte>( value & 0x7Fu );
        value >>= 7u;
        if ( value != 0u ) {
            next |= 0x80u;
        }
        encoded[cbEncoded++] = next;
    } while ( value != 0u );
    return Blob_Append( &blob, { encoded, cbEncoded } );
}

bool_t AppendCopy(
    blob_t &blob,
    usize iSource,
    usize cbData ) noexcept
{
    return AppendByte( blob, static_cast<byte>( diff_op_t::COPY ) ) &&
           AppendVarU64( blob, static_cast<u64>( iSource ) ) &&
           AppendVarU64( blob, static_cast<u64>( cbData ) );
}

bool_t AppendLiteral(
    blob_t &blob,
    const byte *pData,
    usize cbData ) noexcept
{
    return AppendByte( blob, static_cast<byte>( diff_op_t::LITERAL ) ) &&
           AppendVarU64( blob, static_cast<u64>( cbData ) ) &&
           Blob_Append( &blob, { pData, cbData } );
}

bool_t ReadVarU64(
    binary_block_t encoded,
    usize &iCursor,
    u64 &valueOut ) noexcept
{
    valueOut = 0u;
    for ( u32 iByte = 0u; iByte < 10u; ++iByte ) {
        if ( iCursor >= encoded.cbSize ) {
            return CY_FALSE;
        }
        const byte value = encoded.pData[iCursor++];
        // A u64 uses at most one payload bit in its tenth encoded byte.
        if ( iByte == 9u && ( value & 0xFEu ) != 0u ) {
            return CY_FALSE;
        }
        valueOut |= static_cast<u64>( value & 0x7Fu ) << ( iByte * 7u );
        if ( ( value & 0x80u ) == 0u ) {
            // Reject redundant terminal zero groups to keep one canonical encoding.
            return iByte == 0u || value != 0u;
        }
    }
    return CY_FALSE;
}

bool_t RangesOverlap(
    const void *pLeft,
    usize cbLeft,
    const void *pRight,
    usize cbRight ) noexcept
{
    if ( cbLeft == 0u || cbRight == 0u ) {
        return CY_FALSE;
    }
    const uintptr nLeft = reinterpret_cast<uintptr>( pLeft );
    const uintptr nRight = reinterpret_cast<uintptr>( pRight );
    constexpr uintptr nMaximum = std::numeric_limits<uintptr>::max();
    if ( nLeft > nMaximum - cbLeft || nRight > nMaximum - cbRight ) {
        return CY_TRUE;
    }
    return nLeft < nRight + cbRight && nRight < nLeft + cbLeft;
}

struct diff_validation_t {
    diff_status_t status{ diff_status_t::OK }; // Structural validation result.
    usize cbOutput{ 0u };                      // Reconstructed byte count.
    content_hash_t outputHash{};               // Hash produced while validating.
};

diff_validation_t ValidateOperations(
    binary_block_t source,
    const binary_diff_t &diff ) noexcept
{
    // Validation walks the complete stream and hashes logical output without writing it.
    const binary_block_t encoded = Blob_Block( &diff.encodedOps );
    if ( encoded.cbSize < sizeof( DIFF_MAGIC ) + 2u ||
         Cy_MemCompare( encoded.pData, DIFF_MAGIC, sizeof( DIFF_MAGIC ) ) != 0 ||
         encoded.pData[sizeof( DIFF_MAGIC )] != DIFF_VERSION ) {
        return { diff_status_t::CORRUPT_DIFF };
    }

    hash_xxh3_stream_t stream{};
    if ( !HashXXH3_StreamInit(
             &stream,
             hash_xxh3_stream_mode_t::HASH_128,
             0u ) ) {
        return { diff_status_t::INTERNAL_ERROR };
    }

    usize iCursor = sizeof( DIFF_MAGIC ) + 1u; // Skip magic and version.
    usize cbOutput = 0u;
    bool_t bEnded = CY_FALSE;
    while ( iCursor < encoded.cbSize ) {
        const diff_op_t operation = static_cast<diff_op_t>( encoded.pData[iCursor++] );
        if ( operation == diff_op_t::END ) {
            bEnded = CY_TRUE;
            break;
        }

        // Each operation resolves to one bounded block of reconstructed output.
        binary_block_t outputBlock{};
        if ( operation == diff_op_t::COPY ) {
            u64 iSource64 = 0u;
            u64 cbData64 = 0u;
            if ( !ReadVarU64( encoded, iCursor, iSource64 ) ||
                 !ReadVarU64( encoded, iCursor, cbData64 ) ||
                 iSource64 > static_cast<u64>( CY_USIZE_MAX ) ||
                 cbData64 > static_cast<u64>( CY_USIZE_MAX ) ) {
                return { diff_status_t::CORRUPT_DIFF };
            }
            const usize iSource = static_cast<usize>( iSource64 );
            const usize cbData = static_cast<usize>( cbData64 );
            if ( iSource > source.cbSize || cbData > source.cbSize - iSource ) {
                return { diff_status_t::CORRUPT_DIFF };
            }
            outputBlock = {
                cbData != 0u ? source.pData + iSource : nullptr,
                cbData
            };
        } else if ( operation == diff_op_t::LITERAL ) {
            u64 cbData64 = 0u;
            if ( !ReadVarU64( encoded, iCursor, cbData64 ) ||
                 cbData64 > static_cast<u64>( CY_USIZE_MAX ) ) {
                return { diff_status_t::CORRUPT_DIFF };
            }
            const usize cbData = static_cast<usize>( cbData64 );
            if ( cbData > encoded.cbSize - iCursor ) {
                return { diff_status_t::CORRUPT_DIFF };
            }
            outputBlock = { encoded.pData + iCursor, cbData };
            iCursor += cbData;
        } else {
            return { diff_status_t::CORRUPT_DIFF };
        }

        if ( outputBlock.cbSize > CY_USIZE_MAX - cbOutput ) {
            return { diff_status_t::OUTPUT_OVERFLOW };
        }
        cbOutput += outputBlock.cbSize;
        if ( !HashXXH3_StreamUpdate( &stream, outputBlock ) ) {
            return { diff_status_t::INTERNAL_ERROR };
        }
    }
    if ( !bEnded || iCursor != encoded.cbSize || cbOutput != diff.cbTarget ) {
        return { diff_status_t::CORRUPT_DIFF };
    }

    hash128_t hash{};
    if ( !HashXXH3_StreamDigest128( &stream, &hash ) ) {
        return { diff_status_t::INTERNAL_ERROR };
    }
    const content_hash_t outputHash{ hash.low, hash.high };
    if ( !ContentHash_Equals( outputHash, diff.targetHash ) ) {
        return { diff_status_t::CORRUPT_DIFF };
    }
    return { diff_status_t::OK, cbOutput, outputHash };
}

void ApplyOperations(
    binary_block_t source,
    const binary_diff_t &diff,
    byte *pDest ) noexcept
{
    // The caller validates this stream first, so this pass can remain branch-light.
    const binary_block_t encoded = Blob_Block( &diff.encodedOps );
    usize iCursor = sizeof( DIFF_MAGIC ) + 1u;
    usize iOutput = 0u;
    while ( iCursor < encoded.cbSize ) {
        const diff_op_t operation = static_cast<diff_op_t>( encoded.pData[iCursor++] );
        if ( operation == diff_op_t::END ) {
            return;
        }
        if ( operation == diff_op_t::COPY ) {
            u64 iSource = 0u;
            u64 cbData = 0u;
            static_cast<void>( ReadVarU64( encoded, iCursor, iSource ) );
            static_cast<void>( ReadVarU64( encoded, iCursor, cbData ) );
            Cy_MemCopy(
                pDest + iOutput,
                source.pData + static_cast<usize>( iSource ),
                static_cast<usize>( cbData ) );
            iOutput += static_cast<usize>( cbData );
        } else {
            u64 cbData = 0u;
            static_cast<void>( ReadVarU64( encoded, iCursor, cbData ) );
            Cy_MemCopy(
                pDest + iOutput,
                encoded.pData + iCursor,
                static_cast<usize>( cbData ) );
            iCursor += static_cast<usize>( cbData );
            iOutput += static_cast<usize>( cbData );
        }
    }
}

} // namespace

bool_t Diff_Init(
    binary_diff_t *pDiff,
    const allocator_t *pAllocator ) noexcept
{
    if ( pDiff == nullptr ||
         !Blob_IsValid( &pDiff->encodedOps ) ||
         pDiff->encodedOps.pAllocator != nullptr ||
         !Allocator_IsValid( pAllocator ) ) {
        return CY_FALSE;
    }
    pDiff->sourceHash = {};
    pDiff->targetHash = {};
    pDiff->cbSource = 0u;
    pDiff->cbTarget = 0u;
    return Blob_Init( &pDiff->encodedOps, pAllocator );
}

void Diff_Shutdown( binary_diff_t *pDiff ) noexcept
{
    if ( pDiff == nullptr ) {
        return;
    }
    Blob_Shutdown( &pDiff->encodedOps );
    pDiff->sourceHash = {};
    pDiff->targetHash = {};
    pDiff->cbSource = 0u;
    pDiff->cbTarget = 0u;
}

void Diff_Clear( binary_diff_t *pDiff ) noexcept
{
    if ( !DiffIsInitialized( pDiff ) ) {
        return;
    }
    Blob_Clear( &pDiff->encodedOps );
    pDiff->sourceHash = {};
    pDiff->targetHash = {};
    pDiff->cbSource = 0u;
    pDiff->cbTarget = 0u;
}

diff_status_t Diff_Generate(
    binary_block_t source,
    binary_block_t target,
    binary_diff_t *pDiffOut ) noexcept
{
    if ( !BinaryBlock_IsValid( source ) ||
         !BinaryBlock_IsValid( target ) ||
         !DiffIsInitialized( pDiffOut ) ) {
        return diff_status_t::INVALID_ARGUMENT;
    }

    // Version one preserves the common edges and stores the changed middle literally.
    usize cbPrefix = 0u;
    const usize cbShortest = source.cbSize < target.cbSize
        ? source.cbSize
        : target.cbSize;
    while ( cbPrefix < cbShortest &&
            source.pData[cbPrefix] == target.pData[cbPrefix] ) {
        ++cbPrefix;
    }
    usize cbSuffix = 0u;
    while ( cbSuffix < cbShortest - cbPrefix &&
            source.pData[source.cbSize - cbSuffix - 1u] ==
                target.pData[target.cbSize - cbSuffix - 1u] ) {
        ++cbSuffix;
    }
    const usize cbLiteral = target.cbSize - cbPrefix - cbSuffix;

    // Build into temporary storage so failure never destroys the previous diff.
    blob_t encoded{};
    if ( !Blob_Init( &encoded, pDiffOut->encodedOps.pAllocator ) ) {
        return diff_status_t::OUT_OF_MEMORY;
    }
    const bool_t bEncoded =
        Blob_Append( &encoded, { DIFF_MAGIC, sizeof( DIFF_MAGIC ) } ) &&
        AppendByte( encoded, DIFF_VERSION ) &&
        ( cbPrefix == 0u || AppendCopy( encoded, 0u, cbPrefix ) ) &&
        ( cbLiteral == 0u || AppendLiteral(
            encoded,
            target.pData + cbPrefix,
            cbLiteral ) ) &&
        ( cbSuffix == 0u || AppendCopy(
            encoded,
            source.cbSize - cbSuffix,
            cbSuffix ) ) &&
        AppendByte( encoded, static_cast<byte>( diff_op_t::END ) );
    if ( !bEncoded ) {
        return diff_status_t::OUT_OF_MEMORY;
    }

    Blob_Shutdown( &pDiffOut->encodedOps );
    Blob_Move( &pDiffOut->encodedOps, &encoded );
    pDiffOut->sourceHash = ContentHash_Data( source );
    pDiffOut->targetHash = ContentHash_Data( target );
    pDiffOut->cbSource = source.cbSize;
    pDiffOut->cbTarget = target.cbSize;
    return diff_status_t::OK;
}

diff_status_t Diff_Apply(
    binary_block_t source,
    const binary_diff_t &diff,
    byte_span_t dest,
    usize *pcbWrittenOut ) noexcept
{
    if ( pcbWrittenOut != nullptr ) {
        *pcbWrittenOut = 0u;
    }
    if ( !BinaryBlock_IsValid( source ) ||
         !Span_IsValid( dest ) ||
         !DiffIsInitialized( &diff ) ||
         ( dest.nCount != 0u && dest.pData == nullptr ) ) {
        return diff_status_t::INVALID_ARGUMENT;
    }
    if ( source.cbSize != diff.cbSource ||
         !ContentHash_Equals( ContentHash_Data( source ), diff.sourceHash ) ) {
        return diff_status_t::SOURCE_MISMATCH;
    }
    if ( dest.nCount < diff.cbTarget ) {
        return diff_status_t::OUTPUT_TOO_SMALL;
    }
    if ( RangesOverlap(
             source.pData,
             source.cbSize,
             dest.pData,
             diff.cbTarget ) ) {
        return diff_status_t::INVALID_ARGUMENT;
    }

    // Do not touch destination memory until structure, bounds, and target hash agree.
    const diff_validation_t validation = ValidateOperations( source, diff );
    if ( validation.status != diff_status_t::OK ) {
        return validation.status;
    }
    ApplyOperations( source, diff, dest.pData );
    if ( pcbWrittenOut != nullptr ) {
        *pcbWrittenOut = validation.cbOutput;
    }
    return diff_status_t::OK;
}

usize Diff_SerializedSize( const binary_diff_t &diff ) noexcept
{
    return DiffIsInitialized( &diff )
        ? Blob_Size( &diff.encodedOps )
        : 0u;
}

} // namespace cypher::common
