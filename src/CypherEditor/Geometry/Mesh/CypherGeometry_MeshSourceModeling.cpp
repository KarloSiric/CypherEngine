//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSourceModeling.cpp
//  Purpose: Implements identity-addressed modeling edits.
//  Details: Position-only edits (move, transform, smooth, snap) change no
//           topology, so every handle, identity, and attribute survives as
//           is and they skip the capture bracket entirely.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSourceModeling.h"

#include "CypherGeometry_MeshCleanup.h"
#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshTopologyOps.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool PositionOk( math::vec3d_t p ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= kMeshSourceCoordinateMax &&
           std::fabs( p.y ) <= kMeshSourceCoordinateMax && std::fabs( p.z ) <= kMeshSourceCoordinateMax;
}

f64 LinearDeterminant( const math::affine3d_t &t ) noexcept
{
    const math::vec3d_t c0 = math::Affine3d_Column( t, 0u ), c1 = math::Affine3d_Column( t, 1u ),
                        c2 = math::Affine3d_Column( t, 2u );
    return math::Vec3d_Dot( c0, math::Vec3d_Cross( c1, c2 ) );
}

geometry_status_t PreflightExtrudedFacePositions(
    const mesh_source_t *pSource,
    geometry_mesh_face_handle_t hFace,
    f64 distance ) noexcept
{
    const editable_mesh_t *pMesh = &pSource->mesh;
    const mesh_face_record_t *pFace =
        GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pLoop = pFace != nullptr
        ? GenerationPool_Get( &pMesh->loops, pFace->hOuterLoop )
        : nullptr;
    if ( pFace == nullptr || pLoop == nullptr ||
         pLoop->cHalfEdges < 3u ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const math::vec3d_t offset =
        math::Vec3d_Scale( pFace->normal, distance );
    geometry_mesh_half_edge_handle_t hCurrent =
        pLoop->hFirstHalfEdge;
    for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
        const mesh_half_edge_record_t *pHalfEdge =
            GenerationPool_Get( &pMesh->halfEdges, hCurrent );
        const mesh_vertex_record_t *pVertex = pHalfEdge != nullptr
            ? GenerationPool_Get(
                  &pMesh->vertices, pHalfEdge->hOrigin )
            : nullptr;
        if ( pHalfEdge == nullptr || pVertex == nullptr ) {
            return geometry_status_t::CORRUPT_STATE;
        }
        const math::vec3d_t position =
            math::Vec3d_Add( pVertex->position, offset );
        if ( !PositionOk( position ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
        if ( math::Vec3d_EqualsExact(
                 position, pVertex->position ) ) {
            return geometry_status_t::DEGENERATE;
        }
        hCurrent = pHalfEdge->hNext;
    }
    return geometry_status_t::OK;
}

bool TransformedFacesRemainViable(
    const editable_mesh_t *pMesh,
    const math::affine3d_t &transform ) noexcept
{
    constexpr f64 kMinimumNormalLengthSquared = 1.0e-30;
    bool bViable = true;
    (void)GenerationPool_ForEach(
        &pMesh->faces,
        [&]( geometry_mesh_face_handle_t,
             const mesh_face_record_t &face ) noexcept -> bool_t {
            const mesh_loop_record_t *pLoop =
                GenerationPool_Get(
                    &pMesh->loops, face.hOuterLoop );
            if ( pLoop == nullptr || pLoop->cHalfEdges < 3u ) {
                bViable = false;
                return false;
            }

            math::vec3d_t normal = math::Vec3d_Make(
                0.0, 0.0, 0.0 );
            geometry_mesh_half_edge_handle_t hCurrent =
                pLoop->hFirstHalfEdge;
            for ( u32 i = 0u; i < pLoop->cHalfEdges; ++i ) {
                const mesh_half_edge_record_t *pHalfEdge =
                    GenerationPool_Get(
                        &pMesh->halfEdges, hCurrent );
                const mesh_half_edge_record_t *pNext =
                    pHalfEdge != nullptr
                    ? GenerationPool_Get(
                          &pMesh->halfEdges, pHalfEdge->hNext )
                    : nullptr;
                const mesh_vertex_record_t *pA =
                    pHalfEdge != nullptr
                    ? GenerationPool_Get(
                          &pMesh->vertices, pHalfEdge->hOrigin )
                    : nullptr;
                const mesh_vertex_record_t *pB = pNext != nullptr
                    ? GenerationPool_Get(
                          &pMesh->vertices, pNext->hOrigin )
                    : nullptr;
                if ( pHalfEdge == nullptr || pNext == nullptr ||
                     pA == nullptr || pB == nullptr ) {
                    bViable = false;
                    return false;
                }

                const math::vec3d_t a =
                    math::Affine3d_TransformPoint(
                        transform, pA->position );
                const math::vec3d_t b =
                    math::Affine3d_TransformPoint(
                        transform, pB->position );
                normal.x += ( a.y - b.y ) * ( a.z + b.z );
                normal.y += ( a.z - b.z ) * ( a.x + b.x );
                normal.z += ( a.x - b.x ) * ( a.y + b.y );
                hCurrent = pHalfEdge->hNext;
            }

            const f64 lengthSquared =
                math::Vec3d_LengthSquared( normal );
            bViable = std::isfinite( lengthSquared ) &&
                      lengthSquared > kMinimumNormalLengthSquared;
            return bViable;
        } );
    return bViable;
}

// Returns the loop half-edge of face hFace that starts at hVertex.
bool CornerOf(
    const mesh_source_t *pSource,
    geometry_mesh_face_handle_t hFace,
    geometry_mesh_vertex_handle_t hVertex,
    geometry_mesh_half_edge_handle_t *pOut ) noexcept
{
    const editable_mesh_t *pMesh = &pSource->mesh;
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    const mesh_loop_record_t *pL = pF ? GenerationPool_Get( &pMesh->loops, pF->hOuterLoop ) : nullptr;
    if ( pL == nullptr ) { return false; }
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
        if ( pH == nullptr ) { return false; }
        if ( pH->hOrigin.nSlot == hVertex.nSlot && pH->hOrigin.nGeneration == hVertex.nGeneration ) {
            *pOut = h;
            return true;
        }
        h = pH->hNext;
    }
    return false;
}

} // namespace

