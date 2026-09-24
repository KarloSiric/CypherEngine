//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushVertexEdit.cpp
//  Purpose: Implements convex-hull-based brush vertex editing with side
//           identity matching.
//  Details: Matching is a greedy assignment over (hull facet, old face)
//           pairs scored by how many of the old face's vertices, at their
//           new positions, lie on the facet's plane. Pairs are taken in
//           order of score, then normal agreement, then index, so the
//           result is deterministic; a pair needs at least three
//           coincident vertices, the minimum that pins a plane.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushVertexEdit.h"

#include "CypherCommon_Sort.h"

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::usize;
using math::f64;
using math::vec3d_t;

struct match_t {
    u32 score;
    f64 alignment;
    u32 iFacet;
    u32 iFace;
};

struct match_less_t {
    CYPHER_NODISCARD bool_t operator()( const match_t &a, const match_t &b ) const noexcept
    {
        if ( a.score != b.score ) {
            return a.score > b.score;
        }
        if ( a.alignment != b.alignment ) {
            return a.alignment > b.alignment;
        }
        if ( a.iFacet != b.iFacet ) {
            return a.iFacet < b.iFacet;
        }
        return a.iFace < b.iFace;
    }
};

struct value_pin_t {
    const geometry_brush_value_t *p{ nullptr };
    ~value_pin_t() noexcept { BrushValue_Release( p ); }
};

struct piece_holder_t {
    geometry_brush_piece_t piece{};
    ~piece_holder_t() noexcept { BrushPiece_Shutdown( &piece ); }
};

struct boundary_holder_t {
    brush_boundary_t boundary{};
    ~boundary_holder_t() noexcept { BrushBoundary_Shutdown( &boundary ); }
};

