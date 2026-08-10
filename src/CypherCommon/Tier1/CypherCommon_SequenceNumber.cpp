//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SequenceNumber.cpp
//  Purpose: Implements modular sequence comparison and acknowledgement windows.
//  Details: Arithmetic avoids implementation-defined signed casts and treats the
//           exact half-range distance as ambiguous rather than newer.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SequenceNumber.h"

namespace cypher::common
{

bool_t Sequence16_IsNewer( u16 left, u16 right ) noexcept
{
    return Sequence16_Distance( right, left ) > 0;
}

bool_t Sequence32_IsNewer( u32 left, u32 right ) noexcept
{
    return Sequence32_Distance( right, left ) > 0;
}

i32 Sequence16_Distance( u16 from, u16 to ) noexcept
{
    const u16 nForward = static_cast<u16>( to - from );
    if ( nForward < 0x8000u ) {
        return static_cast<i32>( nForward );
    }
    return -static_cast<i32>( 0x10000u - static_cast<u32>( nForward ) );
}

i64 Sequence32_Distance( u32 from, u32 to ) noexcept
{
    const u32 nForward = to - from;
    if ( nForward < 0x80000000u ) {
        return static_cast<i64>( nForward );
    }
    return -static_cast<i64>( 0x100000000ull - static_cast<u64>( nForward ) );
}

void SequenceAck32_Reset( sequence_ack32_t *pState ) noexcept
{
    CY_ASSERT_MSG( pState != nullptr, "SequenceAck32_Reset requires state." );
    if ( pState != nullptr ) {
        *pState = {};
    }
}

bool_t SequenceAck32_Record(
    sequence_ack32_t *pState,
    u32 nSequence ) noexcept
{
    CY_ASSERT_MSG( pState != nullptr, "SequenceAck32_Record requires state." );
    if ( pState == nullptr ) {
        return CY_FALSE;
    }
    if ( !pState->bInitialized ) {
        pState->nLatest = nSequence;
        pState->ackBits = 0u;
        pState->bInitialized = CY_TRUE;
        return CY_TRUE;
    }
    if ( nSequence == pState->nLatest ) {
        return CY_FALSE;
    }

    const i64 nForward = Sequence32_Distance( pState->nLatest, nSequence );
    if ( nForward > 0 ) {
        const u64 nAdvance = static_cast<u64>( nForward );
        if ( nAdvance > 32u ) {
            pState->ackBits = 0u;
        } else {
            pState->ackBits <<= static_cast<u32>( nAdvance );
            pState->ackBits |= CYPHER_BIT32( static_cast<u32>( nAdvance - 1u ) );
        }
        pState->nLatest = nSequence;
        return CY_TRUE;
    }

    const i64 nAge = Sequence32_Distance( nSequence, pState->nLatest );
    if ( nAge <= 0 || nAge > 32 ) {
        return CY_FALSE;
    }
    const u32 nMask = CYPHER_BIT32( static_cast<u32>( nAge - 1 ) );
    if ( ( pState->ackBits & nMask ) != 0u ) {
        return CY_FALSE;
    }
    pState->ackBits |= nMask;
    return CY_TRUE;
}

bool_t SequenceAck32_Contains(
    const sequence_ack32_t *pState,
    u32 nSequence ) noexcept
{
    if ( pState == nullptr || !pState->bInitialized ) {
        return CY_FALSE;
    }
    if ( nSequence == pState->nLatest ) {
        return CY_TRUE;
    }
    const i64 nAge = Sequence32_Distance( nSequence, pState->nLatest );
    if ( nAge <= 0 || nAge > 32 ) {
        return CY_FALSE;
    }
    return ( pState->ackBits & CYPHER_BIT32( static_cast<u32>( nAge - 1 ) ) ) != 0u;
}

} // namespace cypher::common
