//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_InstanceLog.cpp
//  Purpose: Implements bounded per-instance diagnostic history.
//  Details: Records form a fixed-capacity ring. Text is copied per record and the
//           oldest records are evicted to enforce both count and byte budgets.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_InstanceLog.h"

namespace cypher::common
{

namespace
{

struct instance_log_entry_t {
    char *pText{ nullptr };                    // One allocation: category\0message\0.
    usize cchCategory{ 0u };                   // Category bytes, excluding its terminator.
    usize cchMessage{ 0u };                    // Message bytes, excluding its terminator.
    u64 nTimestampTicks{ 0u };                 // Monotonic timer sample captured on insertion.
    log_level_t level{ log_level_t::Info };    // Severity recorded with this message.
};

bool_t LogTextIsValid( string_view_t text ) noexcept
{
    if ( !StringView_IsValid( text ) ) {
        return CY_FALSE;
    }
    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( text.pData[iByte] == '\0' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

struct instance_log_t {
    const allocator_t *pAllocator{ nullptr };  // Allocator owns the log and every text record.
    instance_log_entry_t *pEntries{ nullptr }; // Fixed-size ring allocated when the log is created.
    usize nCapacity{ 0u };                     // Maximum number of resident records.
    usize nCount{ 0u };                        // Number of live entries in the ring.
    usize iHead{ 0u };                         // Physical index of the oldest live record.
    usize cbText{ 0u };                        // Payload bytes currently charged to the text budget.
    usize cbMaxText{ 0u };                     // Maximum combined category and message bytes.
};

namespace
{

void InstanceLog_EvictOldest( instance_log_t *pLog ) noexcept
{
    if ( pLog->nCount == 0u ) {
        return;
    }
    instance_log_entry_t &entry = pLog->pEntries[pLog->iHead];
    const usize cbPayload = entry.cchCategory + entry.cchMessage;
    const usize cbAllocation = cbPayload + 2u; // Account for both stored NUL terminators.
    Allocator_Free(
        pLog->pAllocator,
        entry.pText,
        cbAllocation,
        alignof( char ) );
    pLog->cbText -= cbPayload;
    entry = {};
    pLog->iHead = ( pLog->iHead + 1u ) % pLog->nCapacity;
    --pLog->nCount;
    if ( pLog->nCount == 0u ) {
        pLog->iHead = 0u;
    }
}

} // namespace

instance_log_t *InstanceLog_Create(
    const instance_log_desc_t &desc ) noexcept
{
    if ( !Allocator_IsValid( desc.pAllocator ) || desc.nMaxRecords == 0u ||
         desc.cbMaxText == 0u ) {
        return nullptr;
    }
    usize cbEntries = 0u;
    if ( !Cy_TryArrayByteCount<instance_log_entry_t>(
             desc.nMaxRecords,
             cbEntries ) ) {
        return nullptr;
    }

    auto *pLog = static_cast<instance_log_t *>( Allocator_AllocateZeroed(
        desc.pAllocator,
        sizeof( instance_log_t ),
        alignof( instance_log_t ) ) );
    if ( pLog == nullptr ) {
        return nullptr;
    }
    pLog->pEntries = static_cast<instance_log_entry_t *>( Allocator_AllocateZeroed(
        desc.pAllocator,
        cbEntries,
        alignof( instance_log_entry_t ) ) );
    if ( pLog->pEntries == nullptr ) {
        Allocator_Free(
            desc.pAllocator,
            pLog,
            sizeof( instance_log_t ),
            alignof( instance_log_t ) );
        return nullptr;
    }
    pLog->pAllocator = desc.pAllocator;
    pLog->nCapacity = desc.nMaxRecords;
    pLog->cbMaxText = desc.cbMaxText;
    return pLog;
}

void InstanceLog_Destroy( instance_log_t *pLog ) noexcept
{
    if ( pLog == nullptr ) {
        return;
    }
    const allocator_t *pAllocator = pLog->pAllocator;
    const usize cbEntries = pLog->nCapacity * sizeof( instance_log_entry_t );
    InstanceLog_Clear( pLog );
    Allocator_Free(
        pAllocator,
        pLog->pEntries,
        cbEntries,
        alignof( instance_log_entry_t ) );
    Allocator_Free(
        pAllocator,
        pLog,
        sizeof( instance_log_t ),
        alignof( instance_log_t ) );
}

void InstanceLog_Clear( instance_log_t *pLog ) noexcept
{
    if ( pLog == nullptr ) {
        return;
    }
    while ( pLog->nCount > 0u ) {
        InstanceLog_EvictOldest( pLog );
    }
}

bool_t InstanceLog_Add(
    instance_log_t *pLog,
    log_level_t level,
    string_view_t category,
    string_view_t message ) noexcept
{
    if ( pLog == nullptr || level >= log_level_t::Count ||
         !LogTextIsValid( category ) || !LogTextIsValid( message ) ||
         category.cchLength > CY_USIZE_MAX - message.cchLength ) {
        return CY_FALSE;
    }
    const usize cbPayload = category.cchLength + message.cchLength;
    if ( cbPayload > pLog->cbMaxText || cbPayload > CY_USIZE_MAX - 2u ) {
        return CY_FALSE;
    }

    char *pText = static_cast<char *>( Allocator_Allocate(
        pLog->pAllocator,
        cbPayload + 2u,
        alignof( char ) ) );
    if ( pText == nullptr ) {
        return CY_FALSE;
    }
    if ( category.cchLength > 0u ) {
        Cy_MemCopy( pText, category.pData, category.cchLength );
    }
    pText[category.cchLength] = '\0';
    if ( message.cchLength > 0u ) {
        Cy_MemCopy(
            pText + category.cchLength + 1u,
            message.pData,
            message.cchLength );
    }
    pText[cbPayload + 1u] = '\0';

    // A new record must satisfy both independent limits. Eviction always starts
    // at the head, preserving chronological order for every surviving record.
    while ( pLog->nCount == pLog->nCapacity ||
            pLog->cbText > pLog->cbMaxText - cbPayload ) {
        InstanceLog_EvictOldest( pLog );
    }
    const usize iTail = ( pLog->iHead + pLog->nCount ) % pLog->nCapacity;
    pLog->pEntries[iTail] = {
        pText,
        category.cchLength,
        message.cchLength,
        Cy_TimerNowTicks(),
        level
    };
    ++pLog->nCount;
    pLog->cbText += cbPayload;
    return CY_TRUE;
}

usize InstanceLog_Count( const instance_log_t *pLog ) noexcept
{
    return pLog != nullptr ? pLog->nCount : 0u;
}

bool_t InstanceLog_Record(
    const instance_log_t *pLog,
    usize iRecord,
    instance_log_record_t *pRecordOut ) noexcept
{
    if ( pRecordOut == nullptr ) {
        return CY_FALSE;
    }
    *pRecordOut = {};
    if ( pLog == nullptr || iRecord >= pLog->nCount ) {
        return CY_FALSE;
    }
    // Public record indices are chronological; translate them into the ring's
    // wrapped physical storage before exposing non-owning string views.
    const usize iEntry = ( pLog->iHead + iRecord ) % pLog->nCapacity;
    const instance_log_entry_t &entry = pLog->pEntries[iEntry];
    pRecordOut->nTimestampTicks = entry.nTimestampTicks;
    pRecordOut->level = entry.level;
    pRecordOut->category = { entry.pText, entry.cchCategory };
    pRecordOut->message = {
        entry.pText + entry.cchCategory + 1u,
        entry.cchMessage
    };
    return CY_TRUE;
}

} // namespace cypher::common
