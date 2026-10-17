//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TextureEdit.cpp
//  Purpose: Implements material alignment on brush sides (projection
//           records) and mesh faces (corner UVs).
//  Details: Brush polygons for Fit, Justify and pivots come from the
//           reconstructed boundary of the private copy; brush planes are the
//           only canonical data.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_TextureEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_FaceEdit.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushFaceQueries.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Patch.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshUvTransform.h"
#include "CypherGeometry_UvAlignment.h"
#include "CypherGeometry_UvTools.h"
#include "CypherGeometry_MeshSurfacing.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
using vec3 = math::vec3d_t;
constexpr auto OK = geo::geometry_status_t::OK;
constexpr f64 kPi = 3.14159265358979323846;

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

enum class op_kind_t : u8 { ALIGN, ROTATE, SCALE, SHIFT, COPY, FIT_RECT, HOTSPOT };
struct uv_op_t {
    op_kind_t kind{ op_kind_t::ALIGN };
    map_uv_align_t align{ map_uv_align_t::WORLD };
    f64 radians{ 0.0 };
    math::vec2d_t factor{ 1.0, 1.0 };
    math::vec2d_t shift{ 0.0, 0.0 };
    f64 meshWorldUnitsPerUv{ 128.0 };
    geo::geometry_brush_side_attributes_t copy{}; // COPY: a brush side's material and projection,
    vec3 copyNormal{};                            // and the way that side faces.
    map_uv_rect_t rect{};                         // FIT_RECT: where the side goes,
    map_uv_fit_mode_t fitMode{ map_uv_fit_mode_t::EXISTING }; // and how.
    const std::vector<geo::mesh_hotspot_rect_t> *pHotspots{ nullptr }; // HOTSPOT: the regions to choose from.
};

bool SideUvBounds( const geo::geometry_brush_side_attributes_t &a, const std::vector<vec3> &polygon, math::vec2d_t &low, math::vec2d_t &high ) noexcept;

// Scales the side's texture about the face's centre so its UV extent
// matches the rectangle (a negative factor flips), then shifts it in.
geo::geometry_status_t FitSide( geo::geometry_brush_side_attributes_t &a, const std::vector<vec3> &polygon, vec3 center, const map_uv_rect_t &rect,
    const geo::geometry_policy_t &policy ) noexcept
{
    math::vec2d_t low{}, high{};
    if ( !SideUvBounds( a, polygon, low, high ) ) { return geo::geometry_status_t::DEGENERATE; }
    const math::vec2d_t size{ rect.maximum.x - rect.minimum.x, rect.maximum.y - rect.minimum.y };
    const math::vec2d_t factor{ ( high.x - low.x ) / size.x * ( rect.bFlipU ? -1.0 : 1.0 ), ( high.y - low.y ) / size.y * ( rect.bFlipV ? -1.0 : 1.0 ) };
    const auto status = geo::UvTools_TryScaleAbout( &a, center, factor, policy.numerical );
    if ( status != geo::geometry_status_t::OK ) { return status; }
    if ( !SideUvBounds( a, polygon, low, high ) ) { return geo::geometry_status_t::DEGENERATE; }
    return geo::UvTools_TryShift( &a, { rect.minimum.x - low.x, rect.minimum.y - low.y }, policy.numerical );
}

// Hotspot texturing for a brush side (meshes use MeshUv_TryApplyHotspots):
// measured in its own plane, it takes the region its size matches best
// (a quarter turn where allowed), fitted exactly along non-tiling axes and
// at the region's density along tiling ones.
geo::geometry_status_t HotspotSide( geo::geometry_brush_side_attributes_t &a, vec3 normal, const std::vector<vec3> &polygon, vec3 center,
    const std::vector<geo::mesh_hotspot_rect_t> &rects, const geo::geometry_policy_t &policy ) noexcept
{
    if ( rects.empty() ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
    auto status = geo::UvAlign_Reset( &a, normal );
    if ( status != geo::geometry_status_t::OK ) { return status; }
    a.uvProjection.worldUnitsPerUv = { 1.0, 1.0 }; // UV extents are world sizes now.
    math::vec2d_t low{}, high{};
    if ( !SideUvBounds( a, polygon, low, high ) ) { return geo::geometry_status_t::DEGENERATE; }
    const f64 width = high.x - low.x, height = high.y - low.y;
    f64 best = 1e300; usize chosen = 0; bool turned = false;
    for ( usize i = 0; i < rects.size(); ++i ) {
        const auto &r = rects[i];
        for ( const bool turn : { false, true } ) {
            if ( turn && !r.bAllowRotation ) { continue; }
            const f64 w = turn ? height : width, h = turn ? width : height;
            const f64 cost = ( r.bTileU ? 0.0 : std::fabs( std::log( w / r.worldSize.x ) ) ) + ( r.bTileV ? 0.0 : std::fabs( std::log( h / r.worldSize.y ) ) );
            if ( cost < best - 1e-12 ) { best = cost; chosen = i; turned = turn; }
        }
    }
    const auto &r = rects[chosen];
    if ( turned ) {
        status = geo::UvTools_TryRotateAbout( &a, center, 1.5707963267948966, policy.numerical );
        if ( status != geo::geometry_status_t::OK ) { return status; }
    }
    const f64 w = turned ? height : width, h = turned ? width : height;
    map_uv_rect_t target{};
    target.minimum = r.uvMin;
    target.maximum = { r.uvMin.x + ( r.uvMax.x - r.uvMin.x ) * ( r.bTileU ? w / r.worldSize.x : 1.0 ),
                       r.uvMin.y + ( r.uvMax.y - r.uvMin.y ) * ( r.bTileV ? h / r.worldSize.y : 1.0 ) };
    return FitSide( a, polygon, center, target, policy );
}

// UV bounds of a polygon under a side's projection.
bool SideUvBounds( const geo::geometry_brush_side_attributes_t &a, const std::vector<vec3> &polygon, math::vec2d_t &low, math::vec2d_t &high ) noexcept
{
    low = { 1e300, 1e300 }; high = { -1e300, -1e300 };
    for ( const vec3 &p : polygon ) {
        math::vec2d_t uv{};
        if ( !math::Uvd_TryProjectPlanarPoint( a.uvProjection, p, 1e-12, &uv ) ) { return false; }
        low = { std::min( low.x, uv.x ), std::min( low.y, uv.y ) }; high = { std::max( high.x, uv.x ), std::max( high.y, uv.y ) };
    }
    return !polygon.empty() && high.x - low.x > 1e-12 && high.y - low.y > 1e-12;
}

// The world axis a face mostly faces, signed: what world alignment projects
// along (a 45-degree slope picks Z first, then X, as the axis basis does).
vec3 DominantAxis( vec3 n ) noexcept
{
    const f64 x = std::fabs( n.x ), y = std::fabs( n.y ), z = std::fabs( n.z );
    if ( z >= x && z >= y ) { return { 0, 0, n.z < 0 ? -1.0 : 1.0 }; }
    if ( x >= y ) { return { n.x < 0 ? -1.0 : 1.0, 0, 0 }; }
    return { 0, n.y < 0 ? -1.0 : 1.0, 0 };
}

bool Targets( const std::vector<u64> &faces, u64 face ) noexcept
{
    return faces.empty() || std::find( faces.begin(), faces.end(), face ) != faces.end();
}

/*
================
Brush sides
================
*/

geo::geometry_status_t ApplyToSide( geo::geometry_brush_side_attributes_t &a, vec3 normal, const std::vector<vec3> &polygon,
    const uv_op_t &op, const geo::geometry_policy_t &policy ) noexcept
{
    if ( polygon.empty() ) { return geo::geometry_status_t::DEGENERATE; }
    vec3 center{};
    for ( const vec3 &p : polygon ) { center = { center.x + p.x, center.y + p.y, center.z + p.z }; }
    center = { center.x / polygon.size(), center.y / polygon.size(), center.z / polygon.size() };
    const auto scale = a.uvProjection.worldUnitsPerUv;
    switch ( op.kind ) {
        case op_kind_t::ROTATE: return geo::UvTools_TryRotateAbout( &a, center, op.radians, policy.numerical );
        case op_kind_t::SCALE: return geo::UvTools_TryScaleAbout( &a, center, op.factor, policy.numerical );
        case op_kind_t::SHIFT: return geo::UvTools_TryShift( &a, { op.shift.x / scale.x, op.shift.y / scale.y }, policy.numerical );
        case op_kind_t::COPY: {
            // A projection is anchored in the world, so a face facing the
            // same way continues it seamlessly; any other keeps its own.
            a.material = op.copy.material;
            const vec3 m = op.copyNormal;
            if ( normal.x * m.x + normal.y * m.y + normal.z * m.z > 0.999 ) { a.uvProjection = op.copy.uvProjection; }
            return OK;
        }
        case op_kind_t::FIT_RECT: {
            if ( op.fitMode == map_uv_fit_mode_t::PLANAR ) {
                const auto aligned = geo::UvAlign_Reset( &a, normal );
                a.uvProjection.worldUnitsPerUv = scale;
                if ( aligned != OK ) { return aligned; }
            }
            return FitSide( a, polygon, center, op.rect, policy );
        }
        case op_kind_t::HOTSPOT: return op.pHotspots != nullptr ? HotspotSide( a, normal, polygon, center, *op.pHotspots, policy ) : geo::geometry_status_t::INVALID_ARGUMENT;
        case op_kind_t::ALIGN: break;
    }
    geo::geometry_status_t status = OK;
    switch ( op.align ) {
        case map_uv_align_t::WORLD:
        case map_uv_align_t::FACE:
            // Reset rebuilds the basis at unit scale; the face keeps its own.
            status = geo::UvAlign_Reset( &a, op.align == map_uv_align_t::WORLD ? DominantAxis( normal ) : normal );
            a.uvProjection.worldUnitsPerUv = scale;
            return status;
        case map_uv_align_t::FIT: return geo::UvAlign_FitToFace( &a, polygon.data(), polygon.size() );
        case map_uv_align_t::JUSTIFY_LEFT: return geo::UvAlign_JustifyU( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::MIN );
        case map_uv_align_t::JUSTIFY_RIGHT: return geo::UvAlign_JustifyU( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::MAX );
        case map_uv_align_t::JUSTIFY_TOP: return geo::UvAlign_JustifyV( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::MAX );
        case map_uv_align_t::JUSTIFY_BOTTOM: return geo::UvAlign_JustifyV( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::MIN );
        case map_uv_align_t::JUSTIFY_CENTER:
            status = geo::UvAlign_JustifyU( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::CENTER );
            return status == OK ? geo::UvAlign_JustifyV( &a, polygon.data(), polygon.size(), geo::uv_justify_mode_t::CENTER ) : status;
        case map_uv_align_t::COUNT: break;
    }
    return geo::geometry_status_t::INVALID_ARGUMENT;
}

map_status_t BrushOne( map_document_t *map, u64 id, const std::vector<u64> &faces, const uv_op_t &op ) noexcept
{
    geo::brush_source_t source{};
    geo::brush_boundary_t boundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { id }, map->pAllocator, &source );
    if ( status == OK ) { status = geo::BrushBoundary_Init( &boundary, map->pAllocator ); }
    if ( status == OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &source.solid, map->geometryPolicy ); }
    // How many sides use each record: one shared with another side is split
    // before editing, so aligning one face never moves another's texture.
    std::vector<u32> users;
    for ( usize i = 0; status == OK && i < source.solid.sides.nCount; ++i ) {
        const u32 record = source.solid.sides.pData[i].iAttributeIndex;
        if ( record >= users.size() ) { users.resize( record + 1u, 0u ); }
        ++users[record];
    }
    bool touched = false;
    for ( usize i = 0; status == OK && i < source.solid.sides.nCount; ++i ) {
        auto &side = source.solid.sides.pData[i];
        if ( !Targets( faces, side.sourceId.value ) ) { continue; }
        std::vector<vec3> polygon;
        for ( usize f = 0; f < boundary.faces.nCount; ++f ) {
            const auto &face = boundary.faces.pData[f];
            if ( face.iSide != i ) { continue; }
            for ( u32 k = 0; k < face.cVertices; ++k ) { polygon.push_back( boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]] ); }
        }
        geo::geometry_brush_side_attributes_t attributes{};
        status = geo::BrushSideAttributeStore_TryGet( &source.attributes, side.iAttributeIndex, &attributes );
        if ( status == OK ) { status = ApplyToSide( attributes, side.plane.normal, polygon, op, map->geometryPolicy ); }
        if ( status != OK ) { break; }
        if ( users[side.iAttributeIndex] > 1u ) {
            usize record = 0;
            status = geo::BrushSideAttributeStore_TryAppend( &source.attributes, map->geometryPolicy, attributes, &record );
            if ( status == OK ) {
                --users[side.iAttributeIndex];
                side.iAttributeIndex = static_cast<u32>( record );
                if ( record >= users.size() ) { users.resize( record + 1u, 0u ); }
                users[record] = 1u;
            }
        } else {
            status = geo::BrushSideAttributeStore_TrySet( &source.attributes, map->geometryPolicy.numerical, side.iAttributeIndex, attributes );
        }
        touched = true;
    }
    if ( status == OK && !touched ) { status = geo::geometry_status_t::INVALID_HANDLE; } // No such side.
    if ( status == OK ) { status = geo::BrushSource_Validate( &source, map->geometryPolicy ).status; }
    if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &source.solid.sourceId, 1u }, { &source, 1u } ); }
    if ( status == OK ) { ++map->geometry.revision; }
    geo::BrushBoundary_Shutdown( &boundary );
    geo::BrushSource_Shutdown( &source );
    return Status( status );
}

