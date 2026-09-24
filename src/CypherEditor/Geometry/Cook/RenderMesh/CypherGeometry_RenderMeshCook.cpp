//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_RenderMeshCook.cpp
//  Purpose: Implements the canonical render-mesh cook.
//  Details: A first pass lists every face with its material and sort key;
//           the list is sorted into canonical order; a second pass emits
//           vertices, indices, and sources with capacity reserved up front.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_RenderMeshCook.h"

#include "CypherCommon_Sort.h"
#include "CypherCommon_StableHash.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::u64;
using common::usize;
using math::f64;
using math::vec3d_t;

// "CYGDRMC1": domain for render-mesh content hashes.
constexpr common::stable_hash_domain_t cHashDomain = 0x31434D5244475943ull;
constexpr u32 cHashSchema = 1u;

struct face_key_t {
    u64 material;
    u32 iEntry; // snapshot entry (ascending brush ID)
    u32 iFace;  // boundary face (ascending side order)
};

struct face_key_less_t {
    CYPHER_NODISCARD bool_t operator()( const face_key_t &a, const face_key_t &b ) const noexcept
    {
        if ( a.material != b.material ) {
            return a.material < b.material;
        }
        if ( a.iEntry != b.iEntry ) {
            return a.iEntry < b.iEntry;
        }
        return a.iFace < b.iFace;
    }
};

void ClearMesh( geometry_render_mesh_t *pMesh ) noexcept
{
    common::Vector_Clear( &pMesh->vertices );
    common::Vector_Clear( &pMesh->indices );
    common::Vector_Clear( &pMesh->batches );
    common::Vector_Clear( &pMesh->triangleSources );
    pMesh->bounds = math::aabb_t{};
    pMesh->sourceRevision = 0u;
    pMesh->contentHash = 0u;
}

bool_t ToF32( vec3d_t value, math::vec3_t *pOut ) noexcept
{
    return math::Vec3d_TryToVec3( value, pOut );
}

bool_t HashMesh( const geometry_render_mesh_t *pMesh, u64 *pHashOut ) noexcept
{
    common::stable_hash_builder_t builder{};
    if ( !common::StableHash_Begin( &builder, cHashDomain, cHashSchema ) ) {
        return false;
    }
    bool_t bOk = common::StableHash_WriteU64( &builder, common::Vector_Count( &pMesh->vertices ) ) &&
                 common::StableHash_WriteU64( &builder, common::Vector_Count( &pMesh->indices ) ) &&
                 common::StableHash_WriteU64( &builder, common::Vector_Count( &pMesh->batches ) );
    for ( usize i = 0u; bOk && i < common::Vector_Count( &pMesh->vertices ); ++i ) {
        const geometry_render_vertex_t &v = pMesh->vertices.pData[i];
        const math::f32 fields[14] = { v.position.x, v.position.y, v.position.z,
                                       v.normal.x,   v.normal.y,   v.normal.z,
                                       v.tangent[0], v.tangent[1], v.tangent[2], v.tangent[3],
                                       v.uv.x,       v.uv.y,       0.0f,         0.0f };
        for ( usize k = 0u; bOk && k < 12u; ++k ) {
            bOk = common::StableHash_WriteF32( &builder, fields[k] );
        }
    }
    for ( usize i = 0u; bOk && i < common::Vector_Count( &pMesh->indices ); ++i ) {
        bOk = common::StableHash_WriteU32( &builder, pMesh->indices.pData[i] );
    }
    for ( usize i = 0u; bOk && i < common::Vector_Count( &pMesh->batches ); ++i ) {
        const geometry_render_batch_t &b = pMesh->batches.pData[i];
        bOk = common::StableHash_WriteU64( &builder, b.material.value ) &&
              common::StableHash_WriteU32( &builder, b.iFirstIndex ) &&
              common::StableHash_WriteU32( &builder, b.cIndices );
    }
    for ( usize i = 0u; bOk && i < common::Vector_Count( &pMesh->triangleSources ); ++i ) {
        const geometry_cook_source_t &s = pMesh->triangleSources.pData[i];
        bOk = common::StableHash_WriteU8( &builder, static_cast<common::u8>( s.representation ) ) &&
              common::StableHash_WriteU64( &builder, s.rootId.value ) &&
              common::StableHash_WriteU64( &builder, s.componentId.value );
    }
    common::hash64_t hash{};
    if ( !common::StableHash_End( &builder, &hash ) || !bOk ) {
        return false;
    }
    *pHashOut = static_cast<u64>( hash );
    return true;
}

