//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentValidation.cpp
//  Purpose: Implements whole-document geometry diagnostics.
//  Details: Pair candidates come from a sort-and-sweep over brush bounds on
//           the x axis (Validation sits below Spatial in the dependency
//           order, so it carries its own broad phase). Candidate pairs are
//           sorted by brush ID before narrow-phase checks, which fixes the
//           diagnostic order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentValidation.h"

#include "CypherGeometry_BrushPiece.h"
#include "CypherGeometry_BrushQueries.h"
#include "CypherGeometry_BrushValidation.h"

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

struct pair_t {
    u32 iA; // snapshot entries, iA < iB (ascending brush ID)
    u32 iB;
};

struct pair_less_t {
    CYPHER_NODISCARD bool_t operator()( const pair_t &a, const pair_t &b ) const noexcept
    {
        return a.iA != b.iA ? a.iA < b.iA : a.iB < b.iB;
    }
};

struct sweep_less_t {
    const geometry_document_snapshot_t *pSnapshot;
    CYPHER_NODISCARD bool_t operator()( const u32 &a, const u32 &b ) const noexcept
    {
        const f64 xa = pSnapshot->pBrushes[a].pValue->bounds.minimum.x;
        const f64 xb = pSnapshot->pBrushes[b].pValue->bounds.minimum.x;
        return xa != xb ? xa < xb : a < b;
    }
};

bool_t BoundsTouch( const math::aabbd_t &a, const math::aabbd_t &b ) noexcept
{
    return a.minimum.x <= b.maximum.x && b.minimum.x <= a.maximum.x &&
           a.minimum.y <= b.maximum.y && b.minimum.y <= a.maximum.y &&
           a.minimum.z <= b.maximum.z && b.minimum.z <= a.maximum.z;
}

struct emitter_t {
    geometry_diagnostic_buffer_t *pBuffer;
    geometry_validation_summary_t *pSummary;

    void Emit( geometry_validation_code_t code, geometry_diagnostic_severity_t severity,
               geometry_diagnostic_target_t target,
               geometry_diagnostic_target_t related = {} ) noexcept
    {
        geometry_diagnostic_t diagnostic{};
        diagnostic.code = GeometryValidation_Code( code );
        diagnostic.module = geometry_diagnostic_module_t::VALIDATION;
        diagnostic.severity = severity;
        diagnostic.target = target;
        diagnostic.related = related;
        if ( severity == geometry_diagnostic_severity_t::ERROR ) {
            ++pSummary->cErrors;
        } else if ( severity == geometry_diagnostic_severity_t::WARNING ) {
            ++pSummary->cWarnings;
        } else {
            ++pSummary->cNotes;
        }
        if ( GeometryDiagnosticBuffer_Append( pBuffer, diagnostic ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY ) {
            ++pSummary->cTruncated;
        }
    }
};

geometry_diagnostic_target_t BrushTarget( const geometry_brush_value_t *pValue ) noexcept
{
    return GeometryDiagnosticTarget_Root( geometry_source_representation_kind_t::BRUSH_SOLID,
                                          pValue->brush.sourceId );
}

geometry_diagnostic_target_t SideTarget( const geometry_brush_value_t *pValue, usize iSide ) noexcept
{
    return GeometryDiagnosticTarget_Component(
        geometry_source_representation_kind_t::BRUSH_SOLID, pValue->brush.sourceId,
        pValue->brush.sides.pData[iSide].sourceId, GEOMETRY_DIAGNOSTIC_COMPONENT_BRUSH_SIDE );
}

// Volume of A ∩ B (0 when empty).
geometry_status_t IntersectionVolume(
    const geometry_brush_value_t *pA, const geometry_brush_value_t *pB,
    const geometry_policy_t &policy, f64 *pVolumeOut ) noexcept
{
    *pVolumeOut = 0.0;
    geometry_brush_piece_t piece{};
    geometry_status_t status = BrushPiece_Init( &piece, pA->pAllocator );
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryFromBrush( &piece, &pA->brush );
    }
    for ( usize i = 0u; status == geometry_status_t::OK && i < BrushSolid_SideCount( &pB->brush ); ++i ) {
        geometry_piece_plane_t plane{};
        plane.plane = pB->brush.sides.pData[i].plane;
        status = BrushPiece_TryAppendPlane( &piece, policy, plane );
    }
    geometry_piece_extent_t extent = geometry_piece_extent_t::EMPTY;
    if ( status == geometry_status_t::OK ) {
        status = BrushPiece_TryReduce( &piece, policy, &extent, nullptr );
    }
    if ( status == geometry_status_t::OK && extent == geometry_piece_extent_t::SOLID ) {
        brush_boundary_t boundary{};
        status = BrushBoundary_Init( &boundary, pA->pAllocator );
        if ( status == geometry_status_t::OK ) {
            status = BrushPiece_TryBuildBoundary( &piece, policy, &boundary );
        }
        vec3d_t centroid{};
        if ( status == geometry_status_t::OK ) {
            status = BrushQuery_TryVolumeCentroid( &boundary, pVolumeOut, &centroid );
            if ( status == geometry_status_t::DEGENERATE ) {
                *pVolumeOut = 0.0;
                status = geometry_status_t::OK;
            }
        }
        BrushBoundary_Shutdown( &boundary );
    }
    BrushPiece_Shutdown( &piece );
    return status;
}