geometry_status_t Rebuild(
    geometry_transaction_t *pTransaction,
    const geometry_brush_value_t *pSource,
    const vec3d_t *pPositions,
    geometry_vertex_edit_result_t *pResult ) noexcept
{
    geometry_document_t *pDocument = pTransaction->pDocument;
    const geometry_policy_t &policy = pDocument->policy;
    const common::allocator_t *pAllocator = pDocument->pAllocator;
    const brush_boundary_t &old = pSource->boundary;
    const usize cVertices = common::Vector_Count( &old.vertices );
    const f64 tolerance = policy.numerical.fCoplanarDistanceTolerance;
    const f64 weldSq = policy.numerical.fWeldDistance * policy.numerical.fWeldDistance;

    // ---- Hull -----------------------------------------------------------------
    piece_holder_t hull{};
    geometry_status_t status = BrushPiece_Init( &hull.piece, pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryFromPoints( &hull.piece, { pPositions, cVertices }, policy );
    }
    boundary_holder_t hullBoundary{};
    if ( status == geometry_status_t::OK ) {
        status = BrushBoundary_Init( &hullBoundary.boundary, pAllocator );
    }
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryBuildBoundary( &hull.piece, policy, &hullBoundary.boundary );
    }
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    // Every moved vertex must survive as a hull vertex.
    for ( usize i = 0u; i < cVertices; ++i ) {
        if ( math::Vec3d_EqualsExact( pPositions[i], old.vertices.pData[i] ) ) {
            continue;
        }
        ++pResult->cMovedVertices;
        bool_t bFound = false;
        for ( usize h = 0u; h < common::Vector_Count( &hullBoundary.boundary.vertices ) && !bFound; ++h ) {
            bFound = math::Vec3d_DistanceSquared( hullBoundary.boundary.vertices.pData[h], pPositions[i] ) <=
                     weldSq;
        }
        if ( !bFound ) {
            return geometry_status_t::DEGENERATE;
        }
    }

    // ---- Match hull facets to old faces ---------------------------------------
    const usize cFacets = BrushPiece_PlaneCount( &hull.piece );
    const usize cFaces = common::Vector_Count( &old.faces );
    common::vector_t<match_t> matches{};
    if ( !common::Vector_Init( &matches, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize h = 0u; h < cFacets; ++h ) {
        const math::planed_t facet = hull.piece.planes.pData[h].plane;
        for ( usize f = 0u; f < cFaces; ++f ) {
            const brush_boundary_face_t &face = old.faces.pData[f];
            u32 score = 0u;
            for ( u32 k = 0u; k < face.cVertices; ++k ) {
                const vec3d_t p = pPositions[old.faceVertexIndices.pData[face.iFirstIndex + k]];
                if ( math::Scalar_Abs( math::Planed_SignedDistance( facet, p ) ) <= tolerance ) {
                    ++score;
                }
            }
            if ( score >= 3u ) {
                const f64 alignment = math::Vec3d_Dot(
                    facet.normal, pSource->brush.sides.pData[face.iSide].plane.normal );
                if ( !common::Vector_PushBack( &matches, match_t{ score, alignment,
                                                                  static_cast<u32>( h ),
                                                                  static_cast<u32>( f ) } ) ) {
                    return geometry_status_t::ALLOCATION_FAILED;
                }
            }
        }
    }
    common::Sort_Unstable( common::span_t<match_t>{ matches.pData, common::Vector_Count( &matches ) },
                           match_less_t{} );

    common::vector_t<u32> facetToFace{};
    common::vector_t<common::u8> faceTaken{};
    if ( !common::Vector_Init( &facetToFace, pAllocator, cFacets ) ||
         !common::Vector_Resize( &facetToFace, cFacets ) ||
         !common::Vector_Init( &faceTaken, pAllocator, cFaces ) ||
         !common::Vector_Resize( &faceTaken, cFaces ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize h = 0u; h < cFacets; ++h ) {
        facetToFace.pData[h] = common::CY_U32_MAX;
    }
    for ( usize f = 0u; f < cFaces; ++f ) {
        faceTaken.pData[f] = 0u;
    }
    for ( usize m = 0u; m < common::Vector_Count( &matches ); ++m ) {
        const match_t &match = matches.pData[m];
        if ( facetToFace.pData[match.iFacet] == common::CY_U32_MAX && faceTaken.pData[match.iFace] == 0u ) {
            facetToFace.pData[match.iFacet] = match.iFace;
            faceTaken.pData[match.iFace] = 1u;
        }
    }

    for ( usize h = 0u; h < cFacets; ++h ) {
        geometry_piece_plane_t &plane = hull.piece.planes.pData[h];
        if ( facetToFace.pData[h] == common::CY_U32_MAX ) {
            plane.origin = geometry_piece_plane_origin_t::NONE;
            ++pResult->cNewSides;
            continue;
        }
        const brush_solid_side_t &side =
            pSource->brush.sides.pData[old.faces.pData[facetToFace.pData[h]].iSide];
        plane.origin = geometry_piece_plane_origin_t::SOURCE_SIDE;
        plane.sourceBrushId = pSource->brush.sourceId;
        plane.sourceSideId = side.sourceId;
        ++pResult->cKeptSides;
    }
    pResult->cLostSides = static_cast<u32>( BrushSolid_SideCount( &pSource->brush ) - pResult->cKeptSides );

    // ---- Materialize and preview ------------------------------------------------
    const geometry_brush_value_t *donors[1] = { pSource };
    geometry_materialize_desc_t desc{};
    desc.pPiece = &hull.piece;
    desc.brushId = pSource->brush.sourceId;
    desc.bReuseSideIds = true;
    desc.donors = { donors, 1u };
    const geometry_brush_value_t *pValue = nullptr;
    status = GeometryMaterialize_TryPiece( pTransaction, desc, &pValue );
    if ( status == geometry_status_t::OK ) {
        status = GeometryTransaction_TryPreviewReplace( pTransaction, pValue );
    }
    BrushValue_Release( pValue );
    return status;
}

geometry_status_t PinPreview(
    geometry_transaction_t *pTransaction, geometry_source_id_t brushId, value_pin_t *pPin ) noexcept
{
    const geometry_brush_value_t *pValue = nullptr;
    if ( GeometryTransaction_TryGetPreview( pTransaction, brushId, &pValue ) != geometry_status_t::OK ) {
        return geometry_status_t::INVALID_HANDLE;
    }
    BrushValue_AddRef( pValue );
    pPin->p = pValue;
    return geometry_status_t::OK;
}

} // namespace

