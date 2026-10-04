//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Cell.cpp
//  Purpose: Implements map cells and their chunk-file names.
//  Details: Indices come from floor(position / size) in f64, which is exact
//           for every representable map coordinate at the default sizes; the
//           index limit keeps a runaway coordinate from producing an absurd
//           file name.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Cell.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier1/CypherCommon_StringView.h"

#include <cmath>
#include <cstdio>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

constexpr char kAxisNames[3]{ 'x', 'y', 'z' };

CYPHER_NODISCARD bool_t ReadIndex( const key_value_t *pValue, i64 &indexOut ) noexcept
{
    u64 nUnsigned = 0u;
    if ( KeyValue_GetI64( pValue, &indexOut ) ) {
        return indexOut >= -MAP_CELL_INDEX_LIMIT && indexOut <= MAP_CELL_INDEX_LIMIT;
    }
    if ( KeyValue_GetU64( pValue, &nUnsigned ) && nUnsigned <= static_cast<u64>( MAP_CELL_INDEX_LIMIT ) ) {
        indexOut = static_cast<i64>( nUnsigned );
        return CY_TRUE;
    }
    return CY_FALSE;
}

} // namespace

map_cell_t MapCell_Global() noexcept
{
    return {};
}

map_cell_t MapCell_ForPosition( const map_cell_grid_t &grid, math::vec3d_t position ) noexcept
{
    const f64 components[3]{ position.x, position.y, position.z };
    map_cell_t cell{};
    cell.bGlobal = CY_FALSE;
    for ( u32 iAxis = 0u; iAxis < 3u; ++iAxis ) {
        if ( !( grid.size[iAxis] > 0.0 ) ) {
            continue;
        }
        if ( !std::isfinite( components[iAxis] ) ) {
            return MapCell_Global();
        }
        const f64 flIndex = std::floor( components[iAxis] / grid.size[iAxis] );
        if ( !( flIndex >= -static_cast<f64>( MAP_CELL_INDEX_LIMIT ) &&
                flIndex <= static_cast<f64>( MAP_CELL_INDEX_LIMIT ) ) ) {
            return MapCell_Global();
        }
        cell.axes = static_cast<u8>( cell.axes | ( 1u << iAxis ) );
        cell.index[iAxis] = static_cast<i64>( flIndex );
    }
    // A grid that cuts no axis keeps everything in one chunk per layer.
    return cell.axes != 0u ? cell : MapCell_Global();
}

bool_t MapCell_Equals( const map_cell_t &a, const map_cell_t &b ) noexcept
{
    return MapCell_Compare( a, b ) == 0;
}

i32 MapCell_Compare( const map_cell_t &a, const map_cell_t &b ) noexcept
{
    if ( a.bGlobal != b.bGlobal ) { return a.bGlobal ? -1 : 1; }
    if ( a.bGlobal ) { return 0; }
    if ( a.axes != b.axes ) { return a.axes < b.axes ? -1 : 1; }
    for ( u32 iAxis = 0u; iAxis < 3u; ++iAxis ) {
        if ( ( a.axes & ( 1u << iAxis ) ) != 0u && a.index[iAxis] != b.index[iAxis] ) {
            return a.index[iAxis] < b.index[iAxis] ? -1 : 1;
        }
    }
    return 0;
}

usize MapCell_FormatName( const map_cell_t &cell, char ( &buffer )[MAP_CELL_NAME_CAPACITY] ) noexcept
{
    if ( cell.bGlobal || cell.axes == 0u ) {
        return static_cast<usize>( std::snprintf( buffer, MAP_CELL_NAME_CAPACITY, "global" ) );
    }
    usize cch = 0u;
    for ( u32 iAxis = 0u; iAxis < 3u; ++iAxis ) {
        if ( ( cell.axes & ( 1u << iAxis ) ) == 0u ) {
            continue;
        }
        const int written = std::snprintf( buffer + cch, MAP_CELL_NAME_CAPACITY - cch, "%s%c%lld",
                                           cch == 0u ? "" : "_", kAxisNames[iAxis],
                                           static_cast<long long>( cell.index[iAxis] ) );
        CY_ASSERT( written > 0 && static_cast<usize>( written ) < MAP_CELL_NAME_CAPACITY - cch );
        cch += static_cast<usize>( written );
    }
    return cch;
}

