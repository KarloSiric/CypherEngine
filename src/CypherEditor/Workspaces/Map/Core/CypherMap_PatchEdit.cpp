//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_PatchEdit.cpp
//  Purpose: Implements the patch matrix operations over the geometry
//           library's patch resizing and reordering.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_PatchEdit.h"

#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Patch.h"
#include "CypherGeometry_BrushPatches.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_PatchResize.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;

namespace
{
constexpr auto OK = geo::geometry_status_t::OK;

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

bool Applies( const geo::patch_surface_t *patch, map_patch_op_t op ) noexcept
{
    if ( patch == nullptr ) { return false; }
    switch ( op ) {
        case map_patch_op_t::REMOVE_ROW: return geo::Patch_SubPatchRows( patch ) >= 2;
        case map_patch_op_t::REMOVE_COLUMN: return geo::Patch_SubPatchColumns( patch ) >= 2;
        case map_patch_op_t::INSERT_ROW: return geo::Patch_SubPatchRows( patch ) >= 1;
        case map_patch_op_t::INSERT_COLUMN: return geo::Patch_SubPatchColumns( patch ) >= 1;
        case map_patch_op_t::TRANSPOSE: case map_patch_op_t::INVERT: return true;
        case map_patch_op_t::COUNT: return false;
    }
    return false;
}
} // namespace

bool MapPatchEdit_Can( const map_document_t *map, u64 patch, map_patch_op_t op ) noexcept
{
    return map != nullptr && !map->bReadOnly && Applies( geo::GeometryDocument_FindPatch( &map->geometry, { patch } ), op );
}

