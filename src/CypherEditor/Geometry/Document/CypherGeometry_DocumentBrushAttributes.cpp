//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentBrushAttributes.cpp
//  Purpose: Implements brush record tables for the geometry document.
//  Details: Lookup and the core add live in CypherGeometry_Document.cpp,
//           next to the brush index and identity code they share; this file
//           holds the table helpers and the brush_source_t conveniences.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentBrushAttributes.h"

#include <new>

namespace cypher::editor::geometry
{

geometry_brush_side_attribute_store_t *BrushAttributes_Allocate( const common::allocator_t *pAllocator ) noexcept
{
    void *pMemory = common::Allocator_AllocateZeroed( pAllocator, sizeof( geometry_brush_side_attribute_store_t ),
                                                      alignof( geometry_brush_side_attribute_store_t ) );
    return pMemory != nullptr ? new ( pMemory ) geometry_brush_side_attribute_store_t{} : nullptr;
}

void BrushAttributes_Free( const common::allocator_t *pAllocator, geometry_brush_side_attribute_store_t *pStore ) noexcept
{
    if ( pStore == nullptr ) { return; }
    BrushSideAttributeStore_Shutdown( pStore );
    pStore->~geometry_brush_side_attribute_store_t();
    common::Allocator_Free( pAllocator, pStore, sizeof( geometry_brush_side_attribute_store_t ),
                            alignof( geometry_brush_side_attribute_store_t ) );
}

bool BrushAttributes_Covers( const brush_solid_t *pSolid, const geometry_brush_side_attribute_store_t *pStore ) noexcept
{
    if ( pSolid == nullptr || pStore == nullptr ) { return false; }
    const common::usize cRecords = BrushSideAttributeStore_Count( pStore );
    for ( common::usize i = 0u; i < pSolid->sides.nCount; ++i ) {
        if ( pSolid->sides.pData[i].iAttributeIndex >= cRecords ) { return false; }
    }
    return true;
}

geometry_status_t BrushAttributes_TryBuildCovering(
    const brush_solid_t *pSolid,
    const geometry_brush_side_attribute_store_t *pBase,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_brush_side_attribute_store_t *pOut ) noexcept
{
    if ( pSolid == nullptr || pAllocator == nullptr || pOut == nullptr || pOut->records.pAllocator != nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    common::usize cNeeded = 0u;
    for ( common::usize i = 0u; i < pSolid->sides.nCount; ++i ) {
        const common::usize need = static_cast<common::usize>( pSolid->sides.pData[i].iAttributeIndex ) + 1u;
        cNeeded = need > cNeeded ? need : cNeeded;
    }
    const common::usize cBase = pBase != nullptr ? BrushSideAttributeStore_Count( pBase ) : 0u;
    const common::usize cFinal = cNeeded > cBase ? cNeeded : cBase;
    if ( static_cast<common::u64>( cFinal ) > policy.limits.cBrushSidesPerBrushMax ) { return geometry_status_t::LIMIT_EXCEEDED; }

    geometry_brush_side_attribute_store_t built{};
    geometry_status_t st = BrushSideAttributeStore_Init( &built, pAllocator );
    if ( st == geometry_status_t::OK && pBase != nullptr ) { st = BrushSideAttributeStore_Validate( pBase, policy ); }
    if ( st == geometry_status_t::OK && pBase != nullptr ) { st = BrushSideAttributeStore_TryCopyFrom( &built, pBase, policy.limits ); }
    if ( st == geometry_status_t::OK ) { st = BrushSideAttributeStore_TryReserve( &built, policy.limits, cFinal ); }
    const geometry_brush_side_attributes_t fallback = BrushSideAttributes_MakeDefault();
    while ( st == geometry_status_t::OK && BrushSideAttributeStore_Count( &built ) < cFinal ) {
        st = BrushSideAttributeStore_TryAppend( &built, policy, fallback, nullptr );
    }
    if ( st != geometry_status_t::OK ) {
        BrushSideAttributeStore_Shutdown( &built );
        return st;
    }
    common::Vector_Move( &pOut->records, &built.records );
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_TryAddBrushSource( geometry_document_t *pDocument, const brush_source_t *pSource ) noexcept
{
    if ( pSource == nullptr || !BrushSource_IsInitialized( pSource ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    return GeometryDocument_TryAddBrushWithAttributes( pDocument, &pSource->solid, &pSource->attributes );
}

geometry_status_t GeometryDocument_TryCopyBrushSource(
    const geometry_document_t *pDocument,
    geometry_source_id_t brushId,
    const common::allocator_t *pAllocator,
    brush_source_t *pOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const brush_solid_t *pSolid = GeometryDocument_FindBrush( pDocument, brushId );
    const geometry_brush_side_attribute_store_t *pStore = GeometryDocument_FindBrushAttributes( pDocument, brushId );
    if ( pSolid == nullptr || pStore == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    return BrushSource_TryBuildWithStore( pSolid, pStore, pAllocator, pDocument->policy, pOut );
}

geometry_status_t GeometryDocument_ValidateBrushAttributes( const geometry_document_t *pDocument ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( pDocument->brushAttributes.nCount != pDocument->brushes.nCount ) { return geometry_status_t::CORRUPT_STATE; }
    for ( common::usize i = 0u; i < pDocument->brushes.nCount; ++i ) {
        const geometry_brush_side_attribute_store_t *pStore = pDocument->brushAttributes.pData[i];
        if ( pStore == nullptr || !BrushAttributes_Covers( pDocument->brushes.pData[i], pStore ) ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        const geometry_status_t st = BrushSideAttributeStore_Validate( pStore, pDocument->policy );
        if ( st != geometry_status_t::OK ) { return st; }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
