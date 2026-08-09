//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ChecksumCRC64.cpp
//  Purpose: Implements CRC-64/ECMA-182 checksums.
//  Details: This is the non-reflected ECMA-182 variant with an all-zero initial
//           state and no final XOR, matching the public format contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ChecksumCRC64.h"

#include <array>

namespace cypher::common
{

namespace
{

using crc64_table_t = std::array<crc64_t, 256u>;
using crc64_slice_tables_t = std::array<crc64_table_t, 8u>;

constexpr crc64_slice_tables_t BuildCRC64Tables() noexcept
{
    crc64_slice_tables_t tables{};
    for ( usize iEntry = 0u; iEntry < tables[0].size(); ++iEntry ) {
        crc64_t nValue = static_cast<crc64_t>( iEntry ) << 56u;
        for ( u32 iBit = 0u; iBit < 8u; ++iBit ) {
            const bool_t bTopBitSet = ( nValue & 0x8000000000000000ull ) != 0u;
            nValue <<= 1u;
            if ( bTopBitSet ) {
                nValue ^= CY_CRC64_POLYNOMIAL;
            }
        }
        tables[0][iEntry] = nValue;
    }

    for ( usize iSlice = 1u; iSlice < tables.size(); ++iSlice ) {
        for ( usize iEntry = 0u; iEntry < tables[iSlice].size(); ++iEntry ) {
            const crc64_t nPrevious = tables[iSlice - 1u][iEntry];
            tables[iSlice][iEntry] =
                ( nPrevious << 8u ) ^ tables[0][nPrevious >> 56u];
        }
    }
    return tables;
}

constexpr auto g_crc64Tables = BuildCRC64Tables();

} // namespace

crc64_t ChecksumCRC64_Update( crc64_t state, binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "CRC-64 requires a valid binary block." );
    if ( !bValidData ) {
        return state;
    }

    const byte *pCursor = data.pData;
    usize cbRemaining = data.cbSize;
    while ( cbRemaining >= 8u ) {
        const crc64_t nBlock = state ^
            ( static_cast<crc64_t>( pCursor[0] ) << 56u ) ^
            ( static_cast<crc64_t>( pCursor[1] ) << 48u ) ^
            ( static_cast<crc64_t>( pCursor[2] ) << 40u ) ^
            ( static_cast<crc64_t>( pCursor[3] ) << 32u ) ^
            ( static_cast<crc64_t>( pCursor[4] ) << 24u ) ^
            ( static_cast<crc64_t>( pCursor[5] ) << 16u ) ^
            ( static_cast<crc64_t>( pCursor[6] ) << 8u ) ^
            static_cast<crc64_t>( pCursor[7] );

        state =
            g_crc64Tables[7][( nBlock >> 56u ) & 0xFFu] ^
            g_crc64Tables[6][( nBlock >> 48u ) & 0xFFu] ^
            g_crc64Tables[5][( nBlock >> 40u ) & 0xFFu] ^
            g_crc64Tables[4][( nBlock >> 32u ) & 0xFFu] ^
            g_crc64Tables[3][( nBlock >> 24u ) & 0xFFu] ^
            g_crc64Tables[2][( nBlock >> 16u ) & 0xFFu] ^
            g_crc64Tables[1][( nBlock >> 8u ) & 0xFFu] ^
            g_crc64Tables[0][nBlock & 0xFFu];
        pCursor += 8u;
        cbRemaining -= 8u;
    }

    while ( cbRemaining > 0u ) {
        const u8 nTableIndex = static_cast<u8>(
            ( state >> 56u ) ^ *pCursor );
        state = g_crc64Tables[0][nTableIndex] ^ ( state << 8u );
        ++pCursor;
        --cbRemaining;
    }
    return state;
}

crc64_t ChecksumCRC64_Finalize( crc64_t state ) noexcept
{
    return state ^ CY_CRC64_FINAL_XOR;
}

crc64_t ChecksumCRC64_Data( binary_block_t data ) noexcept
{
    return ChecksumCRC64_Finalize(
        ChecksumCRC64_Update( CY_CRC64_INITIAL, data ) );
}

} // namespace cypher::common