/*
================
Mesh faces
================
*/

struct mesh_job_t {
    const std::vector<u64> *pFaces{};
    const uv_op_t *pOp{};
};

// World alignment has no mesh helper: each corner projects onto the two
// world axes beside its face's dominant axis.
geo::geometry_status_t AlignMeshToWorld( geo::mesh_source_t *working, const std::vector<u64> &faces, f64 worldUnitsPerUv ) noexcept
{
    geo::mesh_source_description_t description{};
    auto status = geo::MeshSourceDescription_Init( &description, working->mesh.pAllocator, working->sourceId );
    if ( status == OK ) { status = geo::MeshSource_TryDescribe( working, &description ); }
    for ( usize f = 0; status == OK && f < description.faces.nCount; ++f ) {
        const auto &face = description.faces.pData[f];
        if ( !Targets( faces, face.sourceId.value ) ) { continue; }
        vec3 normal{};
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const vec3 p = description.vertices.pData[description.corners.pData[face.iFirstCorner + k].iVertex].position;
            const vec3 q = description.vertices.pData[description.corners.pData[face.iFirstCorner + ( k + 1 ) % face.cCorners].iVertex].position;
            normal = { normal.x + ( p.y - q.y ) * ( p.z + q.z ), normal.y + ( p.z - q.z ) * ( p.x + q.x ), normal.z + ( p.x - q.x ) * ( p.y + q.y ) };
        }
        geo::geometry_brush_side_attributes_t basis{};
        status = geo::UvAlign_Reset( &basis, DominantAxis( normal ) );
        for ( u32 k = 0; status == OK && k < face.cCorners; ++k ) {
            const auto &corner = description.corners.pData[face.iFirstCorner + k];
            const vec3 p = description.vertices.pData[corner.iVertex].position;
            auto attributes = corner.attributes;
            attributes.uv0 = { ( p.x * basis.uvProjection.uAxis.x + p.y * basis.uvProjection.uAxis.y + p.z * basis.uvProjection.uAxis.z ) / worldUnitsPerUv,
                               ( p.x * basis.uvProjection.vAxis.x + p.y * basis.uvProjection.vAxis.y + p.z * basis.uvProjection.vAxis.z ) / worldUnitsPerUv };
            status = geo::MeshSourceEdit_TrySetCornerAttributes( working, face.sourceId, description.vertices.pData[corner.iVertex].sourceId, attributes );
        }
    }
    geo::MeshSourceDescription_Shutdown( &description );
    return status;
}

geo::geometry_status_t MeshEdit( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept
{
    const auto &job = *static_cast<const mesh_job_t *>( context );
    const auto &op = *job.pOp;
    if ( op.kind == op_kind_t::ALIGN && op.align == map_uv_align_t::WORLD ) { return AlignMeshToWorld( working, *job.pFaces, op.meshWorldUnitsPerUv ); }
    std::vector<geo::geometry_mesh_face_handle_t> handles;
    if ( job.pFaces->empty() ) {
        ( void )GenerationPool_ForEach( &working->mesh.faces, [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t & ) noexcept -> bool_t {
            handles.push_back( handle ); return CY_TRUE;
        } );
    }
    for ( u64 id : *job.pFaces ) {
        geo::geometry_mesh_face_handle_t handle{};
        if ( !geo::MeshSource_TryFindFace( working, { id }, &handle ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
        handles.push_back( handle );
    }
    const span_t<const geo::geometry_mesh_face_handle_t> span{ handles.data(), handles.size() };
    const auto set = geo::mesh_uv_set_t::MATERIAL;
    geo::mesh_uv_transform_t transform{};
    switch ( op.kind ) {
        case op_kind_t::ROTATE: transform.rotationRadians = op.radians; break;
        // A texture twice as large repeats half as often: UVs shrink.
        case op_kind_t::SCALE: transform.scale = { 1.0 / op.factor.x, 1.0 / op.factor.y }; break;
        case op_kind_t::SHIFT: transform.shift = { op.shift.x / op.meshWorldUnitsPerUv, op.shift.y / op.meshWorldUnitsPerUv }; break;
        case op_kind_t::COPY:
        case op_kind_t::FIT_RECT:
        case op_kind_t::HOTSPOT: return geo::geometry_status_t::INVALID_ARGUMENT; // Brush sides only here; meshes fit below.
        case op_kind_t::ALIGN:
            switch ( op.align ) {
                case map_uv_align_t::FACE:
                    return geo::MeshUv_TryAlignToFace( &working->attributes, &working->mesh, span, set, { op.meshWorldUnitsPerUv, op.meshWorldUnitsPerUv } );
                case map_uv_align_t::FIT: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::FIT, false );
                case map_uv_align_t::JUSTIFY_LEFT: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::MIN_U, false );
                case map_uv_align_t::JUSTIFY_RIGHT: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::MAX_U, false );
                case map_uv_align_t::JUSTIFY_TOP: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::MAX_V, false );
                case map_uv_align_t::JUSTIFY_BOTTOM: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::MIN_V, false );
                case map_uv_align_t::JUSTIFY_CENTER: return geo::MeshUv_TryJustify( &working->attributes, &working->mesh, span, set, geo::mesh_uv_justify_t::CENTER, false );
                default: return geo::geometry_status_t::INVALID_ARGUMENT;
            }
    }
    return geo::MeshUv_TryTransform( &working->attributes, &working->mesh, span, set, transform, geo::mesh_uv_pivot_t::FACE_CENTER, {} );
}