geometry_status_t MeshSourceEdit_TryExtrudeFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    f64 distance,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( distance ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    geometry_mesh_face_handle_t hFace{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &hFace ) ) { return geometry_status_t::INVALID_HANDLE; }
    const geometry_status_t preflight =
        PreflightExtrudedFacePositions(
            pSource, hFace, distance );
    if ( preflight != geometry_status_t::OK ) { return preflight; }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            const mesh_extrude_face_result_t r = MeshOps_ExtrudeFace( &pSource->mesh, hFace, distance );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            // The cap *is* the extruded face: keep its identity. Side walls
            // are parented by the generic shared-vertex rule.
            return Vector_PushBack( pParents, mesh_edit_face_parent_t{ r.hExtrudedFace, hFace, true } )
                       ? geometry_status_t::OK
                       : geometry_status_t::ALLOCATION_FAILED;
        },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryInsetFace(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    f64 margin,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_face_handle_t hFace{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &hFace ) ) { return geometry_status_t::INVALID_HANDLE; }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> *pParents ) noexcept {
            const mesh_inset_face_result_t r = MeshOps_InsetFace( &pSource->mesh, hFace, margin );
            if ( r.status != geometry_status_t::OK ) { return r.status; }
            if ( GenerationPool_Contains( &pSource->mesh.faces, hFace ) ) {
                // The op kept the original handle as the inner face; its
                // identity is already in place.
                return geometry_status_t::OK;
            }
            return Vector_PushBack( pParents, mesh_edit_face_parent_t{ r.hInsetFace, hFace, true } )
                       ? geometry_status_t::OK
                       : geometry_status_t::ALLOCATION_FAILED;
        },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryBevelEdge(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    f64 width,
    u32 cSegments,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) ) { return geometry_status_t::INVALID_HANDLE; }
    return MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            return MeshOps_BevelEdge( &pSource->mesh, hEdge, width, cSegments ).status;
        },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TryLoopCut(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    f64 t,
    u32 *pFacesSplitOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pFacesSplitOut ) { *pFacesSplitOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) ) { return geometry_status_t::INVALID_HANDLE; }
    u32 cSplit = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_loop_cut_result_t r = MeshOps_LoopCut( &pSource->mesh, hEdge, t );
            cSplit = r.cFacesSplit;
            return r.status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pFacesSplitOut ) { *pFacesSplitOut = cSplit; }
    return st;
}