void FaceRing( const geometry_brush_value_t *pValue, usize iFace,
               common::vector_t<vec3d_t> *pOut ) noexcept
{
    common::Vector_Clear( pOut );
    const brush_boundary_face_t &face = pValue->boundary.faces.pData[iFace];
    for ( u32 i = 0u; i < face.cVertices; ++i ) {
        ( void )common::Vector_PushBack(
            pOut, pValue->boundary.vertices.pData[
                      pValue->boundary.faceVertexIndices.pData[face.iFirstIndex + i]] );
    }
}

} // namespace

geometry_status_t GeometryValidation_TryCoplanarOverlapArea(
    const geometry_brush_value_t *pA, usize iFaceA,
    const geometry_brush_value_t *pB, usize iFaceB,
    f64 *pAreaOut ) noexcept
{
    if ( pAreaOut == nullptr || pA == nullptr || pB == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAreaOut = 0.0;
    if ( iFaceA >= common::Vector_Count( &pA->boundary.faces ) ||
         iFaceB >= common::Vector_Count( &pB->boundary.faces ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const vec3d_t nA = pA->brush.sides.pData[pA->boundary.faces.pData[iFaceA].iSide].plane.normal;
    const vec3d_t nB = pB->brush.sides.pData[pB->boundary.faces.pData[iFaceB].iSide].plane.normal;

    common::vector_t<vec3d_t> subject{};
    common::vector_t<vec3d_t> scratch{};
    common::vector_t<vec3d_t> clip{};
    const usize cMax = pA->boundary.faces.pData[iFaceA].cVertices +
                       pB->boundary.faces.pData[iFaceB].cVertices + 4u;
    if ( !common::Vector_Init( &subject, pA->pAllocator, cMax ) ||
         !common::Vector_Init( &scratch, pA->pAllocator, cMax ) ||
         !common::Vector_Init( &clip, pA->pAllocator, cMax ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    FaceRing( pA, iFaceA, &subject );
    FaceRing( pB, iFaceB, &clip );

    // Sutherland-Hodgman against B's edges. B's ring winds CCW about nB;
    // cross(nB, edge) points into B.
    const usize cClip = common::Vector_Count( &clip );
    for ( usize e = 0u; e < cClip && common::Vector_Count( &subject ) >= 3u; ++e ) {
        const vec3d_t a = clip.pData[e];
        const vec3d_t b = clip.pData[( e + 1u ) % cClip];
        const vec3d_t inward = math::Vec3d_Cross( nB, math::Vec3d_Subtract( b, a ) );
        common::Vector_Clear( &scratch );
        const usize cSubject = common::Vector_Count( &subject );
        for ( usize i = 0u; i < cSubject; ++i ) {
            const vec3d_t p = subject.pData[i];
            const vec3d_t q = subject.pData[( i + 1u ) % cSubject];
            const f64 dp = math::Vec3d_Dot( math::Vec3d_Subtract( p, a ), inward );
            const f64 dq = math::Vec3d_Dot( math::Vec3d_Subtract( q, a ), inward );
            if ( dp >= 0.0 ) {
                ( void )common::Vector_PushBack( &scratch, p );
            }
            if ( ( dp >= 0.0 ) != ( dq >= 0.0 ) ) {
                const f64 t = dp / ( dp - dq );
                ( void )common::Vector_PushBack(
                    &scratch, math::Vec3d_Add( p, math::Vec3d_Scale( math::Vec3d_Subtract( q, p ), t ) ) );
            }
        }
        common::Vector_Clear( &subject );
        for ( usize i = 0u; i < common::Vector_Count( &scratch ); ++i ) {
            ( void )common::Vector_PushBack( &subject, scratch.pData[i] );
        }
    }
    const usize cResult = common::Vector_Count( &subject );
    if ( cResult < 3u ) {
        return geometry_status_t::OK;
    }
    vec3d_t newell = math::CY_VEC3D_ZERO;
    for ( usize i = 0u; i < cResult; ++i ) {
        const vec3d_t p = subject.pData[i];
        const vec3d_t q = subject.pData[( i + 1u ) % cResult];
        newell.x += ( p.y - q.y ) * ( p.z + q.z );
        newell.y += ( p.z - q.z ) * ( p.x + q.x );
        newell.z += ( p.x - q.x ) * ( p.y + q.y );
    }
    const f64 area = 0.5 * math::Vec3d_Dot( newell, nA );
    *pAreaOut = area > 0.0 ? area : -area;
    return geometry_status_t::OK;
}

geometry_status_t GeometryValidation_TryValidateSnapshot(
    const geometry_document_snapshot_t *pSnapshot,
    const geometry_validation_options_t &options,
    geometry_diagnostic_buffer_t *pDiagnostics,
    geometry_validation_summary_t *pSummaryOut ) noexcept
{
    if ( pSummaryOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSummaryOut = {};
    if ( !GeometryDiagnosticBuffer_IsValid( pDiagnostics ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pSnapshot == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_policy_t &policy = pSnapshot->policy;
    emitter_t emitter{ pDiagnostics, pSummaryOut };
    const usize cBrushes = pSnapshot->cBrushes;

    // ---- Per-brush checks ----------------------------------------------------
    for ( usize i = 0u; i < cBrushes; ++i ) {
        const geometry_brush_value_t *pValue = pSnapshot->pBrushes[i].pValue;
        if ( options.bRevalidateValues ) {
            const geometry_status_t quick = BrushValidation_Quick( &pValue->brush, policy );
            const brush_validation_result_t deep =
                BrushValidation_CheckBoundary( &pValue->brush, &pValue->boundary, policy );
            if ( quick != geometry_status_t::OK || deep.status != geometry_status_t::OK ) {
                emitter.Emit( geometry_validation_code_t::REVALIDATION_FAILED,
                              geometry_diagnostic_severity_t::ERROR, BrushTarget( pValue ) );
            }
        }
        f64 volume = 0.0;
        vec3d_t centroid{};
        if ( BrushQuery_TryVolumeCentroid( &pValue->boundary, &volume, &centroid ) !=
                 geometry_status_t::OK ||
             volume < options.fTinyVolume ) {
            emitter.Emit( geometry_validation_code_t::TINY_BRUSH,
                          geometry_diagnostic_severity_t::WARNING, BrushTarget( pValue ) );
        }
        const vec3d_t size = math::Vec3d_Subtract( pValue->bounds.maximum, pValue->bounds.minimum );
        if ( size.x < options.fThinExtent || size.y < options.fThinExtent ||
             size.z < options.fThinExtent ) {
            emitter.Emit( geometry_validation_code_t::THIN_BRUSH,
                          geometry_diagnostic_severity_t::WARNING, BrushTarget( pValue ) );
        }
        const f64 reach = math::Vec3d_MaxComponent( math::Vec3d_Max(
            math::Vec3d_Abs( pValue->bounds.minimum ), math::Vec3d_Abs( pValue->bounds.maximum ) ) );
        if ( reach > options.fLimitFraction * policy.numerical.fCoordinateMagnitudeLimit ) {
            emitter.Emit( geometry_validation_code_t::NEAR_COORDINATE_LIMIT,
                          geometry_diagnostic_severity_t::WARNING, BrushTarget( pValue ) );
        }
        if ( options.bCheckMaterials ) {
            for ( usize s = 0u; s < BrushSolid_SideCount( &pValue->brush ); ++s ) {
                const brush_solid_side_t &side = pValue->brush.sides.pData[s];
                if ( !GeometryMaterialRef_IsAssigned(
                         pValue->attributes.records.pData[side.iAttributeIndex].material ) ) {
                    emitter.Emit( geometry_validation_code_t::UNASSIGNED_MATERIAL,
                                  geometry_diagnostic_severity_t::NOTE, SideTarget( pValue, s ) );
                }
            }
        }
    }

    if ( !options.bCheckOverlaps && !options.bCheckCoplanarFaces ) {
        return geometry_status_t::OK;
    }

    // ---- Broad phase: sort and sweep on x ----------------------------------
    const common::allocator_t *pAllocator = pSnapshot->pAllocator;
    common::vector_t<u32> order{};
    common::vector_t<pair_t> pairs{};
    if ( !common::Vector_Init( &order, pAllocator, cBrushes ) ||
         !common::Vector_Init( &pairs, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cBrushes; ++i ) {
        ( void )common::Vector_PushBack( &order, static_cast<u32>( i ) );
    }
    common::Sort_Unstable( common::span_t<u32>{ order.pData, cBrushes }, sweep_less_t{ pSnapshot } );
    for ( usize i = 0u; i < cBrushes; ++i ) {
        const math::aabbd_t &a = pSnapshot->pBrushes[order.pData[i]].pValue->bounds;
        for ( usize j = i + 1u; j < cBrushes; ++j ) {
            const math::aabbd_t &b = pSnapshot->pBrushes[order.pData[j]].pValue->bounds;
            if ( b.minimum.x > a.maximum.x ) {
                break;
            }
            if ( BoundsTouch( a, b ) ) {
                const u32 lo = order.pData[i] < order.pData[j] ? order.pData[i] : order.pData[j];
                const u32 hi = order.pData[i] < order.pData[j] ? order.pData[j] : order.pData[i];
                if ( !common::Vector_PushBack( &pairs, pair_t{ lo, hi } ) ) {
                    return geometry_status_t::ALLOCATION_FAILED;
                }
            }
        }
    }
    const usize cPairs = common::Vector_Count( &pairs );
    common::Sort_Unstable( common::span_t<pair_t>{ pairs.pData, cPairs }, pair_less_t{} );

    // ---- Narrow phase --------------------------------------------------------
    const f64 tolerance = policy.numerical.fCoplanarDistanceTolerance;
    for ( usize p = 0u; p < cPairs; ++p ) {
        const geometry_brush_value_t *pA = pSnapshot->pBrushes[pairs.pData[p].iA].pValue;
        const geometry_brush_value_t *pB = pSnapshot->pBrushes[pairs.pData[p].iB].pValue;

        if ( options.bCheckOverlaps ) {
            f64 overlap = 0.0;
            const geometry_status_t status = IntersectionVolume( pA, pB, policy, &overlap );
            if ( status != geometry_status_t::OK ) {
                return status;
            }
            if ( overlap > options.fOverlapVolume ) {
                f64 volumeA = 0.0;
                f64 volumeB = 0.0;
                vec3d_t centroid{};
                ( void )BrushQuery_TryVolumeCentroid( &pA->boundary, &volumeA, &centroid );
                ( void )BrushQuery_TryVolumeCentroid( &pB->boundary, &volumeB, &centroid );
                const bool_t bDuplicate = math::Scalar_Abs( volumeA - overlap ) <= options.fOverlapVolume &&
                                          math::Scalar_Abs( volumeB - overlap ) <= options.fOverlapVolume;
                emitter.Emit( bDuplicate ? geometry_validation_code_t::DUPLICATE_BRUSH
                                         : geometry_validation_code_t::BRUSH_OVERLAP,
                              bDuplicate ? geometry_diagnostic_severity_t::ERROR
                                         : geometry_diagnostic_severity_t::WARNING,
                              BrushTarget( pA ), BrushTarget( pB ) );
            }
        }

        if ( !options.bCheckCoplanarFaces ) {
            continue;
        }
        const usize cFacesA = common::Vector_Count( &pA->boundary.faces );
        const usize cFacesB = common::Vector_Count( &pB->boundary.faces );
        for ( usize fa = 0u; fa < cFacesA; ++fa ) {
            const u32 sideA = pA->boundary.faces.pData[fa].iSide;
            const math::planed_t planeA = pA->brush.sides.pData[sideA].plane;
            for ( usize fb = 0u; fb < cFacesB; ++fb ) {
                const u32 sideB = pB->boundary.faces.pData[fb].iSide;
                const math::planed_t planeB = pB->brush.sides.pData[sideB].plane;
                const f64 dot = math::Vec3d_Dot( planeA.normal, planeB.normal );
                const bool_t bSame = dot > 1.0 - 1.0e-9 &&
                                     math::Scalar_Abs( planeA.d - planeB.d ) <= tolerance;
                const bool_t bOpposed = dot < -( 1.0 - 1.0e-9 ) &&
                                        math::Scalar_Abs( planeA.d + planeB.d ) <= tolerance;
                if ( !bSame && !( bOpposed && options.bReportHiddenFaces ) ) {
                    continue;
                }
                f64 area = 0.0;
                const geometry_status_t status =
                    GeometryValidation_TryCoplanarOverlapArea( pA, fa, pB, fb, &area );
                if ( status != geometry_status_t::OK ) {
                    return status;
                }
                if ( area <= options.fOverlapArea ) {
                    continue;
                }
                emitter.Emit( bSame ? geometry_validation_code_t::COPLANAR_FACE_FIGHT
                                    : geometry_validation_code_t::HIDDEN_FACE,
                              bSame ? geometry_diagnostic_severity_t::WARNING
                                    : geometry_diagnostic_severity_t::NOTE,
                              SideTarget( pA, sideA ), SideTarget( pB, sideB ) );
            }
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