map_status_t Apply( map_document_t *map, span_t<const map_surface_ref_t> surfaces, const uv_op_t &op ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || surfaces.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( !std::isfinite( op.meshWorldUnitsPerUv ) || op.meshWorldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    // One pass per object with all of its listed faces; an object listed
    // whole (face 0) takes every face.
    std::vector<u64> objects;
    for ( usize i = 0; i < surfaces.nCount; ++i ) {
        if ( surfaces.pData[i].object == 0 ) { return map_status_t::INVALID_ARGUMENT; }
        if ( std::find( objects.begin(), objects.end(), surfaces.pData[i].object ) == objects.end() ) { objects.push_back( surfaces.pData[i].object ); }
    }
    for ( u64 id : objects ) {
        std::vector<u64> faces;
        bool whole = false;
        for ( usize i = 0; i < surfaces.nCount; ++i ) {
            if ( surfaces.pData[i].object != id ) { continue; }
            whole |= surfaces.pData[i].face == 0;
            faces.push_back( surfaces.pData[i].face );
        }
        if ( whole ) { faces.clear(); }
        map_status_t result = map_status_t::UNKNOWN_OBJECT;
        if ( geo::GeometryDocument_FindBrush( &map->geometry, { id } ) != nullptr ) {
            result = BrushOne( map, id, faces, op );
        } else if ( geo::GeometryDocument_FindMesh( &map->geometry, { id } ) != nullptr ) {
            const mesh_job_t job{ &faces, &op };
            result = MapMeshEdit_Apply( map, id, &MeshEdit, const_cast<mesh_job_t *>( &job ) );
        } else if ( MapDocument_FindObject( map, id, nullptr ) != nullptr ) {
            result = map_status_t::INVALID_ARGUMENT; // Entities, patches and terrain have no face textures here.
        }
        if ( result != map_status_t::OK ) { return result; }
    }
    return map_status_t::OK;
}
} // namespace

map_status_t MapTextureEdit_Align( map_document_t *map, span_t<const map_surface_ref_t> surfaces, map_uv_align_t align, f64 meshWorldUnitsPerUv ) noexcept
{
    if ( align >= map_uv_align_t::COUNT ) { return map_status_t::INVALID_ARGUMENT; }
    uv_op_t op{}; op.kind = op_kind_t::ALIGN; op.align = align; op.meshWorldUnitsPerUv = meshWorldUnitsPerUv;
    return Apply( map, surfaces, op );
}

map_status_t MapTextureEdit_Rotate( map_document_t *map, span_t<const map_surface_ref_t> surfaces, f64 degrees ) noexcept
{
    if ( !std::isfinite( degrees ) ) { return map_status_t::INVALID_ARGUMENT; }
    uv_op_t op{}; op.kind = op_kind_t::ROTATE; op.radians = degrees * kPi / 180.0;
    return Apply( map, surfaces, op );
}

map_status_t MapTextureEdit_Scale( map_document_t *map, span_t<const map_surface_ref_t> surfaces, math::vec2d_t factor ) noexcept
{
    if ( !std::isfinite( factor.x ) || !std::isfinite( factor.y ) || std::fabs( factor.x ) < 1e-6 || std::fabs( factor.y ) < 1e-6 ) { return map_status_t::INVALID_ARGUMENT; }
    uv_op_t op{}; op.kind = op_kind_t::SCALE; op.factor = factor;
    return Apply( map, surfaces, op );
}

map_status_t MapTextureEdit_Shift( map_document_t *map, span_t<const map_surface_ref_t> surfaces, math::vec2d_t worldDistance, f64 meshWorldUnitsPerUv ) noexcept
{
    if ( !std::isfinite( worldDistance.x ) || !std::isfinite( worldDistance.y ) ) { return map_status_t::INVALID_ARGUMENT; }
    uv_op_t op{}; op.kind = op_kind_t::SHIFT; op.shift = worldDistance; op.meshWorldUnitsPerUv = meshWorldUnitsPerUv;
    return Apply( map, surfaces, op );
}

namespace
{
geo::geometry_status_t UnwrapMesh( geo::mesh_source_t *working, const std::vector<u64> &faces ) noexcept
{
    geo::mesh_source_description_t d{};
    auto status = geo::MeshSourceDescription_Init( &d, working->mesh.pAllocator, working->sourceId );
    if ( status == OK ) { status = geo::MeshSource_TryDescribe( working, &d ); }
    if ( status != OK ) { geo::MeshSourceDescription_Shutdown( &d ); return status; }
    const usize nFaces = d.faces.nCount;
    const auto corner = [&]( usize f, u32 k ) { return d.corners.pData[d.faces.pData[f].iFirstCorner + ( k % d.faces.pData[f].cCorners )].iVertex; };
    // Signed dominant axis per face (0..5), and which faces take part.
    std::vector<int> axis( nFaces, -1 );
    std::vector<bool> included( nFaces, false );
    for ( usize f = 0; f < nFaces; ++f ) {
        included[f] = Targets( faces, d.faces.pData[f].sourceId.value );
        vec3 n{};
        for ( u32 k = 0; k < d.faces.pData[f].cCorners; ++k ) {
            const vec3 p = d.vertices.pData[corner( f, k )].position, q = d.vertices.pData[corner( f, k + 1 )].position;
            n = { n.x + ( p.y - q.y ) * ( p.z + q.z ), n.y + ( p.z - q.z ) * ( p.x + q.x ), n.z + ( p.x - q.x ) * ( p.y + q.y ) };
        }
        const f64 ax = std::fabs( n.x ), ay = std::fabs( n.y ), az = std::fabs( n.z );
        axis[f] = az >= ax && az >= ay ? ( n.z < 0 ? 5 : 4 ) : ax >= ay ? ( n.x < 0 ? 1 : 0 ) : ( n.y < 0 ? 3 : 2 );
    }
    // Edge (vertex pair) -> faces, for chart growth across shared edges.
    std::vector<std::pair<std::pair<u32, u32>, u32>> edges;
    for ( usize f = 0; f < nFaces; ++f ) {
        for ( u32 k = 0; k < d.faces.pData[f].cCorners; ++k ) {
            const u32 a = corner( f, k ), b = corner( f, k + 1 );
            edges.push_back( { { std::min( a, b ), std::max( a, b ) }, static_cast<u32>( f ) } );
        }
    }
    std::sort( edges.begin(), edges.end() );
    std::vector<int> chart( nFaces, -1 );
    int charts = 0;
    for ( usize seed = 0; seed < nFaces; ++seed ) {
        if ( !included[seed] || chart[seed] >= 0 ) { continue; }
        std::vector<usize> stack{ seed };
        chart[seed] = charts;
        while ( !stack.empty() ) {
            const usize f = stack.back(); stack.pop_back();
            for ( u32 k = 0; k < d.faces.pData[f].cCorners; ++k ) {
                const u32 a = corner( f, k ), b = corner( f, k + 1 );
                const std::pair<u32, u32> key{ std::min( a, b ), std::max( a, b ) };
                auto it = std::lower_bound( edges.begin(), edges.end(), std::make_pair( key, 0u ) );
                for ( ; it != edges.end() && it->first == key; ++it ) {
                    const usize g = it->second;
                    if ( included[g] && chart[g] < 0 && axis[g] == axis[f] ) { chart[g] = charts; stack.push_back( g ); }
                }
            }
        }
        ++charts;
    }
    // Project each chart along its axis and measure it.
    struct box_t { f64 x0{ 1e300 }, y0{ 1e300 }, x1{ -1e300 }, y1{ -1e300 }, px{ 0 }, py{ 0 }; };
    std::vector<box_t> boxes( static_cast<usize>( charts ) );
    std::vector<math::vec2d_t> projected( d.corners.nCount );
    for ( usize f = 0; f < nFaces; ++f ) {
        if ( chart[f] < 0 ) { continue; }
        geo::geometry_brush_side_attributes_t basis{};
        const int a = axis[f];
        const vec3 direction{ a / 2 == 0 ? ( a % 2 ? -1.0 : 1.0 ) : 0.0, a / 2 == 1 ? ( a % 2 ? -1.0 : 1.0 ) : 0.0, a / 2 == 2 ? ( a % 2 ? -1.0 : 1.0 ) : 0.0 };
        if ( geo::UvAlign_Reset( &basis, direction ) != OK ) { continue; }
        auto &box = boxes[static_cast<usize>( chart[f] )];
        for ( u32 k = 0; k < d.faces.pData[f].cCorners; ++k ) {
            const vec3 p = d.vertices.pData[corner( f, k )].position;
            const math::vec2d_t uv{ p.x * basis.uvProjection.uAxis.x + p.y * basis.uvProjection.uAxis.y + p.z * basis.uvProjection.uAxis.z,
                                     p.x * basis.uvProjection.vAxis.x + p.y * basis.uvProjection.vAxis.y + p.z * basis.uvProjection.vAxis.z };
            projected[d.faces.pData[f].iFirstCorner + k] = uv;
            box.x0 = std::min( box.x0, uv.x ); box.y0 = std::min( box.y0, uv.y ); box.x1 = std::max( box.x1, uv.x ); box.y1 = std::max( box.y1, uv.y );
        }
    }
    // Shelf packing, tallest first, into a roughly square region.
    f64 area = 0.0, widest = 0.0;
    for ( const auto &box : boxes ) { area += ( box.x1 - box.x0 ) * ( box.y1 - box.y0 ); widest = std::max( widest, box.x1 - box.x0 ); }
    const f64 margin = std::sqrt( std::max( area, 1e-12 ) ) * 0.02;
    const f64 rowWidth = std::max( widest, std::sqrt( std::max( area, 1e-12 ) ) * 1.2 );
    std::vector<usize> order( boxes.size() );
    for ( usize i = 0; i < order.size(); ++i ) { order[i] = i; }
    std::sort( order.begin(), order.end(), [&]( usize l, usize r ) { return boxes[l].y1 - boxes[l].y0 > boxes[r].y1 - boxes[r].y0; } );
    f64 x = 0.0, y = 0.0, rowHeight = 0.0, extentX = 0.0;
    for ( usize i : order ) {
        auto &box = boxes[i];
        const f64 w = box.x1 - box.x0, h = box.y1 - box.y0;
        if ( x > 0.0 && x + w > rowWidth ) { x = 0.0; y += rowHeight + margin; rowHeight = 0.0; }
        box.px = x; box.py = y;
        x += w + margin; rowHeight = std::max( rowHeight, h ); extentX = std::max( extentX, x - margin );
    }
    const f64 extent = std::max( { extentX, y + rowHeight, 1e-12 } );
    for ( usize f = 0; status == OK && f < nFaces; ++f ) {
        if ( chart[f] < 0 ) { continue; }
        const auto &box = boxes[static_cast<usize>( chart[f] )];
        for ( u32 k = 0; status == OK && k < d.faces.pData[f].cCorners; ++k ) {
            auto attributes = d.corners.pData[d.faces.pData[f].iFirstCorner + k].attributes;
            const auto uv = projected[d.faces.pData[f].iFirstCorner + k];
            attributes.uv0 = { ( uv.x - box.x0 + box.px ) / extent, ( uv.y - box.y0 + box.py ) / extent };
            status = geo::MeshSourceEdit_TrySetCornerAttributes( working, d.faces.pData[f].sourceId, d.vertices.pData[corner( f, k )].sourceId, attributes );
        }
    }
    geo::MeshSourceDescription_Shutdown( &d );
    return status;
}
} // namespace

