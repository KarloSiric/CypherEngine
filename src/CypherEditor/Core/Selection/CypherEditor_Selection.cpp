//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Selection.cpp
//  Purpose: Implements the sorted ID selection.
//  Details: Sorted storage makes membership a binary search, which views
//           call for every object on every repaint.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Selection.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

#include <algorithm>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

CYPHER_NODISCARD usize LowerBound( const editor_selection_t &selection, u64 id ) noexcept
{
    const u64 *pBegin = selection.ids.pData;
    const u64 *pEnd = pBegin + selection.ids.nCount;
    return static_cast<usize>( std::lower_bound( pBegin, pEnd, id ) - pBegin );
}

CYPHER_NODISCARD bool_t Changed( editor_selection_t *pSelection ) noexcept
{
    ++pSelection->revision;
    return CY_TRUE;
}

} // namespace

bool_t EditorSelection_Init( editor_selection_t *pSelection, const allocator_t *pAllocator ) noexcept
{
    CY_ASSERT( pSelection != nullptr && pAllocator != nullptr );
    return pSelection != nullptr && pAllocator != nullptr && Vector_Init( &pSelection->ids, pAllocator );
}

void EditorSelection_Shutdown( editor_selection_t *pSelection ) noexcept
{
    if ( pSelection == nullptr ) { return; }
    Vector_Shutdown( &pSelection->ids );
}

bool_t EditorSelection_Apply( editor_selection_t *pSelection, u64 id, editor_select_mode_t mode ) noexcept
{
    CY_ASSERT( pSelection != nullptr );
    const usize iAt = LowerBound( *pSelection, id );
    const bool_t bSelected = iAt < pSelection->ids.nCount && pSelection->ids.pData[iAt] == id;
    switch ( mode ) {
        case EDITOR_SELECT_REPLACE:
            if ( id == 0u ) { return EditorSelection_Clear( pSelection ); }
            if ( bSelected && pSelection->ids.nCount == 1u ) { return CY_FALSE; }
            return EditorSelection_Set( pSelection, &id, 1u );
        case EDITOR_SELECT_TOGGLE:
            if ( id == 0u ) { return CY_FALSE; }
            if ( bSelected ) {
                Vector_Erase( &pSelection->ids, iAt );
                return Changed( pSelection );
            }
            return Vector_Insert( &pSelection->ids, iAt, id ) && Changed( pSelection );
        case EDITOR_SELECT_ADD:
            if ( id == 0u || bSelected ) { return CY_FALSE; }
            return Vector_Insert( &pSelection->ids, iAt, id ) && Changed( pSelection );
        case EDITOR_SELECT_REMOVE:
            if ( !bSelected ) { return CY_FALSE; }
            Vector_Erase( &pSelection->ids, iAt );
            return Changed( pSelection );
    }
    return CY_FALSE;
}

bool_t EditorSelection_Set( editor_selection_t *pSelection, const u64 *pIds, usize nIds ) noexcept
{
    CY_ASSERT( pSelection != nullptr && ( pIds != nullptr || nIds == 0u ) );
    // Build the new set aside so a failed allocation leaves the old one.
    vector_t<u64> next{};
    if ( !Vector_Init( &next, pSelection->ids.pAllocator, nIds ) ) { return CY_FALSE; }
    for ( usize i = 0u; i < nIds; ++i ) {
        if ( pIds[i] != 0u && !Vector_PushBack( &next, pIds[i] ) ) { return CY_FALSE; }
    }
    u64 *pBegin = next.pData;
    u64 *pEnd = pBegin + next.nCount;
    std::sort( pBegin, pEnd );
    const usize nUnique = static_cast<usize>( std::unique( pBegin, pEnd ) - pBegin );
    const bool_t bSame = nUnique == pSelection->ids.nCount && std::equal( pBegin, pBegin + nUnique, pSelection->ids.pData );
    if ( bSame ) { return CY_FALSE; }
    const bool_t bResized = Vector_Resize( &next, nUnique ); // Shrinking never allocates.
    CY_ASSERT( bResized );
    ( void )bResized;
    Vector_Shutdown( &pSelection->ids ); // Vector_Move requires an empty destination.
    Vector_Move( &pSelection->ids, &next );
    return Changed( pSelection );
}

bool_t EditorSelection_Clear( editor_selection_t *pSelection ) noexcept
{
    CY_ASSERT( pSelection != nullptr );
    if ( pSelection->ids.nCount == 0u ) { return CY_FALSE; }
    Vector_Clear( &pSelection->ids );
    return Changed( pSelection );
}

bool_t EditorSelection_Prune( editor_selection_t *pSelection, bool_t ( *pfnExists )( void *pContext, u64 id ) noexcept, void *pContext ) noexcept
{
    CY_ASSERT( pSelection != nullptr && pfnExists != nullptr );
    usize nKept = 0u;
    for ( usize i = 0u; i < pSelection->ids.nCount; ++i ) {
        const u64 id = pSelection->ids.pData[i];
        if ( pfnExists( pContext, id ) ) { pSelection->ids.pData[nKept++] = id; }
    }
    if ( nKept == pSelection->ids.nCount ) { return CY_FALSE; }
    const bool_t bResized = Vector_Resize( &pSelection->ids, nKept );
    CY_ASSERT( bResized );
    ( void )bResized;
    return Changed( pSelection );
}

bool_t EditorSelection_Contains( const editor_selection_t *pSelection, u64 id ) noexcept
{
    CY_ASSERT( pSelection != nullptr );
    const usize iAt = LowerBound( *pSelection, id );
    return iAt < pSelection->ids.nCount && pSelection->ids.pData[iAt] == id;
}

usize EditorSelection_Count( const editor_selection_t *pSelection ) noexcept
{
    CY_ASSERT( pSelection != nullptr );
    return pSelection->ids.nCount;
}

u64 EditorSelection_At( const editor_selection_t *pSelection, usize iIndex ) noexcept
{
    CY_ASSERT( pSelection != nullptr && iIndex < pSelection->ids.nCount );
    return iIndex < pSelection->ids.nCount ? pSelection->ids.pData[iIndex] : 0u;
}

} // namespace cypher::editor