geometry_status_t MeshSourceEdit_TryTriangulate(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    u32 *pFacesCreatedOut,
    mesh_edit_report_t *pReportOut ) noexcept
{
    if ( pFacesCreatedOut ) { *pFacesCreatedOut = 0u; }
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_face_handle_t hFace{};
    const bool bOne = GeometrySourceId_IsValid( faceId );
    if ( bOne && !MeshSource_TryFindFace( pSource, faceId, &hFace ) ) { return geometry_status_t::INVALID_HANDLE; }
    u32 cCreated = 0u;
    const geometry_status_t st = MeshEdit_Bracket(
        pSource,
        [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept {
            const mesh_triangulate_result_t r = bOne ? MeshOps_TriangulateFace( &pSource->mesh, hFace )
                                                     : MeshOps_TriangulateFaces( &pSource->mesh );
            cCreated = r.cFacesCreated;
            return r.status;
        },
        pReportOut );
    if ( st == geometry_status_t::OK && pFacesCreatedOut ) { *pFacesCreatedOut = cCreated; }
    return st;
}

geometry_status_t MeshSourceEdit_TryMoveVertex( mesh_source_t *pSource, geometry_source_id_t vertexId, math::vec3d_t position ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !PositionOk( position ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    geometry_mesh_vertex_handle_t h{};
    if ( !MeshSource_TryFindVertex( pSource, vertexId, &h ) ) { return geometry_status_t::INVALID_HANDLE; }
    return MeshOps_MoveVertex( &pSource->mesh, h, position );
}

geometry_status_t MeshSourceEdit_TryTransform( mesh_source_t *pSource, const math::affine3d_t &transform ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const f64 det = LinearDeterminant( transform );
    if ( !std::isfinite( det ) || det == 0.0 ) { return geometry_status_t::DEGENERATE; }
    if ( det < 0.0 ) { return geometry_status_t::UNSUPPORTED; }
    editable_mesh_t *pMesh = &pSource->mesh;
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            bOk = PositionOk( math::Affine3d_TransformPoint( transform, v.position ) );
            return bOk;
        } );
    if ( !bOk ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( !TransformedFacesRemainViable( pMesh, transform ) ) {
        return geometry_status_t::DEGENERATE;
    }
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t, mesh_vertex_record_t &v ) noexcept -> bool_t {
            v.position = math::Affine3d_TransformPoint( transform, v.position );
            return true;
        } );
    return MeshCleanup_RecalculateNormals( pMesh ).status;
}