map_status_t MapTextureEdit_Unwrap( map_document_t *map, span_t<const map_surface_ref_t> surfaces ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || surfaces.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    std::vector<u64> objects;
    for ( usize i = 0; i < surfaces.nCount; ++i ) {
        if ( std::find( objects.begin(), objects.end(), surfaces.pData[i].object ) == objects.end() ) { objects.push_back( surfaces.pData[i].object ); }
    }
    bool any = false;
    for ( u64 id : objects ) {
        if ( geo::GeometryDocument_FindMesh( &map->geometry, { id } ) == nullptr ) { continue; } // Brush sides keep their projections.
        std::vector<u64> faces;
        bool whole = false;
        for ( usize i = 0; i < surfaces.nCount; ++i ) {
            if ( surfaces.pData[i].object == id ) { whole |= surfaces.pData[i].face == 0; faces.push_back( surfaces.pData[i].face ); }
        }
        if ( whole ) { faces.clear(); }
        const auto result = MapMeshEdit_Apply( map, id, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            return UnwrapMesh( working, *static_cast<const std::vector<u64> *>( context ) );
        }, &faces );
        if ( result != map_status_t::OK ) { return result; }
        any = true;
    }
    return any ? map_status_t::OK : map_status_t::INVALID_ARGUMENT;
}

