//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOptionSet.cpp
//  Purpose: Implements deterministic caller-owned tool option resolution.
//  Details: Scalar values replace weaker sources. Repeatable values retain all
//           occurrences from only the strongest source so project defaults do
//           not leak into an explicit command-line replacement list.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolOptionSet.h"

namespace cypher::common
{
namespace
{

bool_t OptionSetIsStructurallyValid( const tool_option_set_t *pSet ) noexcept
{
    // Values and every referenced descriptor/string remain caller-owned.
    return pSet != nullptr &&
           pSet->nCount <= pSet->nCapacity &&
           ( pSet->nCapacity == 0u || pSet->pValues != nullptr );
}

bool_t OptionValueMatches(
    const tool_option_value_t &value,
    string_view_t name ) noexcept
{
    return value.pDescriptor != nullptr &&
           StringView_Equals( value.pDescriptor->name, name );
}

void RemoveNamedValues(
    tool_option_set_t *pSet,
    string_view_t name ) noexcept
{
    // Compact in place while preserving the relative order of unrelated values.
    usize iWrite = 0u;
    for ( usize iRead = 0u; iRead < pSet->nCount; ++iRead ) {
        if ( OptionValueMatches( pSet->pValues[iRead], name ) ) {
            continue;
        }
        if ( iWrite != iRead ) {
            pSet->pValues[iWrite] = pSet->pValues[iRead];
        }
        ++iWrite;
    }
    pSet->nCount = iWrite;
}

tool_status_t AppendValue(
    tool_option_set_t *pSet,
    const tool_option_desc_t *pDescriptor,
    string_view_t value,
    tool_option_source_t source,
    u32 nOccurrence ) noexcept
{
    if ( pSet->nCount == pSet->nCapacity ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    // nOccurrence is one-based and records order among values at one source.
    pSet->pValues[pSet->nCount] = {
        pDescriptor,
        value,
        source,
        nOccurrence,
        CY_TRUE
    };
    ++pSet->nCount;
    return tool_status_t::OK;
}

} // namespace

tool_status_t ToolOptionSet_Init(
    tool_option_set_t *pSet,
    tool_option_value_t *pStorage,
    usize nCapacity ) noexcept
{
    if ( pSet == nullptr || ( nCapacity != 0u && pStorage == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *pSet = { pStorage, 0u, nCapacity };
    return tool_status_t::OK;
}

void ToolOptionSet_Clear( tool_option_set_t *pSet ) noexcept
{
    if ( pSet != nullptr ) {
        pSet->nCount = 0u;
    }
}

tool_status_t ToolOptionSet_Resolve(
    tool_option_set_t *pSet,
    const tool_option_desc_t *pDescriptor,
    string_view_t value,
    tool_option_source_t source ) noexcept
{
    if ( !OptionSetIsStructurallyValid( pSet ) || pDescriptor == nullptr ||
         source < tool_option_source_t::DEFAULT_VALUE ||
         source > tool_option_source_t::COMMAND_LINE ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    const tool_status_t descriptorStatus =
        ToolOption_CheckDescriptor( *pDescriptor );
    if ( ToolStatus_Failed( descriptorStatus ) ) {
        return descriptorStatus;
    }
    const tool_status_t valueStatus =
        ToolOption_ValidateValue( *pDescriptor, value );
    if ( ToolStatus_Failed( valueStatus ) ) {
        return valueStatus;
    }

    // All existing occurrences of a repeatable option must come from one source.
    // Mixed-source storage indicates state corruption rather than precedence.
    tool_option_value_t *pFirst = nullptr;
    tool_option_source_t strongestSource = tool_option_source_t::DEFAULT_VALUE;
    usize nMatching = 0u;
    for ( usize i = 0u; i < pSet->nCount; ++i ) {
        tool_option_value_t &existing = pSet->pValues[i];
        if ( OptionValueMatches( existing, pDescriptor->name ) ) {
            if ( existing.pDescriptor != pDescriptor ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
            if ( pFirst == nullptr ) {
                pFirst = &existing;
                strongestSource = existing.source;
            } else if ( existing.source != strongestSource ) {
                return tool_status_t::INVALID_STATE;
            }
            ++nMatching;
        }
    }

    if ( pFirst == nullptr ) {
        return AppendValue( pSet, pDescriptor, value, source, 1u );
    }

    if ( source < strongestSource ) {
        // A weaker layer cannot override an already resolved explicit value.
        return tool_status_t::OK;
    }

    const bool_t bRepeatable =
        ( pDescriptor->flags & TOOL_OPTION_FLAG_REPEATABLE ) != 0u;
    if ( !bRepeatable ) {
        // Scalars replace in place so descriptor order remains stable.
        pFirst->value = value;
        pFirst->source = source;
        pFirst->nOccurrence = 1u;
        pFirst->bPresent = CY_TRUE;
        return tool_status_t::OK;
    }

    if ( source > strongestSource ) {
        // A stronger repeatable source replaces the entire weaker list; merging
        // defaults with command-line lists produces surprising tool invocations.
        RemoveNamedValues( pSet, pDescriptor->name );
        return AppendValue( pSet, pDescriptor, value, source, 1u );
    }
    if ( nMatching >= static_cast<usize>( CY_U32_MAX ) ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    return AppendValue(
        pSet,
        pDescriptor,
        value,
        source,
        static_cast<u32>( nMatching + 1u ) );
}

const tool_option_value_t *ToolOptionSet_Find(
    const tool_option_set_t *pSet,
    string_view_t name ) noexcept
{
    if ( !OptionSetIsStructurallyValid( pSet ) ||
         !StringView_IsValid( name ) ) {
        return nullptr;
    }

    for ( usize i = 0u; i < pSet->nCount; ++i ) {
        const tool_option_value_t &value = pSet->pValues[i];
        if ( value.pDescriptor != nullptr &&
             StringView_Equals( value.pDescriptor->name, name ) ) {
            return &value;
        }
    }
    return nullptr;
}

usize ToolOptionSet_CountValues(
    const tool_option_set_t *pSet,
    string_view_t name ) noexcept
{
    if ( !OptionSetIsStructurallyValid( pSet ) ||
         !StringView_IsValid( name ) ) {
        return 0u;
    }

    usize nValues = 0u;
    for ( usize i = 0u; i < pSet->nCount; ++i ) {
        if ( OptionValueMatches( pSet->pValues[i], name ) ) {
            ++nValues;
        }
    }
    return nValues;
}

const tool_option_value_t *ToolOptionSet_FindAt(
    const tool_option_set_t *pSet,
    string_view_t name,
    usize iOccurrence ) noexcept
{
    if ( !OptionSetIsStructurallyValid( pSet ) ||
         !StringView_IsValid( name ) ) {
        return nullptr;
    }

    usize iMatch = 0u;
    for ( usize i = 0u; i < pSet->nCount; ++i ) {
        if ( OptionValueMatches( pSet->pValues[i], name ) ) {
            if ( iMatch == iOccurrence ) {
                return &pSet->pValues[i];
            }
            ++iMatch;
        }
    }
    return nullptr;
}

} // namespace cypher::common