geometry_status_t MeshSourceEdit_TryMirror( mesh_source_t *pSource, math::planed_t plane, mesh_edit_report_t *pReportOut ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    editable_mesh_t *pMesh = &pSource->mesh;
    // Validate reflected positions first so a mirror out of range changes
    // nothing.
    math::planed_t unit = plane;
    if ( !math::Planed_TryNormalize( plane, 1e-12, &unit ) ) { return geometry_status_t::DEGENERATE; }
    bool bOk = true;
    (void)GenerationPool_ForEach( &pMesh->vertices,
        [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> bool_t {
            const f64 d = math::Vec3d_Dot( unit.normal, v.position ) + unit.d;
            bOk = PositionOk( math::Vec3d_Subtract( v.position, math::Vec3d_Scale( unit.normal, 2.0 * d ) ) );
            return bOk;
        } );
    if ( !bOk ) { return geometry_status_t::NUMERIC_FAILURE; }
    return MeshEdit_Bracket(
        pSource, [&]( vector_t<mesh_edit_face_parent_t> * ) noexcept { return MeshOps_Mirror( pMesh, unit ); },
        pReportOut );
}

geometry_status_t MeshSourceEdit_TrySmooth( mesh_source_t *pSource, f64 factor, u32 cIterations ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    return MeshOps_LaplacianSmooth( &pSource->mesh, factor, cIterations ).status;
}

geometry_status_t MeshSourceEdit_TrySnapToGrid( mesh_source_t *pSource, f64 spacing ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( spacing ) || spacing <= 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    bool bPositionsValid = true;
    (void)GenerationPool_ForEach(
        &pSource->mesh.vertices,
        [&]( geometry_mesh_vertex_handle_t,
             const mesh_vertex_record_t &vertex ) noexcept -> bool_t {
            const math::vec3d_t snapped = math::Vec3d_Make(
                std::round( vertex.position.x / spacing ) * spacing,
                std::round( vertex.position.y / spacing ) * spacing,
                std::round( vertex.position.z / spacing ) * spacing );
            bPositionsValid = PositionOk( snapped );
            return bPositionsValid;
        } );
    if ( !bPositionsValid ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    return MeshOps_SnapToGrid( &pSource->mesh, spacing ).status;
}

geometry_status_t MeshSourceEdit_TrySetFaceAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    const mesh_face_attributes_t &attributes ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_mesh_face_handle_t h{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &h ) ) { return geometry_status_t::INVALID_HANDLE; }
    return MeshAttributeStore_TrySetFace( &pSource->attributes, h, attributes, pSource->mesh.faces.cSlots );
}

geometry_status_t MeshSourceEdit_TrySetCornerAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t faceId,
    geometry_source_id_t vertexId,
    const mesh_corner_attributes_t &attributes ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !math::Vec2d_IsFinite( attributes.uv0 ) || !math::Vec2d_IsFinite( attributes.uv1 ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    geometry_mesh_face_handle_t hFace{};
    geometry_mesh_vertex_handle_t hVertex{};
    geometry_mesh_half_edge_handle_t hCorner{};
    if ( !MeshSource_TryFindFace( pSource, faceId, &hFace ) || !MeshSource_TryFindVertex( pSource, vertexId, &hVertex ) ||
         !CornerOf( pSource, hFace, hVertex, &hCorner ) ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    return MeshAttributeStore_TrySetCorner( &pSource->attributes, hCorner, attributes, pSource->mesh.halfEdges.cSlots );
}

geometry_status_t MeshSourceEdit_TrySetEdgeAttributes(
    mesh_source_t *pSource,
    geometry_source_id_t vertexA,
    geometry_source_id_t vertexB,
    const mesh_edge_attributes_t &attributes,
    f64 creaseWeight ) noexcept
{
    if ( !MeshSource_IsInitialized( pSource ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !std::isfinite( creaseWeight ) || creaseWeight < 0.0 || creaseWeight > 1.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_mesh_edge_handle_t hEdge{};
    if ( !MeshSourceEdit_TryFindEdge( pSource, vertexA, vertexB, &hEdge ) ) { return geometry_status_t::INVALID_HANDLE; }
    const geometry_status_t st =
        MeshAttributeStore_TrySetEdge( &pSource->attributes, hEdge, attributes, pSource->mesh.edges.cSlots );
    if ( st != geometry_status_t::OK ) { return st; }
    GenerationPool_Get( &pSource->mesh.edges, hEdge )->creaseWeight = creaseWeight;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
