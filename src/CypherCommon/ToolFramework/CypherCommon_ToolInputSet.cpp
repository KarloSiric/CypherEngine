//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolInputSet.cpp
//  Purpose: Implements allocation-free tool input-set management.
//  Details: Validation rejects contradictory traversal policy while preserving
//           source order, which is significant for deterministic diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolInputSet.h"

namespace cypher::common
{
namespace
{

bool_t InputSetIsValid( const tool_input_set_t *pSet ) noexcept
{
    return pSet != nullptr &&
           pSet->nCount <= pSet->nCapacity &&
           ( pSet->nCapacity == 0u || pSet->pInputs != nullptr );
}

bool_t FilterIsValid( const path_filter_t &filter ) noexcept
{
    if ( ( filter.nIncludeCount != 0u && filter.pIncludes == nullptr ) ||
         ( filter.nExcludeCount != 0u && filter.pExcludes == nullptr ) ||
         ( filter.flags & ~PATH_MATCH_VALID_FLAGS ) != 0u ) {
        return CY_FALSE;
    }
    for ( usize i = 0u; i < filter.nIncludeCount; ++i ) {
        if ( !StringView_IsValid( filter.pIncludes[i] ) ||
             filter.pIncludes[i].cchLength == 0u ) {
            return CY_FALSE;
        }
    }
    for ( usize i = 0u; i < filter.nExcludeCount; ++i ) {
        if ( !StringView_IsValid( filter.pExcludes[i] ) ||
             filter.pExcludes[i].cchLength == 0u ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace

tool_status_t ToolInput_Validate( const tool_input_t &input ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_INPUT_FLAG_REQUIRED |
        TOOL_INPUT_FLAG_RECURSIVE |
        TOOL_INPUT_FLAG_FOLLOW_SYMLINKS |
        TOOL_INPUT_FLAG_ALLOW_MISSING;

    if ( !StringView_IsValid( input.value ) || input.value.cchLength == 0u ||
         !StringView_IsValid( input.baseDirectory ) ||
         input.kind > tool_input_kind_t::MANIFEST ||
         ( input.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( ( input.flags & TOOL_INPUT_FLAG_REQUIRED ) != 0u &&
         ( input.flags & TOOL_INPUT_FLAG_ALLOW_MISSING ) != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    if ( ( input.flags & TOOL_INPUT_FLAG_RECURSIVE ) != 0u &&
         input.kind != tool_input_kind_t::DIRECTORY &&
         input.kind != tool_input_kind_t::PATTERN ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    if ( ( input.flags & TOOL_INPUT_FLAG_FOLLOW_SYMLINKS ) != 0u &&
         input.kind != tool_input_kind_t::DIRECTORY ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    return tool_status_t::OK;
}

tool_status_t ToolInputSet_Init(
    tool_input_set_t *pSet,
    tool_input_t *pStorage,
    usize nCapacity ) noexcept
{
    if ( pSet == nullptr || ( nCapacity != 0u && pStorage == nullptr ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    *pSet = { pStorage, 0u, nCapacity, {} };
    return tool_status_t::OK;
}

void ToolInputSet_Clear( tool_input_set_t *pSet ) noexcept
{
    if ( pSet != nullptr ) {
        pSet->nCount = 0u;
        pSet->filter = {};
    }
}

tool_status_t ToolInputSet_SetFilter(
    tool_input_set_t *pSet,
    const path_filter_t &filter ) noexcept
{
    if ( !InputSetIsValid( pSet ) || !FilterIsValid( filter ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    pSet->filter = filter;
    return tool_status_t::OK;
}

tool_status_t ToolInputSet_Add(
    tool_input_set_t *pSet,
    const tool_input_t &input ) noexcept
{
    if ( !InputSetIsValid( pSet ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    const tool_status_t status = ToolInput_Validate( input );
    if ( ToolStatus_Failed( status ) ) {
        return status;
    }
    if ( pSet->nCount == pSet->nCapacity ) {
        return tool_status_t::CAPACITY_EXCEEDED;
    }
    pSet->pInputs[pSet->nCount++] = input;
    return tool_status_t::OK;
}

const tool_input_t *ToolInputSet_At(
    const tool_input_set_t *pSet,
    usize iInput ) noexcept
{
    return InputSetIsValid( pSet ) && iInput < pSet->nCount
        ? &pSet->pInputs[iInput]
        : nullptr;
}

bool_t ToolInputSet_AcceptsPath(
    const tool_input_set_t *pSet,
    string_view_t path ) noexcept
{
    return InputSetIsValid( pSet ) && StringView_IsValid( path ) &&
           path.cchLength != 0u && PathMatch_Filter( path, pSet->filter );
}

const char *ToolInput_KindName( tool_input_kind_t kind ) noexcept
{
    switch ( kind ) {
        case tool_input_kind_t::FILE: return "file";
        case tool_input_kind_t::DIRECTORY: return "directory";
        case tool_input_kind_t::PATTERN: return "pattern";
        case tool_input_kind_t::MANIFEST: return "manifest";
    }
    return "unknown";
}

} // namespace cypher::common