map_status_t MapPatchEdit_Apply( map_document_t *map, span_t<const u64> patches, map_patch_op_t op ) noexcept
{
    if ( map == nullptr || !Span_IsValid( patches ) || patches.nCount == 0 || op >= map_patch_op_t::COUNT ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0; i < patches.nCount; ++i ) {
        const auto *patch = geo::GeometryDocument_FindPatch( &map->geometry, { patches.pData[i] } );
        if ( patch == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        if ( !Applies( patch, op ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    // Inserted controls must clear map-only IDs as well as geometry ones.
    auto &ids = map->geometry.sourceIds.allocator;
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &ids ) ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( map->nextId > 1 ) {
        const auto status = geo::GeometrySourceIdAllocator_AdvancePast( &ids, { map->nextId - 1 } );
        if ( status != OK ) { return Status( status ); }
    }
    for ( usize i = 0; i < patches.nCount; ++i ) {
        geo::patch_surface_t copy{};
        auto status = geo::Patch_TryClone( geo::GeometryDocument_FindPatch( &map->geometry, { patches.pData[i] } ), map->pAllocator, &copy );
        if ( status == OK ) {
            switch ( op ) {
                case map_patch_op_t::INSERT_ROW: status = geo::Patch_TryInsertRow( &copy, geo::Patch_SubPatchRows( &copy ) - 1u, &ids ); break;
                case map_patch_op_t::INSERT_COLUMN: status = geo::Patch_TryInsertColumn( &copy, geo::Patch_SubPatchColumns( &copy ) - 1u, &ids ); break;
                case map_patch_op_t::REMOVE_ROW: status = geo::Patch_TryRemoveRow( &copy, geo::Patch_SubPatchRows( &copy ) - 2u ); break;
                case map_patch_op_t::REMOVE_COLUMN: status = geo::Patch_TryRemoveColumn( &copy, geo::Patch_SubPatchColumns( &copy ) - 2u ); break;
                case map_patch_op_t::TRANSPOSE: status = geo::Patch_TryTranspose( &copy ); break;
                case map_patch_op_t::INVERT: geo::Patch_ReverseColumns( &copy ); break;
                case map_patch_op_t::COUNT: status = geo::geometry_status_t::INVALID_ARGUMENT; break;
            }
        }
        if ( status == OK ) { status = geo::GeometryDocument_TryReplacePatch( &map->geometry, &copy ); }
        geo::Patch_Shutdown( &copy );
        if ( status != OK ) { return Status( status ); }
    }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

namespace
{
using vec3 = math::vec3d_t;

vec3 Add( vec3 a, vec3 b ) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vec3 Sub( vec3 a, vec3 b ) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vec3 Mul( vec3 a, f64 s ) noexcept { return { a.x * s, a.y * s, a.z * s }; }
f64 Dot( vec3 a, vec3 b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool Same( vec3 a, vec3 b ) noexcept { const vec3 d = Sub( a, b ); return Dot( d, d ) <= 1e-12; }

// Copies the new patch into the document beside its source: same layer and
// owner, so it saves into the same layer and entity.
map_status_t AddBeside( map_document_t *map, const geo::patch_surface_t &patch, u64 source, vector_t<u64> *pRoots ) noexcept
{
    // Adding a placement can reallocate geometryRecords. Keep its values,
    // rather than a borrowed record, across either placement mutator.
    map_geometry_record_t placement{};
    for ( usize i = 0; i < map->geometryRecords.nCount; ++i ) {
        if ( map->geometryRecords.pData[i].id == source ) { placement = map->geometryRecords.pData[i]; break; }
    }
    auto result = Status( geo::GeometryDocument_TryAddPatch( &map->geometry, &patch ) );
    if ( result == map_status_t::OK && placement.id != 0 && placement.iLayer < map->layers.nCount ) {
        result = MapDocument_SetGeometryLayer( map, patch.sourceId.value, StringView_FromCString( map->layers.pData[placement.iLayer].id ) );
    }
    if ( result == map_status_t::OK && placement.id != 0 && placement.owner != 0 ) {
        result = MapDocument_SetGeometryOwner( map, patch.sourceId.value, placement.owner );
    }
    if ( result == map_status_t::OK && !Vector_PushBack( pRoots, patch.sourceId.value ) ) { result = map_status_t::OUT_OF_MEMORY; }
    return result;
}

// A seam strip from one border of the front patch to the same border of the
// back one: three rows (front, middle, back) along the border's controls.
// Turned to face away from the patch's centre.
map_status_t AddSeam( map_document_t *map, const geo::patch_surface_t &front, const geo::patch_surface_t &back, bool alongRows, u32 index,
    vec3 centre, u64 source, geo::geometry_source_id_allocator_t *ids, vector_t<u64> *pRoots ) noexcept
{
    const u32 count = alongRows ? front.cColumns : front.cRows;
    const auto frontAt = [&]( u32 k ) { return alongRows ? geo::Patch_Control( &front, k, index ) : geo::Patch_Control( &front, index, k ); };
    // The back patch has its columns reversed.
    const auto backAt = [&]( u32 k ) {
        return alongRows ? geo::Patch_Control( &back, back.cColumns - 1 - k, index ) : geo::Patch_Control( &back, back.cColumns - 1 - index, k );
    };
    // A collapsed border (a cone's tip) has no area to close.
    bool collapsed = true;
    for ( u32 k = 1; k < count; ++k ) { collapsed = collapsed && Same( frontAt( k )->position, frontAt( 0 )->position ); }
    if ( collapsed ) { return map_status_t::OK; }
    const auto root = geo::GeometrySourceIdAllocator_Allocate( ids );
    if ( root.status != geo::geometry_status_t::OK ) { return Status( root.status ); }
    geo::patch_surface_t seam{};
    auto status = geo::Patch_Init( &seam, map->pAllocator, front.basis, count, 3, root.id );
    for ( u32 k = 0; status == geo::geometry_status_t::OK && k < count; ++k ) {
        const auto *a = frontAt( k ), *b = backAt( k );
        const geo::patch_control_t rows[3]{ { a->position, { a->uv.x, 0.0 }, {} }, { Mul( Add( a->position, b->position ), 0.5 ), { a->uv.x, 0.5 }, {} },
                                            { b->position, { a->uv.x, 1.0 }, {} } };
        for ( u32 r = 0; status == geo::geometry_status_t::OK && r < 3; ++r ) {
            auto control = rows[r];
            const auto id = geo::GeometrySourceIdAllocator_Allocate( ids );
            status = id.status;
            control.sourceId = id.id;
            if ( status == geo::geometry_status_t::OK ) { status = geo::Patch_TrySetControl( &seam, k, r, control ); }
        }
    }
    seam.materialId = front.materialId;
    if ( status == geo::geometry_status_t::OK ) {
        const auto sample = geo::Patch_Evaluate( &seam, 0.5, 0.5 );
        if ( sample.bNormalValid && Dot( sample.normal, Sub( sample.position, centre ) ) < 0.0 ) { geo::Patch_ReverseColumns( &seam ); }
    }
    auto result = Status( status );
    if ( result == map_status_t::OK ) { result = AddBeside( map, seam, source, pRoots ); }
    geo::Patch_Shutdown( &seam );
    return result;
}

bool ClosedAlong( const geo::patch_surface_t &patch, bool columns ) noexcept
{
    const u32 count = columns ? patch.cRows : patch.cColumns;
    for ( u32 k = 0; k < count; ++k ) {
        const auto *first = columns ? geo::Patch_Control( &patch, 0, k ) : geo::Patch_Control( &patch, k, 0 );
        const auto *last = columns ? geo::Patch_Control( &patch, patch.cColumns - 1, k ) : geo::Patch_Control( &patch, k, patch.cRows - 1 );
        if ( !Same( first->position, last->position ) ) { return false; }
    }
    return true;
}
} // namespace

map_status_t MapPatchEdit_Thicken( map_document_t *map, span_t<const u64> patches, f64 thickness, bool seams, vector_t<u64> *pRootsOut ) noexcept
{
    if ( map == nullptr || !Span_IsValid( patches ) || patches.nCount == 0 || pRootsOut == nullptr || !Vector_IsValid( pRootsOut ) ||
         !std::isfinite( thickness ) || thickness == 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0; i < patches.nCount; ++i ) {
        if ( geo::GeometryDocument_FindPatch( &map->geometry, { patches.pData[i] } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    }
    auto &ids = map->geometry.sourceIds.allocator;
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &ids ) ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( map->nextId > 1 ) {
        const auto status = geo::GeometrySourceIdAllocator_AdvancePast( &ids, { map->nextId - 1 } );
        if ( status != OK ) { return Status( status ); }
    }
    for ( usize p = 0; p < patches.nCount; ++p ) {
        const u64 source = patches.pData[p];
        if ( !Vector_PushBack( pRootsOut, source ) ) { return map_status_t::OUT_OF_MEMORY; }
        geo::patch_surface_t front{}, back{};
        auto status = geo::Patch_TryClone( geo::GeometryDocument_FindPatch( &map->geometry, { source } ), map->pAllocator, &front );
        if ( status == OK ) { status = geo::Patch_TryClone( &front, map->pAllocator, &back ); }
        // Behind the front face, along the surface normal at each control's
        // own parameter (NetRadiant's approximation of an offset surface).
        vec3 centre{};
        for ( u32 r = 0; status == OK && r < front.cRows; ++r ) {
            for ( u32 c = 0; status == OK && c < front.cColumns; ++c ) {
                const auto sample = geo::Patch_Evaluate( &front, front.cColumns > 1 ? static_cast<f64>( c ) / ( front.cColumns - 1 ) : 0.0,
                                                         front.cRows > 1 ? static_cast<f64>( r ) / ( front.cRows - 1 ) : 0.0 );
                auto control = *geo::Patch_Control( &front, c, r );
                centre = Add( centre, Mul( control.position, 1.0 / ( front.cColumns * front.cRows ) ) );
                if ( !sample.bNormalValid ) { status = geo::geometry_status_t::DEGENERATE; break; }
                control.position = Sub( control.position, Mul( sample.normal, thickness ) );
                const auto id = geo::GeometrySourceIdAllocator_Allocate( &ids );
                status = id.status;
                control.sourceId = id.id;
                if ( status == OK ) { status = geo::Patch_TrySetControl( &back, c, r, control ); }
            }
        }
        if ( status == OK ) {
            const auto root = geo::GeometrySourceIdAllocator_Allocate( &ids );
            status = root.status;
            back.sourceId = root.id;
        }
        if ( status == OK ) { geo::Patch_ReverseColumns( &back ); } // It faces the other way.
        auto result = Status( status );
        if ( result == map_status_t::OK ) { result = AddBeside( map, back, source, pRootsOut ); }
        if ( result == map_status_t::OK && seams ) {
            // Rows 0 and last close the v borders, columns 0 and last the u
            // borders; a closed direction (a cylinder's seam) needs none.
            if ( !ClosedAlong( front, false ) ) {
                for ( u32 row : { 0u, front.cRows - 1 } ) { if ( result == map_status_t::OK ) { result = AddSeam( map, front, back, true, row, centre, source, &ids, pRootsOut ); } }
            }
            if ( !ClosedAlong( front, true ) ) {
                for ( u32 column : { 0u, front.cColumns - 1 } ) { if ( result == map_status_t::OK ) { result = AddSeam( map, front, back, false, column, centre, source, &ids, pRootsOut ); } }
            }
        }
        geo::Patch_Shutdown( &back );
        geo::Patch_Shutdown( &front );
        if ( result != map_status_t::OK ) { return result; }
    }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

map_status_t MapPatchEdit_FromBrushFace( map_document_t *map, u64 brush, u64 side, vector_t<u64> *pIdsOut ) noexcept
{
    if ( map == nullptr || pIdsOut == nullptr || !Vector_IsValid( pIdsOut ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( map->nextId == 0 || geometry::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    geometry::brush_source_t source{};
    auto status = geometry::GeometryDocument_TryCopyBrushSource( &map->geometry, { brush }, map->pAllocator, &source );
    u32 iSide = CY_U32_MAX;
    for ( usize i = 0; status == geometry::geometry_status_t::OK && i < source.solid.sides.nCount; ++i ) {
        if ( source.solid.sides.pData[i].sourceId.value == side ) { iSide = static_cast<u32>( i ); }
    }
    if ( status == geometry::geometry_status_t::OK && iSide == CY_U32_MAX ) { geometry::BrushSource_Shutdown( &source ); return map_status_t::UNKNOWN_OBJECT; }
    // The face's material goes with it.
    u32 material = 0;
    geometry::geometry_brush_side_attributes_t attributes{};
    if ( status == geometry::geometry_status_t::OK &&
         geometry::BrushSideAttributeStore_TryGet( &source.attributes, source.solid.sides.pData[iSide].iAttributeIndex, &attributes ) == geometry::geometry_status_t::OK ) {
        material = static_cast<u32>( attributes.material.value );
    }
    geometry::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    constexpr u32 kMax = 64;
    std::vector<geometry::patch_surface_t> patches( kMax );
    u32 count = 0;
    if ( status == geometry::geometry_status_t::OK ) {
        status = geometry::BrushPatches_TryFromFace( &source, iSide, geometry::patch_basis_t::BIQUADRATIC_BEZIER, map->pAllocator, map->geometryPolicy, &ids,
                                                     patches.data(), kMax, &count );
    }
    for ( u32 i = 0; status == geometry::geometry_status_t::OK && i < count; ++i ) {
        if ( material != 0 ) { patches[i].materialId = material; }
        status = geometry::GeometryDocument_TryAddPatch( &map->geometry, &patches[i] );
        if ( status == geometry::geometry_status_t::OK && !Vector_PushBack( pIdsOut, patches[i].sourceId.value ) ) { status = geometry::geometry_status_t::ALLOCATION_FAILED; }
    }
    for ( auto &patch : patches ) { geometry::Patch_Shutdown( &patch ); }
    geometry::BrushSource_Shutdown( &source );
    if ( status != geometry::geometry_status_t::OK ) {
        return status == geometry::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY : map_status_t::GEOMETRY_FAILED;
    }
    // Placed like the brush: its layer.
    for ( usize i = 0; i < pIdsOut->nCount; ++i ) {
        const auto result = MapDocument_SetGeometryLayer( map, pIdsOut->pData[i], { map->layers.pData[0].id, std::strlen( map->layers.pData[0].id ) } );
        if ( result != map_status_t::OK ) { return result; }
    }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

namespace
{
// One closed border filled from its centre (see MapPatchEdit_Cap).
map_status_t AddCap( map_document_t *map, const geo::patch_surface_t &patch, bool alongRows, u32 index, vec3 patchCentre, u64 source,
    geo::geometry_source_id_allocator_t *ids, vector_t<u64> *pRoots, usize &caps ) noexcept
{
    const u32 count = alongRows ? patch.cColumns : patch.cRows;
    const auto at = [&]( u32 k ) { return alongRows ? geo::Patch_Control( &patch, k, index ) : geo::Patch_Control( &patch, index, k ); };
    if ( count < 3 || !Same( at( 0 )->position, at( count - 1 )->position ) ) { return map_status_t::OK; } // Open border.
    bool collapsed = true;
    for ( u32 k = 1; k < count; ++k ) { collapsed = collapsed && Same( at( k )->position, at( 0 )->position ); }
    if ( collapsed ) { return map_status_t::OK; } // A cone's tip.
    vec3 centre{}, normal{};
    for ( u32 k = 0; k + 1 < count; ++k ) {
        const vec3 a = at( k )->position, b = at( k + 1 )->position;
        centre = Add( centre, Mul( a, 1.0 / ( count - 1 ) ) );
        normal = Add( normal, vec3{ ( a.y - b.y ) * ( a.z + b.z ), ( a.z - b.z ) * ( a.x + b.x ), ( a.x - b.x ) * ( a.y + b.y ) } );
    }
    // A loop running counter-clockwise about its normal fills facing along
    // it; turn the loop so that faces away from the patch.
    const vec3 away = Sub( centre, patchCentre );
    // A patch centred in its own border (a disc) already fills it.
    f64 radius = 0.0;
    for ( u32 k = 0; k + 1 < count; ++k ) { const vec3 d = Sub( at( k )->position, centre ); radius += std::sqrt( Dot( d, d ) ) / ( count - 1 ); }
    if ( std::sqrt( Dot( away, away ) ) < 1e-3 * radius ) { return map_status_t::OK; }
    const bool reverse = normal.x * away.x + normal.y * away.y + normal.z * away.z < 0.0;
    const u32 degree = geo::Patch_Degree( patch.basis );
    const auto root = geo::GeometrySourceIdAllocator_Allocate( ids );
    if ( root.status != geo::geometry_status_t::OK || degree == 0 ) { return root.status != geo::geometry_status_t::OK ? Status( root.status ) : map_status_t::INVALID_ARGUMENT; }
    geo::patch_surface_t cap{};
    auto status = geo::Patch_Init( &cap, map->pAllocator, patch.basis, count, degree + 1, root.id );
    cap.materialId = patch.materialId;
    for ( u32 k = 0; status == geo::geometry_status_t::OK && k < count; ++k ) {
        const vec3 rim = at( reverse ? count - 1 - k : k )->position;
        for ( u32 r = 0; status == geo::geometry_status_t::OK && r <= degree; ++r ) {
            const f64 t = static_cast<f64>( r ) / degree;
            const auto id = geo::GeometrySourceIdAllocator_Allocate( ids );
            status = id.status;
            const geo::patch_control_t control{ Add( Mul( rim, 1.0 - t ), Mul( centre, t ) ), { static_cast<f64>( k ) / ( count - 1 ), t }, id.id };
            if ( status == geo::geometry_status_t::OK ) { status = geo::Patch_TrySetControl( &cap, k, r, control ); }
        }
    }
    auto result = Status( status );
    if ( result == map_status_t::OK ) { result = AddBeside( map, cap, source, pRoots ); }
    geo::Patch_Shutdown( &cap );
    if ( result == map_status_t::OK ) { ++caps; }
    return result;
}
} // namespace

map_status_t MapPatchEdit_Cap( map_document_t *map, span_t<const u64> patches, vector_t<u64> *pRootsOut, usize *pCapsOut ) noexcept
{
    if ( pCapsOut != nullptr ) { *pCapsOut = 0; }
    if ( map == nullptr || !Span_IsValid( patches ) || patches.nCount == 0 || pRootsOut == nullptr || !Vector_IsValid( pRootsOut ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0; i < patches.nCount; ++i ) {
        if ( geo::GeometryDocument_FindPatch( &map->geometry, { patches.pData[i] } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    }
    auto &ids = map->geometry.sourceIds.allocator;
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &ids ) ) { return map_status_t::LIMIT_EXCEEDED; }
    if ( map->nextId > 1 ) {
        const auto status = geo::GeometrySourceIdAllocator_AdvancePast( &ids, { map->nextId - 1 } );
        if ( status != OK ) { return Status( status ); }
    }
    usize caps = 0;
    for ( usize p = 0; p < patches.nCount; ++p ) {
        const u64 source = patches.pData[p];
        if ( !Vector_PushBack( pRootsOut, source ) ) { return map_status_t::OUT_OF_MEMORY; }
        // The document may move patches as caps are added: work on a copy.
        geo::patch_surface_t patch{};
        auto result = Status( geo::Patch_TryClone( geo::GeometryDocument_FindPatch( &map->geometry, { source } ), map->pAllocator, &patch ) );
        // The middle of the controls' box: a seam's repeated controls would
        // pull an average off centre.
        vec3 low{ 1e300, 1e300, 1e300 }, high{ -1e300, -1e300, -1e300 };
        for ( u32 r = 0; result == map_status_t::OK && r < patch.cRows; ++r ) {
            for ( u32 c = 0; c < patch.cColumns; ++c ) {
                const vec3 q = geo::Patch_Control( &patch, c, r )->position;
                low = { std::min( low.x, q.x ), std::min( low.y, q.y ), std::min( low.z, q.z ) };
                high = { std::max( high.x, q.x ), std::max( high.y, q.y ), std::max( high.z, q.z ) };
            }
        }
        const vec3 centre = Mul( Add( low, high ), 0.5 );
        for ( u32 row : { 0u, patch.cRows - 1 } ) { if ( result == map_status_t::OK ) { result = AddCap( map, patch, true, row, centre, source, &ids, pRootsOut, caps ); } }
        for ( u32 column : { 0u, patch.cColumns - 1 } ) { if ( result == map_status_t::OK ) { result = AddCap( map, patch, false, column, centre, source, &ids, pRootsOut, caps ); } }
        geo::Patch_Shutdown( &patch );
        if ( result != map_status_t::OK ) { return result; }
    }
    if ( caps == 0 ) { return map_status_t::OK; } // Nothing closed: the map is unchanged.
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    if ( pCapsOut != nullptr ) { *pCapsOut = caps; }
    return map_status_t::OK;
}

} // namespace cypher::editor::map

