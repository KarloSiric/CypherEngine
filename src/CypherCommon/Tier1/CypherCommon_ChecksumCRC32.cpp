//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ChecksumCRC32.cpp
//  Purpose: Implements CRC-32/ISO-HDLC checksums.
//  Details: The reflected polynomial, initial state, and final XOR are fixed by the
//           public contract so package and packet checks remain portable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Checksum CRC32 Implementation Notes

This algorithm is deterministic over an explicit byte range. It is suitable for lookup, change
detection, or corruption checks, but must not be used as a cryptographic authenticator.
================
*/

#include "CypherCommon_ChecksumCRC32.h"

#include <array>

namespace cypher::common
{

namespace
{

using crc32_table_t = std::array<crc32_t, 256u>;
using crc32_slice_tables_t = std::array<crc32_table_t, 8u>;

constexpr crc32_slice_tables_t BuildCRC32Tables() noexcept
{
    // Slice zero advances one reflected byte; later tables precompute additional
    // byte advances so Update can consume eight bytes per iteration.
    crc32_slice_tables_t tables{};
    for ( usize iEntry = 0u; iEntry < tables[0].size(); ++iEntry ) {
        crc32_t nValue = static_cast<crc32_t>( iEntry );
        for ( u32 iBit = 0u; iBit < 8u; ++iBit ) {
            const crc32_t nMask = 0u - ( nValue & 1u );
            nValue = ( nValue >> 1u ) ^
                ( CY_CRC32_POLYNOMIAL_REFLECTED & nMask );
        }
        tables[0][iEntry] = nValue;
    }

    for ( usize iSlice = 1u; iSlice < tables.size(); ++iSlice ) {
        for ( usize iEntry = 0u; iEntry < tables[iSlice].size(); ++iEntry ) {
            const crc32_t nPrevious = tables[iSlice - 1u][iEntry];
            tables[iSlice][iEntry] =
                ( nPrevious >> 8u ) ^ tables[0][nPrevious & 0xFFu];
        }
    }
    return tables;
}

constexpr auto g_crc32Tables = BuildCRC32Tables();

} // namespace

crc32_t ChecksumCRC32_Update( crc32_t state, binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "CRC-32 requires a valid binary block." );
    if ( !bValidData ) {
        return state;
    }

    const byte *pCursor = data.pData;
    usize cbRemaining = data.cbSize;
    // Slicing-by-eight avoids unaligned native loads and remains endian-explicit.
    while ( cbRemaining >= 8u ) {
        const crc32_t nFirstWord = state ^
            static_cast<crc32_t>( pCursor[0] ) ^
            ( static_cast<crc32_t>( pCursor[1] ) << 8u ) ^
            ( static_cast<crc32_t>( pCursor[2] ) << 16u ) ^
            ( static_cast<crc32_t>( pCursor[3] ) << 24u );

        state =
            g_crc32Tables[7][nFirstWord & 0xFFu] ^
            g_crc32Tables[6][( nFirstWord >> 8u ) & 0xFFu] ^
            g_crc32Tables[5][( nFirstWord >> 16u ) & 0xFFu] ^
            g_crc32Tables[4][nFirstWord >> 24u] ^
            g_crc32Tables[3][pCursor[4]] ^
            g_crc32Tables[2][pCursor[5]] ^
            g_crc32Tables[1][pCursor[6]] ^
            g_crc32Tables[0][pCursor[7]];
        pCursor += 8u;
        cbRemaining -= 8u;
    }

    while ( cbRemaining > 0u ) {
        const u8 nTableIndex = static_cast<u8>( state ^ *pCursor );
        state = g_crc32Tables[0][nTableIndex] ^ ( state >> 8u );
        ++pCursor;
        --cbRemaining;
    }
    return state;
}

crc32_t ChecksumCRC32_Finalize( crc32_t state ) noexcept
{
    return state ^ CY_CRC32_FINAL_XOR;
}

crc32_t ChecksumCRC32_Data( binary_block_t data ) noexcept
{
    return ChecksumCRC32_Finalize(
        ChecksumCRC32_Update( CY_CRC32_INITIAL, data ) );
}

} // namespace cypher::common
