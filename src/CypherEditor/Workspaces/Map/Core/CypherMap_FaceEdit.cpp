//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Plane and surface-record edits preserve canonical brush identities.
//////////////////////////////////////////////////////////////////////////
#include "CypherMap_FaceEdit.h"

#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"

#include <cmath>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == geo::geometry_status_t::OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

map_status_t FindFace( const map_document_t *pMap, u64 brushId, u64 sideId, usize *pIndexOut ) noexcept
{
    if ( pMap == nullptr || pMap->pAllocator == nullptr || brushId == 0u || sideId == 0u || pIndexOut == nullptr ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( pMap->bReadOnly ) { return map_status_t::READ_ONLY; }
    const auto *pBrush = geo::GeometryDocument_FindBrush( &pMap->geometry, { brushId } );
    if ( pBrush == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    for ( usize i = 0u; i < pBrush->sides.nCount; ++i ) {
        if ( pBrush->sides.pData[i].sourceId.value == sideId ) { *pIndexOut = i; return map_status_t::OK; }
    }
    return map_status_t::UNKNOWN_OBJECT;
}

map_status_t Publish( map_document_t *pMap, const geo::brush_source_t &source ) noexcept
{
    auto status = geo::BrushSource_Validate( &source, pMap->geometryPolicy ).status;
    if ( status == geo::geometry_status_t::OK ) {
        status = geo::BrushValidation_Deep( &source.solid, pMap->geometryPolicy, pMap->pAllocator ).status;
    }
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    const auto id = source.solid.sourceId;
    status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &pMap->geometry, { &id, 1u }, { &source, 1u } );
    if ( status == geo::geometry_status_t::OK ) { ++pMap->geometry.revision; }
    return Status( status );
}

} // namespace

map_status_t MapFaceEdit_PushPull( map_document_t *pMap, u64 brushId, u64 sideId, f64 distance ) noexcept
{
    if ( !std::isfinite( distance ) ) { return map_status_t::INVALID_ARGUMENT; }
    usize iSide = 0u;
    auto result = FindFace( pMap, brushId, sideId, &iSide );
    if ( result != map_status_t::OK || distance == 0.0 ) { return result; }
    geo::brush_source_t source{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { brushId }, pMap->pAllocator, &source );
    if ( status == geo::geometry_status_t::OK ) {
        auto plane = source.solid.sides.pData[iSide].plane;
        // The interior is n.p + d <= 0, so outward movement subtracts d.
        plane.d -= distance;
        status = std::isfinite( plane.d ) ? geo::BrushSolid_TrySetSidePlane( &source.solid, iSide, plane ) : geo::geometry_status_t::NUMERIC_FAILURE;
    }
    result = status == geo::geometry_status_t::OK ? Publish( pMap, source ) : Status( status );
    geo::BrushSource_Shutdown( &source );
    return result;
}

map_status_t MapFaceEdit_SetMaterial( map_document_t *pMap, u64 brushId, u64 sideId, string_view_t material ) noexcept
{
    if ( !StringView_IsValid( material ) || material.cchLength > MAP_MATERIAL_PATH_MAX ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < material.cchLength; ++i ) {
        if ( static_cast<unsigned char>( material.pData[i] ) < 0x20u || material.pData[i] == '\\' ) { return map_status_t::INVALID_ARGUMENT; }
    }
    usize iSide = 0u;
    auto result = FindFace( pMap, brushId, sideId, &iSide );
    if ( result != map_status_t::OK ) { return result; }
    geo::brush_source_t source{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &pMap->geometry, { brushId }, pMap->pAllocator, &source );
    if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
    auto &side = source.solid.sides.pData[iSide];
    geo::geometry_brush_side_attributes_t attributes{};
    status = geo::BrushSideAttributeStore_TryGet( &source.attributes, side.iAttributeIndex, &attributes );
    if ( status == geo::geometry_status_t::OK && StringView_Equals( MapMaterials_Path( &pMap->materials, attributes.material.value ), material ) ) {
        geo::BrushSource_Shutdown( &source );
        return map_status_t::OK;
    }
    u64 ref = 0u;
    result = status == geo::geometry_status_t::OK ? MapDocument_MaterialRef( pMap, material, &ref ) : Status( status );
    if ( result == map_status_t::OK ) {
        attributes.material.value = ref;
        bool shared = false;
        for ( usize i = 0u; i < source.solid.sides.nCount; ++i ) {
            if ( i != iSide && source.solid.sides.pData[i].iAttributeIndex == side.iAttributeIndex ) { shared = true; break; }
        }
        if ( shared ) {
            usize iAttribute = 0u;
            status = geo::BrushSideAttributeStore_TryAppend( &source.attributes, pMap->geometryPolicy, attributes, &iAttribute );
            if ( status == geo::geometry_status_t::OK ) { side.iAttributeIndex = static_cast<u32>( iAttribute ); }
        } else {
            status = geo::BrushSideAttributeStore_TrySet( &source.attributes, pMap->geometryPolicy.numerical, side.iAttributeIndex, attributes );
        }
        result = status == geo::geometry_status_t::OK ? Publish( pMap, source ) : Status( status );
    }
    geo::BrushSource_Shutdown( &source );
    return result;
}

} // namespace cypher::editor::map