map_status_t MapTextureEdit_ReplaceMaterial( map_document_t *map, string_view_t from, string_view_t to, usize *pReplacedOut ) noexcept
{
    if ( pReplacedOut != nullptr ) { *pReplacedOut = 0; }
    if ( map == nullptr || !StringView_IsValid( from ) || !StringView_IsValid( to ) || from.cchLength == 0 || to.cchLength == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( StringView_Equals( from, to ) ) { return map_status_t::OK; }
    const auto uses = [&]( u64 ref ) { return ref != 0 && StringView_Equals( MapMaterials_Path( &map->materials, ref ), from ); };
    // Gather the users first: the target path may enter the table now.
    vector_t<u64> brushes{}, meshes{}, patches{};
    if ( !Vector_Init( &brushes, map->pAllocator ) || !Vector_Init( &meshes, map->pAllocator ) ||
         !Vector_Init( &patches, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0; i < map->geometry.brushes.nCount; ++i ) {
        const auto *brush = map->geometry.brushes.pData[i];
        const auto *store = geo::GeometryDocument_FindBrushAttributes( &map->geometry, brush->sourceId );
        for ( usize k = 0; store != nullptr && k < store->records.nCount; ++k ) {
            if ( uses( store->records.pData[k].material.value ) ) {
                if ( !Vector_PushBack( &brushes, brush->sourceId.value ) ) { return map_status_t::OUT_OF_MEMORY; }
                break;
            }
        }
    }
    for ( usize i = 0; i < map->geometry.meshes.nCount; ++i ) {
        const auto *mesh = map->geometry.meshes.pData[i];
        bool found = false;
        ( void )GenerationPool_ForEach( &mesh->mesh.faces, [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t & ) noexcept -> bool_t {
            found = uses( geo::MeshAttributeStore_GetFace( &mesh->attributes, handle ).material.value );
            return found ? CY_FALSE : CY_TRUE;
        } );
        if ( found && !Vector_PushBack( &meshes, mesh->sourceId.value ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    for ( usize i = 0; i < geo::GeometryDocument_PatchCount( &map->geometry ); ++i ) {
        const auto *patch = geo::GeometryDocument_PatchAt( &map->geometry, i );
        if ( uses( patch->materialId ) && !Vector_PushBack( &patches, patch->sourceId.value ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    if ( brushes.nCount == 0 && meshes.nCount == 0 && patches.nCount == 0 ) { return map_status_t::OK; }
    u64 target = 0;
    auto result = MapDocument_MaterialRef( map, to, &target );
    usize replaced = 0;
    for ( usize i = 0; result == map_status_t::OK && i < brushes.nCount; ++i ) {
        geo::brush_source_t source{};
        auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushes.pData[i] }, map->pAllocator, &source );
        for ( usize k = 0; status == OK && k < source.attributes.records.nCount; ++k ) {
            auto record = source.attributes.records.pData[k];
            if ( !uses( record.material.value ) ) { continue; }
            record.material.value = target;
            status = geo::BrushSideAttributeStore_TrySet( &source.attributes, map->geometryPolicy.numerical, k, record );
            for ( usize side = 0; side < source.solid.sides.nCount; ++side ) { replaced += source.solid.sides.pData[side].iAttributeIndex == k; }
        }
        if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &source.solid.sourceId, 1 }, { &source, 1 } ); }
        geo::BrushSource_Shutdown( &source );
        result = Status( status );
    }
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        struct job_t { decltype( uses ) *pUses; u64 target; usize *pReplaced; } job{ &uses, target, &replaced };
        result = MapMeshEdit_Apply( map, meshes.pData[i], []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            auto &replace = *static_cast<job_t *>( context );
            geo::mesh_source_description_t description{};
            auto status = geo::MeshSourceDescription_Init( &description, working->mesh.pAllocator, working->sourceId );
            if ( status == OK ) { status = geo::MeshSource_TryDescribe( working, &description ); }
            for ( usize f = 0; status == OK && f < description.faces.nCount; ++f ) {
                auto attributes = description.faces.pData[f].attributes;
                if ( !( *replace.pUses )( attributes.material.value ) ) { continue; }
                attributes.material.value = replace.target;
                status = geo::MeshSourceEdit_TrySetFaceAttributes( working, description.faces.pData[f].sourceId, attributes );
                ++*replace.pReplaced;
            }
            geo::MeshSourceDescription_Shutdown( &description );
            return status;
        }, &job );
    }
    for ( usize i = 0; result == map_status_t::OK && i < patches.nCount; ++i ) {
        geo::patch_surface_t copy{};
        auto status = geo::Patch_TryClone( geo::GeometryDocument_FindPatch( &map->geometry, { patches.pData[i] } ), map->pAllocator, &copy );
        if ( status == OK ) { copy.materialId = static_cast<u32>( target ); status = geo::GeometryDocument_TryReplacePatch( &map->geometry, &copy ); ++replaced; }
        geo::Patch_Shutdown( &copy );
        result = Status( status );
    }
    if ( result == map_status_t::OK ) {
        ++map->geometry.revision;
        if ( pReplacedOut != nullptr ) { *pReplacedOut = replaced; }
    }
    return result;
}

namespace
{
// Mesh faces of the surfaces, per mesh (an empty list: every face).
std::vector<std::pair<u64, std::vector<u64>>> MeshFaces( const map_document_t *map, span_t<const map_surface_ref_t> surfaces )
{
    std::vector<std::pair<u64, std::vector<u64>>> out;
    for ( usize i = 0; i < surfaces.nCount; ++i ) {
        const auto &surface = surfaces.pData[i];
        if ( geo::GeometryDocument_FindMesh( &map->geometry, { surface.object } ) == nullptr ) { continue; }
        auto at = std::find_if( out.begin(), out.end(), [&]( const auto &entry ) { return entry.first == surface.object; } );
        if ( at == out.end() ) { out.push_back( { surface.object, {} } ); at = out.end() - 1; if ( surface.face == 0 ) { continue; } }
        else if ( at->second.empty() ) { continue; }
        if ( surface.face == 0 ) { at->second.clear(); } else { at->second.push_back( surface.face ); }
    }
    return out;
}

// The face handles of a working mesh for a face ID list (empty: all).
bool Handles( const geo::mesh_source_t *working, const std::vector<u64> &faces, std::vector<geo::geometry_mesh_face_handle_t> &out ) noexcept
{
    out.clear();
    if ( faces.empty() ) {
        ( void )GenerationPool_ForEach( &working->mesh.faces, [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t & ) noexcept -> bool_t {
            out.push_back( handle ); return CY_TRUE;
        } );
        return true;
    }
    for ( u64 id : faces ) {
        geo::geometry_mesh_face_handle_t handle{};
        if ( !geo::MeshSource_TryFindFace( working, { id }, &handle ) ) { return false; }
        out.push_back( handle );
    }
    return true;
}

template <typename Edit>
map_status_t EachMesh( map_document_t *map, span_t<const map_surface_ref_t> surfaces, Edit edit ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    const auto meshes = MeshFaces( map, surfaces );
    if ( meshes.empty() ) { return map_status_t::INVALID_ARGUMENT; } // Only mesh faces take these.
    for ( const auto &[id, faces] : meshes ) {
        struct job_t { const std::vector<u64> *pFaces; Edit *pEdit; } job{ &faces, &edit };
        const auto result = MapMeshEdit_Apply( map, id, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            std::vector<geo::geometry_mesh_face_handle_t> handles;
            if ( !Handles( working, *j.pFaces, handles ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            return ( *j.pEdit )( working, span_t<const geo::geometry_mesh_face_handle_t>{ handles.data(), handles.size() } );
        }, &job );
        if ( result != map_status_t::OK ) { return result; }
    }
    return map_status_t::OK;
}
} // namespace

map_status_t MapTextureEdit_Project( map_document_t *map, span_t<const map_surface_ref_t> surfaces, map_uv_projection_t projection, vec3 origin, vec3 axis,
    f64 radius, math::vec2d_t repeat ) noexcept
{
    if ( !std::isfinite( repeat.x ) || !std::isfinite( repeat.y ) || repeat.x <= 0.0 || repeat.y <= 0.0 || projection > map_uv_projection_t::PLANAR ) { return map_status_t::INVALID_ARGUMENT; }
    planar_uv_mappingd_t mapping{};
    if ( projection == map_uv_projection_t::PLANAR ) {
        const vec3 up = std::fabs( axis.z ) < 0.9 ? vec3{ 0, 0, 1 } : vec3{ 0, 1, 0 };
        if ( !math::Uvd_TryBuildPlanarMapping( origin, axis, up, repeat, 0.0, {}, 1e-9, &mapping ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    return EachMesh( map, surfaces, [&]( geo::mesh_source_t *working, span_t<const geo::geometry_mesh_face_handle_t> faces ) noexcept {
        switch ( projection ) {
            case map_uv_projection_t::BOX: return geo::MeshSurfacing_TryProjectBox( &working->attributes, &working->mesh, faces, origin, repeat, geo::mesh_uv_set_t::MATERIAL );
            case map_uv_projection_t::CYLINDER: return geo::MeshSurfacing_TryProjectCylindrical( &working->attributes, &working->mesh, faces, origin, axis, radius, repeat, geo::mesh_uv_set_t::MATERIAL );
            default: return geo::MeshSurfacing_TryProjectPlanar( &working->attributes, &working->mesh, faces, mapping, geo::mesh_uv_set_t::MATERIAL );
        }
    } );
}

map_status_t MapTextureEdit_SetSmoothingGroups( map_document_t *map, span_t<const map_surface_ref_t> surfaces, u32 groups ) noexcept
{
    return EachMesh( map, surfaces, [&]( geo::mesh_source_t *working, span_t<const geo::geometry_mesh_face_handle_t> faces ) noexcept {
        return geo::MeshSurfacing_TrySetSmoothingGroups( &working->attributes, &working->mesh, faces, groups );
    } );
}

map_status_t MapTextureEdit_MarkHardEdgesByAngle( map_document_t *map, span_t<const u64> meshes, f64 degrees, usize *pHardOut ) noexcept
{
    if ( pHardOut != nullptr ) { *pHardOut = 0; }
    if ( map == nullptr || !Span_IsValid( meshes ) || meshes.nCount == 0 || !std::isfinite( degrees ) || degrees < 0.0 || degrees > 180.0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    usize total = 0;
    for ( usize i = 0; i < meshes.nCount; ++i ) {
        if ( geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        struct job_t { f64 radians; u32 hard; } job{ degrees * 3.14159265358979323846 / 180.0, 0u };
        const auto result = MapMeshEdit_Apply( map, meshes.pData[i], []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            auto &j = *static_cast<job_t *>( context );
            return geo::MeshSurfacing_TryMarkHardEdgesByAngle( &working->attributes, &working->mesh, j.radians, &j.hard );
        }, &job );
        if ( result != map_status_t::OK ) { return result; }
        total += job.hard;
    }
    if ( pHardOut != nullptr ) { *pHardOut = total; }
    return map_status_t::OK;
}

map_status_t MapTextureEdit_SetRepeat( map_document_t *map, span_t<const map_surface_ref_t> surfaces, math::vec2d_t repeat ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || !std::isfinite( repeat.x ) || !std::isfinite( repeat.y ) || repeat.x <= 0.0 || repeat.y <= 0.0 ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    auto result = map_status_t::OK;
    for ( usize i = 0; result == map_status_t::OK && i < surfaces.nCount; ++i ) {
        const auto &surface = surfaces.pData[i];
        if ( const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } ) ) {
            // Each side from its own current size: sides of one brush differ.
            std::vector<std::pair<u64, math::vec2d_t>> sides;
            const auto *store = geo::GeometryDocument_FindBrushAttributes( &map->geometry, brush->sourceId );
            for ( usize k = 0; store != nullptr && k < brush->sides.nCount; ++k ) {
                const auto &side = brush->sides.pData[k];
                if ( ( surface.face != 0 && side.sourceId.value != surface.face ) || side.iAttributeIndex >= store->records.nCount ) { continue; }
                sides.push_back( { side.sourceId.value, store->records.pData[side.iAttributeIndex].uvProjection.worldUnitsPerUv } );
            }
            for ( const auto &[side, current] : sides ) {
                if ( current.x == 0.0 || current.y == 0.0 ) { continue; }
                const math::vec2d_t factor{ repeat.x / std::fabs( current.x ) * ( current.x < 0 ? -1.0 : 1.0 ), repeat.y / std::fabs( current.y ) * ( current.y < 0 ? -1.0 : 1.0 ) };
                if ( std::fabs( factor.x - 1.0 ) < 1e-12 && std::fabs( factor.y - 1.0 ) < 1e-12 ) { continue; }
                const map_surface_ref_t one{ surface.object, side };
                result = MapTextureEdit_Scale( map, { &one, 1 }, factor );
                if ( result != map_status_t::OK ) { break; }
            }
        } else if ( geo::GeometryDocument_FindMesh( &map->geometry, { surface.object } ) != nullptr ) {
            result = MapTextureEdit_Align( map, { &surface, 1 }, map_uv_align_t::WORLD, repeat.x );
            if ( result == map_status_t::OK && std::fabs( repeat.y / repeat.x - 1.0 ) > 1e-12 ) { result = MapTextureEdit_Scale( map, { &surface, 1 }, { 1.0, repeat.y / repeat.x } ); }
        } else {
            result = map_status_t::UNKNOWN_OBJECT;
        }
    }
    return result;
}

map_status_t MapTextureEdit_SetMaterial( map_document_t *map, span_t<const map_surface_ref_t> surfaces, string_view_t material, usize *pChangedOut ) noexcept
{
    if ( pChangedOut != nullptr ) { *pChangedOut = 0; }
    if ( map == nullptr || !Span_IsValid( surfaces ) || !StringView_IsValid( material ) || material.cchLength == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    u64 target = 0;
    auto result = MapDocument_MaterialRef( map, material, &target );
    usize changed = 0;
    const auto has = [&]( u64 ref ) { return ref == target; };
    // Mesh faces are gathered per mesh, for one transaction each (no faces:
    // every face).
    std::vector<std::pair<u64, std::vector<u64>>> meshes;
    for ( usize i = 0; result == map_status_t::OK && i < surfaces.nCount; ++i ) {
        const auto &surface = surfaces.pData[i];
        if ( const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } ) ) {
            const auto *store = geo::GeometryDocument_FindBrushAttributes( &map->geometry, brush->sourceId );
            for ( usize k = 0; result == map_status_t::OK && k < brush->sides.nCount; ++k ) {
                const auto &side = brush->sides.pData[k];
                if ( surface.face != 0 && side.sourceId.value != surface.face ) { continue; }
                const bool already = store != nullptr && side.iAttributeIndex < store->records.nCount && has( store->records.pData[side.iAttributeIndex].material.value );
                if ( already ) { continue; }
                result = MapFaceEdit_SetMaterial( map, surface.object, side.sourceId.value, material );
                store = geo::GeometryDocument_FindBrushAttributes( &map->geometry, { surface.object } );
                brush = geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } );
                ++changed;
                if ( brush == nullptr ) { result = map_status_t::GEOMETRY_FAILED; }
            }
        } else if ( geo::GeometryDocument_FindMesh( &map->geometry, { surface.object } ) != nullptr ) {
            auto at = std::find_if( meshes.begin(), meshes.end(), [&]( const auto &entry ) { return entry.first == surface.object; } );
            if ( at == meshes.end() ) { meshes.push_back( { surface.object, {} } ); at = meshes.end() - 1; if ( surface.face == 0 ) { continue; } }
            else if ( at->second.empty() ) { continue; } // Already every face.
            if ( surface.face == 0 ) { at->second.clear(); } else { at->second.push_back( surface.face ); }
        } else if ( const auto *patch = geo::GeometryDocument_FindPatch( &map->geometry, { surface.object } ) ) {
            if ( has( patch->materialId ) ) { continue; }
            geo::patch_surface_t copy{};
            auto status = geo::Patch_TryClone( patch, map->pAllocator, &copy );
            if ( status == OK ) { copy.materialId = static_cast<u32>( target ); status = geo::GeometryDocument_TryReplacePatch( &map->geometry, &copy ); ++changed; }
            geo::Patch_Shutdown( &copy );
            result = Status( status );
        } else {
            result = map_status_t::UNKNOWN_OBJECT;
        }
    }
    for ( usize i = 0; result == map_status_t::OK && i < meshes.size(); ++i ) {
        struct job_t { const std::vector<u64> *pFaces; u64 target; usize *pChanged; } job{ &meshes[i].second, target, &changed };
        result = MapMeshEdit_Apply( map, meshes[i].first, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            auto &set = *static_cast<job_t *>( context );
            geo::mesh_source_description_t description{};
            auto status = geo::MeshSourceDescription_Init( &description, working->mesh.pAllocator, working->sourceId );
            if ( status == OK ) { status = geo::MeshSource_TryDescribe( working, &description ); }
            for ( usize f = 0; status == OK && f < description.faces.nCount; ++f ) {
                const auto &face = description.faces.pData[f];
                if ( !set.pFaces->empty() && std::find( set.pFaces->begin(), set.pFaces->end(), face.sourceId.value ) == set.pFaces->end() ) { continue; }
                if ( face.attributes.material.value == set.target ) { continue; }
                auto attributes = face.attributes;
                attributes.material.value = set.target;
                status = geo::MeshSourceEdit_TrySetFaceAttributes( working, face.sourceId, attributes );
                ++*set.pChanged;
            }
            geo::MeshSourceDescription_Shutdown( &description );
            return status;
        }, &job );
    }
    if ( result == map_status_t::OK && changed != 0 ) {
        ++map->geometry.revision;
        if ( pChangedOut != nullptr ) { *pChangedOut = changed; }
    }
    return result;
}