geometry_status_t BrushVertexEdit_TrySetVertexPositions(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    common::span_t<const vec3d_t> positions,
    geometry_vertex_edit_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    value_pin_t source{};
    geometry_status_t status = PinPreview( pTransaction, brushId, &source );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    if ( !common::Span_IsValid( positions ) ||
         positions.nCount != common::Vector_Count( &source.p->boundary.vertices ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0u; i < positions.nCount; ++i ) {
        if ( !math::Vec3d_IsFinite( positions.pData[i] ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    geometry_vertex_edit_result_t result{};
    status = Rebuild( pTransaction, source.p, positions.pData, &result );
    if ( status == geometry_status_t::OK ) {
        *pResultOut = result;
    }
    return status;
}

geometry_status_t BrushVertexEdit_TryMoveComponents(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    common::span_t<const geometry_component_ref_t> components,
    vec3d_t delta,
    geometry_vertex_edit_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    if ( !GeometryTransaction_IsActive( pTransaction ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !common::Span_IsValid( components ) || components.nCount == 0u ||
         !math::Vec3d_IsFinite( delta ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    value_pin_t source{};
    geometry_status_t status = PinPreview( pTransaction, brushId, &source );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    const brush_boundary_t &boundary = source.p->boundary;
    const usize cVertices = common::Vector_Count( &boundary.vertices );

    common::vector_t<common::u8> moved{};
    common::vector_t<vec3d_t> positions{};
    if ( !common::Vector_Init( &moved, pTransaction->pDocument->pAllocator, cVertices ) ||
         !common::Vector_Resize( &moved, cVertices ) ||
         !common::Vector_Init( &positions, pTransaction->pDocument->pAllocator, cVertices ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        moved.pData[i] = 0u;
    }
    for ( usize c = 0u; c < components.nCount; ++c ) {
        const geometry_component_ref_t &ref = components.pData[c];
        usize index = 0u;
        if ( ref.brushId.value != brushId.value ||
             ComponentRef_TryResolve( source.p, ref, &index ) != geometry_status_t::OK ) {
            return geometry_status_t::INVALID_ARGUMENT;
        }
        switch ( ref.kind ) {
            case geometry_component_kind_t::BRUSH:
                for ( usize i = 0u; i < cVertices; ++i ) {
                    moved.pData[i] = 1u;
                }
                break;
            case geometry_component_kind_t::BRUSH_VERTEX:
                moved.pData[index] = 1u;
                break;
            case geometry_component_kind_t::BRUSH_EDGE:
                moved.pData[boundary.edges.pData[index].iVertex0] = 1u;
                moved.pData[boundary.edges.pData[index].iVertex1] = 1u;
                break;
            case geometry_component_kind_t::BRUSH_SIDE: {
                usize iFace = 0u;
                if ( BrushBoundary_TryFindFaceForSide( &boundary, index, &iFace ) != geometry_status_t::OK ) {
                    return geometry_status_t::CORRUPT_STATE;
                }
                const brush_boundary_face_t &face = boundary.faces.pData[iFace];
                for ( u32 k = 0u; k < face.cVertices; ++k ) {
                    moved.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]] = 1u;
                }
                break;
            }
            default:
                return geometry_status_t::INVALID_ARGUMENT;
        }
    }
    for ( usize i = 0u; i < cVertices; ++i ) {
        ( void )common::Vector_PushBack(
            &positions, moved.pData[i] != 0u ? math::Vec3d_Add( boundary.vertices.pData[i], delta )
                                             : boundary.vertices.pData[i] );
    }
    geometry_vertex_edit_result_t result{};
    status = Rebuild( pTransaction, source.p, positions.pData, &result );
    if ( status == geometry_status_t::OK ) {
        *pResultOut = result;
    }
    return status;
}

} // namespace cypher::editor::geometry