bool_t MapCell_ParseName( string_view_t name, map_cell_t *pCellOut ) noexcept
{
    if ( pCellOut == nullptr || name.pData == nullptr ) { return CY_FALSE; }
    if ( StringView_Equals( name, StringView_FromCString( "global" ) ) ) {
        *pCellOut = MapCell_Global();
        return CY_TRUE;
    }
    map_cell_t cell{};
    cell.bGlobal = CY_FALSE;
    usize iChar = 0u;
    u32 iNextAxis = 0u;
    while ( iChar < name.cchLength ) {
        if ( cell.axes != 0u ) {
            if ( name.pData[iChar] != '_' ) { return CY_FALSE; }
            ++iChar;
        }
        // Axes appear in x, y, z order, each at most once.
        u32 iAxis = iNextAxis;
        while ( iAxis < 3u && ( iChar >= name.cchLength || name.pData[iChar] != kAxisNames[iAxis] ) ) { ++iAxis; }
        if ( iAxis == 3u ) { return CY_FALSE; }
        ++iChar;
        bool_t bNegative = CY_FALSE;
        if ( iChar < name.cchLength && name.pData[iChar] == '-' ) { bNegative = CY_TRUE; ++iChar; }
        const usize iDigits = iChar;
        i64 value = 0;
        while ( iChar < name.cchLength && name.pData[iChar] >= '0' && name.pData[iChar] <= '9' ) {
            value = value * 10 + ( name.pData[iChar] - '0' );
            if ( value > MAP_CELL_INDEX_LIMIT ) { return CY_FALSE; }
            ++iChar;
        }
        if ( iChar == iDigits ) { return CY_FALSE; }
        // "-0" is not canonical; the writer never produces it.
        if ( bNegative && value == 0 ) { return CY_FALSE; }
        cell.index[iAxis] = bNegative ? -value : value;
        cell.axes = static_cast<u8>( cell.axes | ( 1u << iAxis ) );
        iNextAxis = iAxis + 1u;
    }
    if ( cell.axes == 0u ) { return CY_FALSE; }
    *pCellOut = cell;
    return CY_TRUE;
}

bool_t MapCell_Read( const key_value_t *pValue, map_cell_t *pCellOut ) noexcept
{
    if ( pValue == nullptr || pCellOut == nullptr ) { return CY_FALSE; }
    string_view_t text{};
    if ( KeyValue_GetString( pValue, &text ) ) {
        if ( !StringView_Equals( text, StringView_FromCString( "global" ) ) ) { return CY_FALSE; }
        *pCellOut = MapCell_Global();
        return CY_TRUE;
    }
    if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY ) { return CY_FALSE; }
    const usize nAxes = KeyValue_ChildCount( pValue );
    if ( nAxes != 2u && nAxes != 3u ) { return CY_FALSE; }
    map_cell_t cell{};
    cell.bGlobal = CY_FALSE;
    for ( usize iAxis = 0u; iAxis < nAxes; ++iAxis ) {
        if ( !ReadIndex( KeyValue_ChildAt( pValue, iAxis ), cell.index[iAxis] ) ) { return CY_FALSE; }
        cell.axes = static_cast<u8>( cell.axes | ( 1u << iAxis ) );
    }
    *pCellOut = cell;
    return CY_TRUE;
}

bool_t MapCell_Write( key_value_document_t *pDocument, key_value_t *pValue, const map_cell_t &cell ) noexcept
{
    if ( cell.bGlobal || cell.axes == 0u ) {
        return KeyValue_SetString( pDocument, pValue, StringView_FromCString( "global" ) );
    }
    // The array form lists x and y, plus z when cut; a cut z with an uncut
    // axis before it cannot be expressed and never comes from a grid.
    CY_ASSERT_MSG( ( cell.axes & 3u ) == 3u, "Cells cut x and y together" );
    if ( !KeyValue_SetContainerType( pDocument, pValue, key_value_type_t::ARRAY ) ) { return CY_FALSE; }
    const u32 nAxes = ( cell.axes & 4u ) != 0u ? 3u : 2u;
    for ( u32 iAxis = 0u; iAxis < nAxes; ++iAxis ) {
        key_value_t *pIndex = KeyValue_ArrayAppend( pDocument, pValue, key_value_type_t::NULL_VALUE );
        if ( pIndex == nullptr || !KeyValue_SetI64( pDocument, pIndex, cell.index[iAxis] ) ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

bool_t MapCell_MatchesGrid( const map_cell_t &cell, const map_cell_grid_t &grid ) noexcept
{
    u8 gridAxes = 0u;
    for ( u32 iAxis = 0u; iAxis < 3u; ++iAxis ) {
        if ( grid.size[iAxis] > 0.0 ) { gridAxes = static_cast<u8>( gridAxes | ( 1u << iAxis ) ); }
    }
    return cell.bGlobal || cell.axes == gridAxes;
}

} // namespace cypher::editor::map
