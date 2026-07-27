//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Stats.cpp
//  Purpose: Implements the fixed-capacity Tier0 runtime-statistics registry.
//  Details: Registration copies metadata into stable storage. Hot paths update
//           values by ID without allocating, while tools can enumerate snapshots.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//  - Reworked into a fixed-capacity registry on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stats.h"

#include <cstring>
#include <mutex>

namespace cypher::common
{
namespace
{

struct stat_record_t {
    char szName[CY_STAT_NAME_MAX];
    char szCategory[CY_STAT_CATEGORY_MAX];
    char szDescription[CY_STAT_DESCRIPTION_MAX];
    stat_value_t value;
};

std::mutex g_statsMutex;
stat_record_t g_statRecords[CY_STATS_MAX_COUNT] = {};
usize g_statCount = 0u;
u64 g_droppedRegistrations = 0u;

bool_t Stats_StringFits(
    const char *pszValue,
    usize cchCapacity,
    bool_t allowEmpty ) noexcept
{
    if ( pszValue == nullptr ) {
        return allowEmpty;
    }

    usize i = 0u;
    while ( i < cchCapacity && pszValue[i] != '\0' ) {
        ++i;
    }
    return i < cchCapacity && ( allowEmpty || i != 0u );
}

void Stats_CopyString(
    char *pszDst,
    usize cchDst,
    const char *pszSrc ) noexcept
{
    const char *pszRead = pszSrc != nullptr ? pszSrc : "";
    usize i = 0u;
    while ( i + 1u < cchDst && pszRead[i] != '\0' ) {
        pszDst[i] = pszRead[i];
        ++i;
    }
    pszDst[i] = '\0';
}

stat_value_t Stats_MakeValue( stat_value_type_t type ) noexcept
{
    stat_value_t value{};
    value.type = type;
    switch ( type ) {
        case stat_value_type_t::I64:
            value.i64Value = 0;
            break;
        case stat_value_type_t::U64:
            value.u64Value = 0u;
            break;
        case stat_value_type_t::F64:
            value.f64Value = 0.0;
            break;
    }
    return value;
}

bool_t Stats_IsValidType( stat_value_type_t type ) noexcept
{
    switch ( type ) {
        case stat_value_type_t::I64:
        case stat_value_type_t::U64:
        case stat_value_type_t::F64:
            return CY_TRUE;
    }
    return CY_FALSE;
}

usize Stats_FindIndexLocked( const char *pszName ) noexcept
{
    if ( pszName == nullptr || pszName[0] == '\0' ) {
        return CY_USIZE_MAX;
    }

    for ( usize i = 0u; i < g_statCount; ++i ) {
        if ( std::strcmp( g_statRecords[i].szName, pszName ) == 0 ) {
            return i;
        }
    }
    return CY_USIZE_MAX;
}

stat_id_t Stats_IndexToId( usize nIndex ) noexcept
{
    return static_cast<stat_id_t>( nIndex + 1u );
}

usize Stats_IdToIndex( stat_id_t id ) noexcept
{
    if ( id == CY_STAT_ID_INVALID ) {
        return CY_USIZE_MAX;
    }

    const usize nIndex = static_cast<usize>( id - 1u );
    return nIndex < g_statCount ? nIndex : CY_USIZE_MAX;
}

bool_t Stats_ValidateDescriptor( const stat_desc_t &desc ) noexcept
{
    return Stats_IsValidType( desc.type ) &&
           Stats_StringFits( desc.pszName, CY_STAT_NAME_MAX, CY_FALSE ) &&
           Stats_StringFits(
               desc.pszCategory,
               CY_STAT_CATEGORY_MAX,
               CY_TRUE ) &&
           Stats_StringFits(
               desc.pszDescription,
               CY_STAT_DESCRIPTION_MAX,
               CY_TRUE );
}

} // namespace

bool_t Cy_StatsRegister(
    const stat_desc_t &desc,
    stat_id_t *pOutId ) noexcept
{
    if ( pOutId != nullptr ) {
        *pOutId = CY_STAT_ID_INVALID;
    }
    if ( !Stats_ValidateDescriptor( desc ) ) {
        return CY_FALSE;
    }

    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nExisting = Stats_FindIndexLocked( desc.pszName );
        if ( nExisting != CY_USIZE_MAX ) {
            stat_record_t &record = g_statRecords[nExisting];
            if ( record.value.type != desc.type ) {
                return CY_FALSE;
            }

            Stats_CopyString(
                record.szCategory,
                CY_STAT_CATEGORY_MAX,
                desc.pszCategory );
            Stats_CopyString(
                record.szDescription,
                CY_STAT_DESCRIPTION_MAX,
                desc.pszDescription );
            if ( pOutId != nullptr ) {
                *pOutId = Stats_IndexToId( nExisting );
            }
            return CY_TRUE;
        }

        if ( g_statCount >= CY_STATS_MAX_COUNT ) {
            if ( g_droppedRegistrations != CY_U64_MAX ) {
                ++g_droppedRegistrations;
            }
            return CY_FALSE;
        }

        stat_record_t &record = g_statRecords[g_statCount];
        record = {};
        Stats_CopyString( record.szName, CY_STAT_NAME_MAX, desc.pszName );
        Stats_CopyString(
            record.szCategory,
            CY_STAT_CATEGORY_MAX,
            desc.pszCategory );
        Stats_CopyString(
            record.szDescription,
            CY_STAT_DESCRIPTION_MAX,
            desc.pszDescription );
        record.value = Stats_MakeValue( desc.type );

        const stat_id_t id = Stats_IndexToId( g_statCount );
        ++g_statCount;
        if ( pOutId != nullptr ) {
            *pOutId = id;
        }
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

stat_id_t Cy_StatsFind( const char *pszName ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_FindIndexLocked( pszName );
        return nIndex != CY_USIZE_MAX ?
            Stats_IndexToId( nIndex ) :
            CY_STAT_ID_INVALID;
    } catch ( ... ) {
        return CY_STAT_ID_INVALID;
    }
}

bool_t Cy_StatsSetI64( stat_id_t id, i64 value ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::I64 ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.i64Value = value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsSetU64( stat_id_t id, u64 value ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::U64 ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.u64Value = value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsSetF64( stat_id_t id, f64 value ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::F64 ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.f64Value = value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsAddI64( stat_id_t id, i64 delta ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::I64 ) {
            return CY_FALSE;
        }

        const i64 current = g_statRecords[nIndex].value.i64Value;
        if ( ( delta > 0 && current > CY_I64_MAX - delta ) ||
             ( delta < 0 && current < CY_I64_MIN - delta ) ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.i64Value = current + delta;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsAddU64( stat_id_t id, u64 delta ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::U64 ) {
            return CY_FALSE;
        }

        const u64 current = g_statRecords[nIndex].value.u64Value;
        if ( current > CY_U64_MAX - delta ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.u64Value = current + delta;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsAddF64( stat_id_t id, f64 delta ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ||
             g_statRecords[nIndex].value.type != stat_value_type_t::F64 ) {
            return CY_FALSE;
        }
        g_statRecords[nIndex].value.f64Value += delta;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsGet(
    stat_id_t id,
    stat_value_t *pOutValue ) noexcept
{
    if ( pOutValue == nullptr ) {
        return CY_FALSE;
    }
    *pOutValue = {};

    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_IdToIndex( id );
        if ( nIndex == CY_USIZE_MAX ) {
            return CY_FALSE;
        }
        *pOutValue = g_statRecords[nIndex].value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsGetByName(
    const char *pszName,
    stat_value_t *pOutValue ) noexcept
{
    if ( pOutValue == nullptr ) {
        return CY_FALSE;
    }
    *pOutValue = {};

    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        const usize nIndex = Stats_FindIndexLocked( pszName );
        if ( nIndex == CY_USIZE_MAX ) {
            return CY_FALSE;
        }
        *pOutValue = g_statRecords[nIndex].value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_StatsGetSnapshot(
    usize nIndex,
    stat_snapshot_t *pOutSnapshot ) noexcept
{
    if ( pOutSnapshot == nullptr ) {
        return CY_FALSE;
    }
    *pOutSnapshot = {};

    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        if ( nIndex >= g_statCount ) {
            return CY_FALSE;
        }

        const stat_record_t &record = g_statRecords[nIndex];
        pOutSnapshot->id = Stats_IndexToId( nIndex );
        Stats_CopyString(
            pOutSnapshot->szName,
            CY_STAT_NAME_MAX,
            record.szName );
        Stats_CopyString(
            pOutSnapshot->szCategory,
            CY_STAT_CATEGORY_MAX,
            record.szCategory );
        Stats_CopyString(
            pOutSnapshot->szDescription,
            CY_STAT_DESCRIPTION_MAX,
            record.szDescription );
        pOutSnapshot->value = record.value;
        return CY_TRUE;
    } catch ( ... ) {
        return CY_FALSE;
    }
}

stats_registry_info_t Cy_StatsGetRegistryInfo() noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        return {
            g_statCount,
            CY_STATS_MAX_COUNT,
            g_droppedRegistrations
        };
    } catch ( ... ) {
        return { 0u, CY_STATS_MAX_COUNT, 0u };
    }
}

void Cy_StatsResetValues() noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        for ( usize i = 0u; i < g_statCount; ++i ) {
            g_statRecords[i].value =
                Stats_MakeValue( g_statRecords[i].value.type );
        }
    } catch ( ... ) {
        return;
    }
}

void Cy_StatsClearRegistry() noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_statsMutex );
        for ( usize i = 0u; i < g_statCount; ++i ) {
            g_statRecords[i] = {};
        }
        g_statCount = 0u;
        g_droppedRegistrations = 0u;
    } catch ( ... ) {
        return;
    }
}

} // namespace cypher::common