map_status_t MapTextureEdit_CoplanarBrushSides( const map_document_t *map, map_surface_ref_t seed, std::vector<map_surface_ref_t> &out )
{
    out.clear();
    if ( map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, { seed.object } );
    if ( brush == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    const auto &brushes = map->geometry.brushes;
    geo::brush_face_ref_t ref{ CY_U32_MAX, CY_U32_MAX };
    for ( usize b = 0; b < brushes.nCount; ++b ) { if ( brushes.pData[b] == brush ) { ref.iBrush = static_cast<u32>( b ); } }
    for ( usize k = 0; k < brush->sides.nCount; ++k ) { if ( brush->sides.pData[k].sourceId.value == seed.face ) { ref.iSide = static_cast<u32>( k ); } }
    if ( ref.iBrush == CY_U32_MAX || ref.iSide == CY_U32_MAX ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<geo::brush_face_ref_t> faces{};
    if ( !Vector_Init( &faces, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto status = geo::BrushFaces_TrySelectCoplanar( { brushes.pData, brushes.nCount }, ref, map->geometryPolicy, map->pAllocator, &faces );
    for ( usize i = 0; status == OK && i < faces.nCount; ++i ) {
        const auto *solid = brushes.pData[faces.pData[i].iBrush];
        out.push_back( { solid->sourceId.value, solid->sides.pData[faces.pData[i].iSide].sourceId.value } );
    }
    Vector_Shutdown( &faces );
    return Status( status );
}

map_status_t MapTextureEdit_CopyBrushSide( map_document_t *map, map_surface_ref_t from, span_t<const map_surface_ref_t> to, usize *pChangedOut ) noexcept
{
    if ( pChangedOut != nullptr ) { *pChangedOut = 0; }
    if ( map == nullptr || !Span_IsValid( to ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    // The record a side uses, or null.
    const auto record = [&]( map_surface_ref_t surface, vec3 *pNormal ) -> const geo::geometry_brush_side_attributes_t * {
        const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } );
        const auto *store = brush != nullptr ? geo::GeometryDocument_FindBrushAttributes( &map->geometry, brush->sourceId ) : nullptr;
        for ( usize k = 0; store != nullptr && k < brush->sides.nCount; ++k ) {
            const auto &side = brush->sides.pData[k];
            if ( side.sourceId.value != surface.face || side.iAttributeIndex >= store->records.nCount ) { continue; }
            *pNormal = side.plane.normal;
            return &store->records.pData[side.iAttributeIndex];
        }
        return nullptr;
    };
    vec3 facing{};
    const auto *source = record( from, &facing );
    if ( source == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    uv_op_t op{}; op.kind = op_kind_t::COPY; op.copy = *source; op.copyNormal = facing;
    // Sides already wearing the source's material and projection are left
    // alone, so wrapping twice is a no-op; the rest go one brush at a time.
    std::vector<std::pair<u64, std::vector<u64>>> brushes;
    for ( usize i = 0; i < to.nCount; ++i ) {
        const auto surface = to.pData[i];
        vec3 normal{};
        const auto *current = record( surface, &normal );
        if ( current == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
        if ( surface.object == from.object && surface.face == from.face ) { continue; }
        const auto &a = current->uvProjection, &b = source->uvProjection;
        const auto same = []( vec3 p, vec3 q ) { return p.x == q.x && p.y == q.y && p.z == q.z; };
        const bool parallel = normal.x * facing.x + normal.y * facing.y + normal.z * facing.z > 0.999;
        const bool projected = !parallel || ( same( a.origin, b.origin ) && same( a.uAxis, b.uAxis ) && same( a.vAxis, b.vAxis ) && same( a.normal, b.normal ) &&
                                              a.worldUnitsPerUv.x == b.worldUnitsPerUv.x && a.worldUnitsPerUv.y == b.worldUnitsPerUv.y && a.rotationRadians == b.rotationRadians );
        if ( current->material.value == source->material.value && projected ) { continue; }
        auto it = std::find_if( brushes.begin(), brushes.end(), [&]( const auto &entry ) { return entry.first == surface.object; } );
        if ( it == brushes.end() ) { brushes.push_back( { surface.object, {} } ); it = brushes.end() - 1; }
        it->second.push_back( surface.face );
    }
    usize changed = 0;
    for ( const auto &[id, faces] : brushes ) {
        const auto result = BrushOne( map, id, faces, op );
        if ( result != map_status_t::OK ) { return result; }
        changed += faces.size();
    }
    if ( pChangedOut != nullptr ) { *pChangedOut = changed; }
    return map_status_t::OK;
}

map_status_t MapTextureEdit_FitToRect( map_document_t *map, span_t<const map_surface_ref_t> surfaces, const map_uv_rect_t &rect, map_uv_fit_mode_t mode ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || surfaces.nCount == 0 || mode > map_uv_fit_mode_t::SQUARE ) { return map_status_t::INVALID_ARGUMENT; }
    for ( f64 v : { rect.minimum.x, rect.minimum.y, rect.maximum.x, rect.maximum.y } ) { if ( !std::isfinite( v ) ) { return map_status_t::INVALID_ARGUMENT; } }
    if ( rect.maximum.x - rect.minimum.x < 1e-9 || rect.maximum.y - rect.minimum.y < 1e-9 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    // Brush sides, one brush at a time.
    std::vector<std::pair<u64, std::vector<u64>>> brushes;
    bool meshes = false;
    for ( usize i = 0; i < surfaces.nCount; ++i ) {
        const auto &surface = surfaces.pData[i];
        if ( geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } ) == nullptr ) {
            meshes = meshes || geo::GeometryDocument_FindMesh( &map->geometry, { surface.object } ) != nullptr;
            continue;
        }
        auto it = std::find_if( brushes.begin(), brushes.end(), [&]( const auto &entry ) { return entry.first == surface.object; } );
        if ( it == brushes.end() ) { brushes.push_back( { surface.object, {} } ); it = brushes.end() - 1; }
        if ( surface.face == 0 ) { it->second.clear(); it->second.push_back( 0 ); } // Every side.
        else if ( !( it->second.size() == 1 && it->second[0] == 0 ) ) { it->second.push_back( surface.face ); }
    }
    uv_op_t op{}; op.kind = op_kind_t::FIT_RECT; op.rect = rect; op.fitMode = mode;
    for ( auto &[id, faces] : brushes ) {
        if ( faces.size() == 1 && faces[0] == 0 ) { faces.clear(); }
        const auto result = BrushOne( map, id, faces, op );
        if ( result != map_status_t::OK ) { return result; }
    }
    if ( !meshes ) { return brushes.empty() ? map_status_t::INVALID_ARGUMENT : map_status_t::OK; }
    return EachMesh( map, surfaces, [&]( geo::mesh_source_t *working, span_t<const geo::geometry_mesh_face_handle_t> faces ) noexcept {
        const auto set = geo::mesh_uv_set_t::MATERIAL;
        auto status = OK;
        if ( mode == map_uv_fit_mode_t::PLANAR ) {
            // Flat along the faces' average normal (area weighted); faces
            // that cancel out (a box's top and bottom) go along the first.
            vec3 normal{}, first{};
            for ( usize f = 0; f < faces.nCount; ++f ) {
                const auto *face = geo::EditableMesh_GetFace( &working->mesh, faces.pData[f] );
                const auto *loop = face != nullptr ? geo::EditableMesh_GetLoop( &working->mesh, face->hOuterLoop ) : nullptr;
                std::vector<vec3> points;
                auto edge = loop != nullptr ? loop->hFirstHalfEdge : geo::geometry_mesh_half_edge_handle_t{};
                for ( u32 k = 0; loop != nullptr && k < loop->cHalfEdges; ++k ) {
                    const auto *record = geo::EditableMesh_GetHalfEdge( &working->mesh, edge );
                    const auto *vertex = record != nullptr ? geo::EditableMesh_GetVertex( &working->mesh, record->hOrigin ) : nullptr;
                    if ( vertex == nullptr ) { break; }
                    points.push_back( vertex->position ); edge = record->hNext;
                }
                vec3 own{};
                for ( usize k = 0; k < points.size(); ++k ) {
                    const vec3 a = points[k], b = points[( k + 1 ) % points.size()];
                    own = { own.x + ( a.y - b.y ) * ( a.z + b.z ), own.y + ( a.z - b.z ) * ( a.x + b.x ), own.z + ( a.x - b.x ) * ( a.y + b.y ) };
                }
                normal = { normal.x + own.x, normal.y + own.y, normal.z + own.z };
                if ( f == 0 ) { first = own; }
            }
            const f64 total = std::sqrt( first.x * first.x + first.y * first.y + first.z * first.z );
            if ( std::sqrt( normal.x * normal.x + normal.y * normal.y + normal.z * normal.z ) < 1e-6 * total ) { normal = first; }
            const f64 length = std::sqrt( normal.x * normal.x + normal.y * normal.y + normal.z * normal.z );
            if ( length < 1e-12 ) { return geo::geometry_status_t::DEGENERATE; }
            normal = { normal.x / length, normal.y / length, normal.z / length };
            planar_uv_mappingd_t mapping{};
            const vec3 up = std::fabs( normal.z ) < 0.9 ? vec3{ 0, 0, 1 } : vec3{ 0, 1, 0 };
            if ( !math::Uvd_TryBuildPlanarMapping( {}, normal, up, { 128.0, 128.0 }, 0.0, {}, 1e-9, &mapping ) ) { return geo::geometry_status_t::DEGENERATE; }
            status = geo::MeshSurfacing_TryProjectPlanar( &working->attributes, &working->mesh, faces, mapping, set );
        }
        // Onto 0..1 (together, or each face for SQUARE), then into the rectangle.
        if ( status == OK ) { status = geo::MeshUv_TryJustify( &working->attributes, &working->mesh, faces, set, geo::mesh_uv_justify_t::FIT, mode != map_uv_fit_mode_t::SQUARE ); }
        if ( status != OK ) { return status; }
        geo::mesh_uv_transform_t transform{};
        const math::vec2d_t size{ rect.maximum.x - rect.minimum.x, rect.maximum.y - rect.minimum.y };
        transform.scale = { rect.bFlipU ? -size.x : size.x, rect.bFlipV ? -size.y : size.y };
        transform.shift = { rect.bFlipU ? rect.maximum.x : rect.minimum.x, rect.bFlipV ? rect.maximum.y : rect.minimum.y };
        return geo::MeshUv_TryTransform( &working->attributes, &working->mesh, faces, set, transform, geo::mesh_uv_pivot_t::POINT, { 0.0, 0.0 } );
    } );
}

map_status_t MapTextureEdit_ApplyHotspots( map_document_t *map, span_t<const map_surface_ref_t> surfaces, const std::vector<geo::mesh_hotspot_rect_t> &rects ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || surfaces.nCount == 0 || rects.empty() || rects.size() > geo::kMeshHotspotRectsMax ) { return map_status_t::INVALID_ARGUMENT; }
    for ( const auto &r : rects ) {
        if ( !( r.uvMax.x > r.uvMin.x ) || !( r.uvMax.y > r.uvMin.y ) || !( r.worldSize.x > 0.0 ) || !( r.worldSize.y > 0.0 ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    std::vector<std::pair<u64, std::vector<u64>>> brushes;
    bool meshes = false;
    for ( usize i = 0; i < surfaces.nCount; ++i ) {
        const auto &surface = surfaces.pData[i];
        if ( geo::GeometryDocument_FindBrush( &map->geometry, { surface.object } ) == nullptr ) {
            meshes = meshes || geo::GeometryDocument_FindMesh( &map->geometry, { surface.object } ) != nullptr;
            continue;
        }
        auto it = std::find_if( brushes.begin(), brushes.end(), [&]( const auto &entry ) { return entry.first == surface.object; } );
        if ( it == brushes.end() ) { brushes.push_back( { surface.object, {} } ); it = brushes.end() - 1; }
        if ( surface.face == 0 ) { it->second.clear(); it->second.push_back( 0 ); }
        else if ( !( it->second.size() == 1 && it->second[0] == 0 ) ) { it->second.push_back( surface.face ); }
    }
    uv_op_t op{}; op.kind = op_kind_t::HOTSPOT; op.pHotspots = &rects;
    for ( auto &[id, faces] : brushes ) {
        if ( faces.size() == 1 && faces[0] == 0 ) { faces.clear(); }
        const auto result = BrushOne( map, id, faces, op );
        if ( result != map_status_t::OK ) { return result; }
    }
    if ( !meshes ) { return brushes.empty() ? map_status_t::INVALID_ARGUMENT : map_status_t::OK; }
    return EachMesh( map, surfaces, [&]( geo::mesh_source_t *working, span_t<const geo::geometry_mesh_face_handle_t> faces ) noexcept {
        return geo::MeshUv_TryApplyHotspots( &working->attributes, &working->mesh, faces, geo::mesh_uv_set_t::MATERIAL, { rects.data(), rects.size() }, nullptr );
    } );
}

/*
================
UV editor
================
*/

map_status_t MapTextureEdit_Describe( const map_document_t *map, map_surface_ref_t surface, map_uv_face_t *pOut )
{
    if ( map == nullptr || pOut == nullptr || surface.object == 0 || surface.face == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    *pOut = map_uv_face_t{};
    pOut->surface = surface;
    const geo::geometry_source_id_t id{ surface.object };
    if ( const auto *brush = geo::GeometryDocument_FindBrush( &map->geometry, id ) ) {
        ( void )brush;
        geo::brush_source_t source{};
        geo::brush_boundary_t boundary{};
        auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, id, map->pAllocator, &source );
        if ( status == OK ) { status = geo::BrushBoundary_Init( &boundary, map->pAllocator ); }
        if ( status == OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &source.solid, map->geometryPolicy ); }
        bool found = false;
        for ( usize i = 0; status == OK && i < source.solid.sides.nCount && !found; ++i ) {
            const auto &side = source.solid.sides.pData[i];
            if ( side.sourceId.value != surface.face ) { continue; }
            found = true;
            geo::geometry_brush_side_attributes_t attributes{};
            status = geo::BrushSideAttributeStore_TryGet( &source.attributes, side.iAttributeIndex, &attributes );
            if ( status != OK ) { break; }
            const string_view_t path = MapMaterials_Path( &map->materials, attributes.material.value );
            pOut->material.assign( path.pData != nullptr ? path.pData : "", path.cchLength );
            pOut->bBrush = true;
            for ( usize f = 0; f < boundary.faces.nCount; ++f ) {
                const auto &face = boundary.faces.pData[f];
                if ( face.iSide != i ) { continue; }
                for ( u32 k = 0; k < face.cVertices; ++k ) {
                    const vec3 p = boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]];
                    math::vec2d_t uv{};
                    if ( !math::Uvd_TryProjectPlanarPoint( attributes.uvProjection, p, map->geometryPolicy.numerical.fAbsoluteDistanceTolerance, &uv ) ) { status = geo::geometry_status_t::NUMERIC_FAILURE; break; }
                    pOut->corners.push_back( p );
                    pOut->uvs.push_back( uv );
                }
            }
        }
        geo::BrushBoundary_Shutdown( &boundary );
        geo::BrushSource_Shutdown( &source );
        if ( status != OK ) { return Status( status ); }
        return found && !pOut->corners.empty() ? map_status_t::OK : map_status_t::UNKNOWN_OBJECT;
    }
    if ( const auto *mesh = geo::GeometryDocument_FindMesh( &map->geometry, id ) ) {
        geo::mesh_source_description_t d{};
        auto status = geo::MeshSourceDescription_Init( &d, map->pAllocator, id );
        if ( status == OK ) { status = geo::MeshSource_TryDescribe( mesh, &d ); }
        bool found = false;
        for ( usize f = 0; status == OK && f < d.faces.nCount && !found; ++f ) {
            const auto &face = d.faces.pData[f];
            if ( face.sourceId.value != surface.face ) { continue; }
            found = true;
            const string_view_t path = MapMaterials_Path( &map->materials, face.attributes.material.value );
            pOut->material.assign( path.pData != nullptr ? path.pData : "", path.cchLength );
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                const auto &corner = d.corners.pData[face.iFirstCorner + k];
                pOut->corners.push_back( d.vertices.pData[corner.iVertex].position );
                pOut->uvs.push_back( corner.attributes.uv0 );
            }
        }
        geo::MeshSourceDescription_Shutdown( &d );
        if ( status != OK ) { return Status( status ); }
        return found ? map_status_t::OK : map_status_t::UNKNOWN_OBJECT;
    }
    return map_status_t::UNKNOWN_OBJECT;
}

namespace
{
struct uv_edit_job_t {
    const std::vector<u64> *pFaces{};
    const map_uv_edit_t *pEdit{};
};

geo::geometry_status_t MeshUvEdit( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept
{
    const auto &job = *static_cast<const uv_edit_job_t *>( context );
    const map_uv_edit_t &e = *job.pEdit;
    std::vector<geo::geometry_mesh_face_handle_t> handles;
    for ( u64 face : *job.pFaces ) {
        geo::geometry_mesh_face_handle_t handle{};
        if ( !geo::MeshSource_TryFindFace( working, { face }, &handle ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
        handles.push_back( handle );
    }
    const span_t<const geo::geometry_mesh_face_handle_t> span{ handles.data(), handles.size() };
    const auto set = geo::mesh_uv_set_t::MATERIAL;
    if ( e.kind == map_uv_edit_kind_t::SHEAR ) { return geo::MeshUv_TryShear( &working->attributes, &working->mesh, span, set, e.shear, e.pivot ); }
    geo::mesh_uv_transform_t transform{};
    if ( e.kind == map_uv_edit_kind_t::SHIFT ) { transform.shift = e.delta; }
    else if ( e.kind == map_uv_edit_kind_t::ROTATE ) { transform.rotationRadians = e.radians; }
    else { transform.scale = e.factor; }
    return geo::MeshUv_TryTransform( &working->attributes, &working->mesh, span, set, transform, geo::mesh_uv_pivot_t::POINT, e.pivot );
}

// A brush side's projection, changed as the edit says: the pivot in texture
// space is found on the side's plane, so the texture turns or grows about
// that texel.
geo::geometry_status_t EditSide( geo::geometry_brush_side_attributes_t &a, const map_uv_edit_t &e, const geo::geometry_policy_t &policy ) noexcept
{
    const f64 tolerance = policy.numerical.fAbsoluteDistanceTolerance;
    vec3 pivot{};
    switch ( e.kind ) {
        case map_uv_edit_kind_t::SHIFT: return geo::UvTools_TryShift( &a, e.delta, policy.numerical );
        case map_uv_edit_kind_t::ROTATE:
            if ( !math::Uvd_TryUnprojectPlanarPoint( a.uvProjection, e.pivot, 0.0, tolerance, &pivot ) ) { return geo::geometry_status_t::NUMERIC_FAILURE; }
            return geo::UvTools_TryRotateAbout( &a, pivot, e.radians, policy.numerical );
        // In UV space, not along the texture's own axes: once it is turned
        // the two differ, and the side's axes turn in the face plane to hold it.
        case map_uv_edit_kind_t::SCALE: return geo::UvTools_TryTransformUv( &a, { e.factor.x, 0.0 }, { 0.0, e.factor.y }, e.pivot, policy.numerical );
        case map_uv_edit_kind_t::SHEAR: return geo::UvTools_TryTransformUv( &a, { 1.0, e.shear.x }, { e.shear.y, 1.0 }, e.pivot, policy.numerical );
    }
    return geo::geometry_status_t::INVALID_ARGUMENT;
}
} // namespace

map_status_t MapTextureEdit_EditUv( map_document_t *map, span_t<const map_surface_ref_t> surfaces, const map_uv_edit_t &edit ) noexcept
{
    if ( map == nullptr || !Span_IsValid( surfaces ) || surfaces.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( edit.kind == map_uv_edit_kind_t::SCALE && ( !( std::fabs( edit.factor.x ) >= 1e-6 ) || !( std::fabs( edit.factor.y ) >= 1e-6 ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    try {
        std::vector<u64> objects;
        for ( usize i = 0; i < surfaces.nCount; ++i ) {
            if ( surfaces.pData[i].object == 0 || surfaces.pData[i].face == 0 ) { return map_status_t::INVALID_ARGUMENT; }
            if ( std::find( objects.begin(), objects.end(), surfaces.pData[i].object ) == objects.end() ) { objects.push_back( surfaces.pData[i].object ); }
        }
        for ( u64 id : objects ) {
            std::vector<u64> faces;
            for ( usize i = 0; i < surfaces.nCount; ++i ) { if ( surfaces.pData[i].object == id ) { faces.push_back( surfaces.pData[i].face ); } }
            if ( geo::GeometryDocument_FindMesh( &map->geometry, { id } ) != nullptr ) {
                const uv_edit_job_t job{ &faces, &edit };
                const map_status_t result = MapMeshEdit_Apply( map, id, &MeshUvEdit, const_cast<uv_edit_job_t *>( &job ) );
                if ( result != map_status_t::OK ) { return result; }
                continue;
            }
            if ( geo::GeometryDocument_FindBrush( &map->geometry, { id } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
            // Each listed side, its record split from any other side's first
            // (as BrushOne does), so editing one face moves no other.
            geo::brush_source_t source{};
            auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { id }, map->pAllocator, &source );
            std::vector<u32> users;
            for ( usize i = 0; status == OK && i < source.solid.sides.nCount; ++i ) {
                const u32 record = source.solid.sides.pData[i].iAttributeIndex;
                if ( record >= users.size() ) { users.resize( record + 1u, 0u ); }
                ++users[record];
            }
            usize touched = 0;
            for ( usize i = 0; status == OK && i < source.solid.sides.nCount; ++i ) {
                auto &side = source.solid.sides.pData[i];
                if ( std::find( faces.begin(), faces.end(), side.sourceId.value ) == faces.end() ) { continue; }
                geo::geometry_brush_side_attributes_t attributes{};
                status = geo::BrushSideAttributeStore_TryGet( &source.attributes, side.iAttributeIndex, &attributes );
                if ( status == OK ) { status = EditSide( attributes, edit, map->geometryPolicy ); }
                if ( status != OK ) { break; }
                if ( users[side.iAttributeIndex] > 1u ) {
                    usize record = 0;
                    status = geo::BrushSideAttributeStore_TryAppend( &source.attributes, map->geometryPolicy, attributes, &record );
                    if ( status == OK ) {
                        --users[side.iAttributeIndex];
                        side.iAttributeIndex = static_cast<u32>( record );
                        if ( record >= users.size() ) { users.resize( record + 1u, 0u ); }
                        users[record] = 1u;
                    }
                } else {
                    status = geo::BrushSideAttributeStore_TrySet( &source.attributes, map->geometryPolicy.numerical, side.iAttributeIndex, attributes );
                }
                ++touched;
            }
            if ( status == OK && touched != faces.size() ) { status = geo::geometry_status_t::INVALID_HANDLE; }
            if ( status == OK ) { status = geo::BrushSource_Validate( &source, map->geometryPolicy ).status; }
            if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &source.solid.sourceId, 1u }, { &source, 1u } ); }
            if ( status == OK ) { ++map->geometry.revision; }
            geo::BrushSource_Shutdown( &source );
            if ( status == geo::geometry_status_t::UNSUPPORTED ) { return map_status_t::INVALID_ARGUMENT; }
            if ( status != OK ) { return Status( status ); }
        }
        return map_status_t::OK;
    } catch ( ... ) { return map_status_t::OUT_OF_MEMORY; }
}

} // namespace cypher::editor::map