geometry_status_t Build(
    geometry_render_mesh_t *pMesh, const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    const common::allocator_t *pAllocator = pMesh->vertices.pAllocator;
    const geometry_numerical_policy_t &numerical = pSnapshot->policy.numerical;

    // ---- Pass 1: list faces and size the output ---------------------------
    usize cFaces = 0u;
    usize cVertices = 0u;
    usize cTriangles = 0u;
    for ( usize e = 0u; e < pSnapshot->cBrushes; ++e ) {
        const brush_boundary_t &boundary = pSnapshot->pBrushes[e].pValue->boundary;
        for ( usize f = 0u; f < common::Vector_Count( &boundary.faces ); ++f ) {
            ++cFaces;
            cVertices += boundary.faces.pData[f].cVertices;
            cTriangles += boundary.faces.pData[f].cVertices - 2u;
        }
    }
    if ( cVertices > static_cast<usize>( common::CY_U32_MAX ) ||
         pSnapshot->cBrushes > static_cast<usize>( common::CY_U32_MAX ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    common::vector_t<face_key_t> keys{};
    if ( !common::Vector_Init( &keys, pAllocator, cFaces ) ||
         !common::Vector_Reserve( &pMesh->vertices, cVertices ) ||
         !common::Vector_Reserve( &pMesh->indices, 3u * cTriangles ) ||
         !common::Vector_Reserve( &pMesh->triangleSources, cTriangles ) ||
         !common::Vector_Reserve( &pMesh->batches, cFaces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize e = 0u; e < pSnapshot->cBrushes; ++e ) {
        const geometry_brush_value_t *pValue = pSnapshot->pBrushes[e].pValue;
        for ( usize f = 0u; f < common::Vector_Count( &pValue->boundary.faces ); ++f ) {
            const brush_solid_side_t &side =
                pValue->brush.sides.pData[pValue->boundary.faces.pData[f].iSide];
            const face_key_t key{ pValue->attributes.records.pData[side.iAttributeIndex].material.value,
                                  static_cast<u32>( e ), static_cast<u32>( f ) };
            ( void )common::Vector_PushBack( &keys, key );
        }
    }
    common::Sort_Unstable( common::span_t<face_key_t>{ keys.pData, cFaces }, face_key_less_t{} );

    // ---- Pass 2: emit ------------------------------------------------------
    math::aabbd_t bounds = math::CY_AABBD_EMPTY;
    for ( usize k = 0u; k < cFaces; ++k ) {
        const face_key_t &key = keys.pData[k];
        const geometry_brush_value_t *pValue = pSnapshot->pBrushes[key.iEntry].pValue;
        const brush_boundary_t &boundary = pValue->boundary;
        const brush_boundary_face_t &face = boundary.faces.pData[key.iFace];
        const brush_solid_side_t &side = pValue->brush.sides.pData[face.iSide];
        const math::planar_uv_mappingd_t &uv =
            pValue->attributes.records.pData[side.iAttributeIndex].uvProjection;

        // Tangent: projection U axis flattened into the face plane.
        const vec3d_t n = side.plane.normal;
        vec3d_t tangent{};
        if ( !math::Vec3d_TryNormalize(
                 math::Vec3d_Subtract( uv.uAxis, math::Vec3d_Scale( n, math::Vec3d_Dot( uv.uAxis, n ) ) ),
                 1.0e-12, &tangent, nullptr ) ) {
            tangent = math::Vec3d_Cross( uv.vAxis, n );
        }
        const f64 handedness =
            math::Vec3d_Dot( math::Vec3d_Cross( n, tangent ), uv.vAxis ) < 0.0 ? -1.0 : 1.0;

        // New batch when the material changes.
        const usize cBatches = common::Vector_Count( &pMesh->batches );
        if ( cBatches == 0u || pMesh->batches.pData[cBatches - 1u].material.value != key.material ) {
            const geometry_render_batch_t batch{
                geometry_material_ref_t{ key.material },
                static_cast<u32>( common::Vector_Count( &pMesh->indices ) ), 0u };
            ( void )common::Vector_PushBack( &pMesh->batches, batch );
        }

        const u32 iBase = static_cast<u32>( common::Vector_Count( &pMesh->vertices ) );
        for ( u32 i = 0u; i < face.cVertices; ++i ) {
            const vec3d_t p =
                boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + i]];
            math::vec2d_t uvCoord{};
            if ( !math::Uvd_TryProjectPlanarPoint( uv, p, numerical.fAbsoluteDistanceTolerance,
                                                   &uvCoord ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            geometry_render_vertex_t vertex{};
            math::vec3_t tangent32{};
            if ( !ToF32( p, &vertex.position ) || !ToF32( n, &vertex.normal ) ||
                 !ToF32( tangent, &tangent32 ) || !math::Vec2d_TryToVec2( uvCoord, &vertex.uv ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
            vertex.tangent[0] = tangent32.x;
            vertex.tangent[1] = tangent32.y;
            vertex.tangent[2] = tangent32.z;
            vertex.tangent[3] = static_cast<math::f32>( handedness );
            ( void )common::Vector_PushBack( &pMesh->vertices, vertex );
            bounds = math::Aabbd_ExpandPoint( bounds, p );
        }
        const geometry_cook_source_t source{ geometry_source_representation_kind_t::BRUSH_SOLID,
                                             pValue->brush.sourceId, side.sourceId };
        for ( u32 i = 1u; i + 1u < face.cVertices; ++i ) {
            ( void )common::Vector_PushBack( &pMesh->indices, iBase );
            ( void )common::Vector_PushBack( &pMesh->indices, iBase + i );
            ( void )common::Vector_PushBack( &pMesh->indices, iBase + i + 1u );
            ( void )common::Vector_PushBack( &pMesh->triangleSources, source );
        }
        pMesh->batches.pData[common::Vector_Count( &pMesh->batches ) - 1u].cIndices +=
            3u * ( face.cVertices - 2u );
    }

    if ( cFaces > 0u ) {
        math::aabb_t bounds32{};
        if ( !math::Aabbd_TryToAabb( bounds, &bounds32 ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        pMesh->bounds = bounds32;
    }
    pMesh->sourceRevision = pSnapshot->revision;
    if ( !HashMesh( pMesh, &pMesh->contentHash ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t RenderMeshCook_Init(
    geometry_render_mesh_t *pMesh, const common::allocator_t *pAllocator ) noexcept
{
    if ( pMesh == nullptr || !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pMesh->vertices.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pMesh->vertices, pAllocator, 0u ) ||
         !common::Vector_Init( &pMesh->indices, pAllocator, 0u ) ||
         !common::Vector_Init( &pMesh->batches, pAllocator, 0u ) ||
         !common::Vector_Init( &pMesh->triangleSources, pAllocator, 0u ) ) {
        RenderMeshCook_Shutdown( pMesh );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void RenderMeshCook_Shutdown( geometry_render_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) {
        return;
    }
    // vector_t shuts itself down safely from any initialized or empty state.
    common::Vector_Shutdown( &pMesh->triangleSources );
    common::Vector_Shutdown( &pMesh->batches );
    common::Vector_Shutdown( &pMesh->indices );
    common::Vector_Shutdown( &pMesh->vertices );
    pMesh->sourceRevision = 0u;
    pMesh->contentHash = 0u;
}

geometry_status_t RenderMeshCook_TryBuild(
    geometry_render_mesh_t *pMesh, const geometry_document_snapshot_t *pSnapshot ) noexcept
{
    if ( pMesh == nullptr || pMesh->vertices.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    ClearMesh( pMesh );
    const geometry_status_t status = Build( pMesh, pSnapshot );
    if ( status != geometry_status_t::OK ) {
        ClearMesh( pMesh );
    }
    return status;
}

common::usize RenderMeshCook_TriangleCount( const geometry_render_mesh_t *pMesh ) noexcept
{
    return pMesh == nullptr ? 0u : common::Vector_Count( &pMesh->triangleSources );
}

} // namespace cypher::editor::geometry
