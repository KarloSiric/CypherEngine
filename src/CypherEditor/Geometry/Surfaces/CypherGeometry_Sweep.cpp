//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Sweep.cpp
//  Purpose: Implements sweep and lathe via an indexed soup and Sanitation.
//  Details: Wall orientation for a sweep: the quad (ring i, k) -> (i, k+1)
//           -> (i+1, k+1) -> (i+1, k) has normal ~ profileDir x T. With
//           (T, N, B) right-handed, that is the outward side of a CCW profile
//           in (N, B) coordinates, so CW profiles simply reverse the order.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Sweep.h"
#include "CypherGeometry_PolygonSoup.h"
#include "CypherGeometry_Sanitation.h"
#include "CypherGeometry_Planar_Triangulate.h"
#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_MeshGeometricValidation.h"

#include <cmath>
#include <utility>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

constexpr f64 kTwoPi = 6.283185307179586;

sweep_report_t Fail( sweep_report_t r, geometry_status_t s ) noexcept { r.status = s; return r; }

struct soup_guard_t {
    polygon_soup_t *p;
    ~soup_guard_t() { PolygonSoup_Shutdown( p ); }
};

// Signed volume of a closed soup by the divergence theorem (fan per face).
f64 SoupVolume( const polygon_soup_t *s ) noexcept
{
    f64 vol = 0.0;
    for ( usize f = 0u; f < s->faces.nCount; ++f ) {
        const span_t<const u32> c = PolygonSoup_FaceCorners( s, f );
        const math::vec3d_t o = s->positions.pData[c.pData[0]];
        for ( usize k = 1u; k + 1u < c.nCount; ++k ) {
            vol += math::Vec3d_Dot( o, math::Vec3d_Cross( s->positions.pData[c.pData[k]],
                                                          s->positions.pData[c.pData[k + 1u]] ) );
        }
    }
    return vol / 6.0;
}

void ReverseAllFaces( polygon_soup_t *s ) noexcept
{
    for ( usize f = 0u; f < s->faces.nCount; ++f ) {
        const polygon_soup_face_t &fc = s->faces.pData[f];
        u32 *c = s->corners.pData + fc.iFirstCorner;
        for ( u32 a = 0u, b = fc.cCorners - 1u; a < b; ++a, --b ) {
            const u32 t = c[a]; c[a] = c[b]; c[b] = t;
        }
    }
}

// Adds a face after dropping consecutive duplicate indices (pole collapse);
// faces left with fewer than three corners are skipped (returns OK).
geometry_status_t AddFaceDedup( polygon_soup_t *s, const u32 *idx, u32 n, u32 group, u32 *pAdded ) noexcept
{
    u32 tmp[8];
    u32 m = 0u;
    for ( u32 i = 0u; i < n; ++i ) {
        if ( m > 0u && tmp[m - 1u] == idx[i] ) { continue; }
        tmp[m++] = idx[i];
    }
    while ( m > 1u && tmp[m - 1u] == tmp[0] ) { --m; }
    if ( m < 3u ) { return geometry_status_t::OK; }
    const geometry_status_t st = PolygonSoup_TryAddFace( s, span_t<const u32>{ tmp, m }, {}, group, nullptr );
    if ( st == geometry_status_t::OK && pAdded ) { ++*pAdded; }
    return st;
}

// The pool types deliberately disable implicit moves. Transfer their storage
// only after every fallible preparation/validation step has succeeded.
template <typename record_t, typename tag_t>
void SwapHollowPool( generation_pool_t<record_t, tag_t> &a,
                     generation_pool_t<record_t, tag_t> &b ) noexcept
{
    using std::swap;
    swap( a.pSlots, b.pSlots ); swap( a.cRecords, b.cRecords );
    swap( a.cSlots, b.cSlots ); swap( a.cRetiredSlots, b.cRetiredSlots );
    swap( a.cSlotLimit, b.cSlotLimit ); swap( a.iFreeHead, b.iFreeHead );
    swap( a.pAllocator, b.pAllocator );
}

bool HollowPositionOk( math::vec3d_t p, f64 limit ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= limit &&
           std::fabs( p.y ) <= limit && std::fabs( p.z ) <= limit;
}

f64 HollowDistance( math::vec3d_t a, math::vec3d_t b ) noexcept
{
    return std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( a, b ) ) );
}

} // namespace

sweep_report_t Sweep_TryHollowCircularPath(
    const curve_network_t *pNet, u32 iPath,
    const hollow_sweep_options_t &options, const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut, mesh_attribute_store_t *pAttributesOut ) noexcept
{
    sweep_report_t report{};
    if ( pMeshOut == nullptr || pAttributesOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return report; }
    if ( pMeshOut->pAllocator != nullptr || pMeshOut->vertices.pAllocator != nullptr ||
         pMeshOut->halfEdges.pAllocator != nullptr || pMeshOut->edges.pAllocator != nullptr ||
         pMeshOut->loops.pAllocator != nullptr || pMeshOut->faces.pAllocator != nullptr ||
         pMeshOut->shells.pAllocator != nullptr || pAttributesOut->corners.pAllocator != nullptr ||
         pAttributesOut->faces.pAllocator != nullptr || pAttributesOut->edges.pAllocator != nullptr ) {
        return Fail( report, geometry_status_t::ALREADY_INITIALIZED );
    }
    const geometry_policy_t policy{};
    const auto &numerical = policy.numerical;
    const f64 outer = options.fOuterRadius, inner = outer - options.fWallThickness;
    if ( !std::isfinite( outer ) || !std::isfinite( options.fWallThickness ) ||
         !std::isfinite( options.fWorldUnitsPerUv ) || !( options.fWorldUnitsPerUv > 0.0 ) ||
         !std::isfinite( options.sampling.fMaxSpacing ) || !( options.sampling.fMaxSpacing > 0.0 ) ||
         !math::Vec3d_IsFinite( options.sampling.initialUp ) ||
         !( inner > numerical.fMinimumEdgeLength ) || !( inner < outer ) ||
         !( options.fWallThickness > numerical.fMinimumEdgeLength ) ||
         options.cSides < 3u || options.cSides > 128u ) { return report; }
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return Fail( report, geometry_status_t::NOT_INITIALIZED ); }
    if ( iPath >= pNet->paths.nCount || pNet->paths.pData[iPath].bClosed ) { return report; }
    geometry_status_t status = CurveNetwork_Validate( pNet );
    if ( status != geometry_status_t::OK ) { return Fail( report, status ); }

    // A cubic's control polygon bounds its arc length. Bound the sampler's
    // divisions before it converts ceil(length/spacing) to an integer. This
    // deliberately conservative guard also bounds scratch work for tiny spacing.
    const curve_path_t &path = pNet->paths.pData[iPath];
    const usize maxRings = kHollowSweepFacesMax / ( 2u * options.cSides );
    usize ringBound = 1u;
    for ( u32 s = 0u; s < path.cSteps; ++s ) {
        const auto &segment = pNet->segments.pData[pNet->steps.pData[path.iFirstStep + s].iSegment];
        const auto a = pNet->nodes.pData[segment.iNode0].position;
        const auto b = pNet->nodes.pData[segment.iNode1].position;
        if ( !HollowPositionOk( a, numerical.fCoordinateMagnitudeLimit ) ||
             !HollowPositionOk( b, numerical.fCoordinateMagnitudeLimit ) ||
             !HollowPositionOk( segment.handle0, numerical.fCoordinateMagnitudeLimit ) ||
             !HollowPositionOk( segment.handle1, numerical.fCoordinateMagnitudeLimit ) ) {
            return Fail( report, geometry_status_t::NUMERIC_FAILURE );
        }
        const f64 bound = segment.basis == curve_basis_t::LINEAR ? HollowDistance( a, b ) :
            HollowDistance( a, segment.handle0 ) + HollowDistance( segment.handle0, segment.handle1 ) +
            HollowDistance( segment.handle1, b );
        const f64 divisions = std::ceil( bound / options.sampling.fMaxSpacing );
        if ( !std::isfinite( divisions ) || divisions > static_cast<f64>( maxRings - ringBound ) ) {
            return Fail( report, geometry_status_t::LIMIT_EXCEEDED );
        }
        ringBound += divisions < 1.0 ? 1u : static_cast<usize>( divisions );
        if ( ringBound > maxRings ) { return Fail( report, geometry_status_t::LIMIT_EXCEEDED ); }
    }
    vector_t<curve_sample_t> samples{};
    if ( !Vector_Init( &samples, pAllocator ) ) { return Fail( report, geometry_status_t::ALLOCATION_FAILED ); }
    status = CurveSampling_TrySamplePath( pNet, iPath, options.sampling, &samples );
    if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
    const usize n = samples.nCount, sides = options.cSides;
    if ( n < 2u ) { return Fail( report, geometry_status_t::DEGENERATE ); }
    if ( n > maxRings ) { return Fail( report, geometry_status_t::LIMIT_EXCEEDED ); }
    const usize faceCount = 2u * n * sides;
    for ( usize i = 1u; i < n; ++i ) {
        const auto &a = samples.pData[i - 1u], &b = samples.pData[i];
        const f64 chord = HollowDistance( a.position, b.position );
        const f64 cosine = math::Vec3d_Dot( a.tangent, b.tangent );
        const f64 turn = std::sqrt( std::fmax( 0.0, 2.0 * ( 1.0 - cosine ) ) );
        if ( !( chord > numerical.fMinimumEdgeLength ) || !( cosine > 0.0 ) ||
             !( chord - outer * turn > numerical.fMinimumEdgeLength ) ) {
            return Fail( report, geometry_status_t::DEGENERATE );
        }
    }

    polygon_soup_t soup{};
    soup_guard_t soupGuard{ &soup };
    vector_t<math::vec2d_t> uvs{};
    vector_t<geometry_mesh_face_handle_t> faceMap{};
    status = PolygonSoup_Init( &soup, pAllocator );
    if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
    if ( !Vector_Reserve( &soup.positions, faceCount ) || !Vector_Reserve( &soup.faces, faceCount ) ||
         !Vector_Reserve( &soup.corners, 4u * faceCount ) || !Vector_Init( &uvs, pAllocator, 4u * faceCount ) ||
         !Vector_Init( &faceMap, pAllocator, faceCount ) ) {
        return Fail( report, geometry_status_t::ALLOCATION_FAILED );
    }
    auto vertex = [&]( usize ring, usize side, bool inside ) noexcept -> u32 {
        return static_cast<u32>( 2u * sides * ring + ( inside ? sides : 0u ) + side % sides );
    };
    for ( usize i = 0u; i < n; ++i ) {
        const auto &sample = samples.pData[i];
        for ( u32 layer = 0u; layer < 2u; ++layer ) {
            const f64 radius = layer == 0u ? outer : inner;
            for ( usize k = 0u; k < sides; ++k ) {
                const f64 angle = kTwoPi * static_cast<f64>( k ) / static_cast<f64>( sides );
                const auto radial = math::Vec3d_Add( math::Vec3d_Scale( sample.normal, radius * std::cos( angle ) ),
                                                     math::Vec3d_Scale( sample.binormal, radius * std::sin( angle ) ) );
                const auto p = math::Vec3d_Add( sample.position, radial );
                if ( !HollowPositionOk( p, numerical.fCoordinateMagnitudeLimit ) ) {
                    return Fail( report, geometry_status_t::NUMERIC_FAILURE );
                }
                status = PolygonSoup_TryAddVertex( &soup, p, nullptr );
                if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
            }
        }
    }
    auto addFace = [&]( const u32 ( &indices )[4], const math::vec2d_t ( &chart )[4], u32 group,
                        math::vec3d_t expectedNormal ) noexcept -> geometry_status_t {
        // All four triangle corners must face the expected wall/rim direction.
        // This refuses collapsed or folded quads before projection triangulation.
        const f64 normalLength = std::sqrt( math::Vec3d_LengthSquared( expectedNormal ) );
        if ( !( normalLength > 0.0 ) || !std::isfinite( normalLength ) ) { return geometry_status_t::DEGENERATE; }
        expectedNormal = math::Vec3d_Scale( expectedNormal, 1.0 / normalLength );
        for ( usize k = 0u; k < 4u; ++k ) {
            if ( !math::Vec2d_IsFinite( chart[k] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
            const auto a = soup.positions.pData[indices[k]];
            const auto b = soup.positions.pData[indices[( k + 1u ) % 4u]];
            const auto c = soup.positions.pData[indices[( k + 2u ) % 4u]];
            const auto normal = math::Vec3d_Cross( math::Vec3d_Subtract( b, a ), math::Vec3d_Subtract( c, b ) );
            if ( !( math::Vec3d_Dot( normal, expectedNormal ) > numerical.fMinimumFaceArea ) ) {
                return geometry_status_t::DEGENERATE;
            }
        }
        const f64 uvArea = ( chart[1].x - chart[0].x ) * ( chart[2].y - chart[0].y ) -
                           ( chart[1].y - chart[0].y ) * ( chart[2].x - chart[0].x );
        if ( !std::isfinite( uvArea ) || uvArea == 0.0 ) { return geometry_status_t::NUMERIC_FAILURE; }
        auto result = PolygonSoup_TryAddFace( &soup, { indices, 4u }, {}, group, nullptr );
        if ( result == geometry_status_t::OK && !Vector_Append( &uvs, span_t<const math::vec2d_t>{ chart, 4u } ) ) {
            result = geometry_status_t::ALLOCATION_FAILED;
        }
        return result;
    };
    const f64 repeat = options.fWorldUnitsPerUv;
    for ( usize i = 0u; i + 1u < n; ++i ) {
        for ( usize k = 0u; k < sides; ++k ) {
            for ( u32 layer = 0u; layer < 2u; ++layer ) {
                const bool inside = layer != 0u;
                const f64 radius = inside ? inner : outer;
                const f64 u0 = kTwoPi * radius * static_cast<f64>( k ) / ( static_cast<f64>( sides ) * repeat );
                const f64 u1 = kTwoPi * radius * static_cast<f64>( k + 1u ) / ( static_cast<f64>( sides ) * repeat );
                const f64 v0 = samples.pData[i].distance / repeat, v1 = samples.pData[i + 1u].distance / repeat;
                u32 q[4] = { vertex( i, k, inside ), vertex( i, k + 1u, inside ),
                             vertex( i + 1u, k + 1u, inside ), vertex( i + 1u, k, inside ) };
                math::vec2d_t chart[4] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
                auto radial = math::Vec3d_Make( 0.0, 0.0, 0.0 );
                for ( usize c = 0u; c < 4u; ++c ) {
                    radial = math::Vec3d_Add( radial, math::Vec3d_Subtract( soup.positions.pData[q[c]],
                        samples.pData[c < 2u ? i : i + 1u].position ) );
                }
                if ( inside ) {
                    std::swap( q[1], q[3] ); std::swap( chart[1], chart[3] );
                    for ( auto &uv : chart ) { uv.x = -uv.x; }
                    radial = math::Vec3d_Scale( radial, -1.0 );
                }
                status = addFace( q, chart, layer, radial );
                if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
            }
        }
    }
    for ( u32 end = 0u; end < 2u; ++end ) {
        const usize i = end == 0u ? 0u : n - 1u;
        const auto normal = math::Vec3d_Scale( samples.pData[i].tangent, end == 0u ? -1.0 : 1.0 );
        for ( usize k = 0u; k < sides; ++k ) {
            u32 q[4] = { vertex( i, k, false ), vertex( i, k + 1u, false ),
                         vertex( i, k + 1u, true ), vertex( i, k, true ) };
            math::vec2d_t chart[4]{};
            for ( usize c = 0u; c < 4u; ++c ) {
                const auto offset = math::Vec3d_Subtract( soup.positions.pData[q[c]], samples.pData[i].position );
                chart[c] = { math::Vec3d_Dot( offset, samples.pData[i].normal ) / repeat,
                             math::Vec3d_Dot( offset, samples.pData[i].binormal ) / repeat };
            }
            if ( end == 0u ) { std::swap( q[1], q[3] ); std::swap( chart[1], chart[3] ); }
            status = addFace( q, chart, 2u + end, normal );
            if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
        }
    }
    editable_mesh_t candidate{};
    mesh_attribute_store_t attributes{};
    struct output_guard_t {
        editable_mesh_t *mesh; mesh_attribute_store_t *attributes;
        ~output_guard_t() { MeshAttributeStore_Shutdown( attributes ); EditableMesh_Shutdown( mesh ); }
    } outputGuard{ &candidate, &attributes };
    const auto sanitation = Sanitation_TryPolygonSoupToMesh( &soup, {}, pAllocator, &candidate, &faceMap );
    if ( sanitation.status != geometry_status_t::OK ) { return Fail( report, sanitation.status ); }
    if ( sanitation.cShells != 1u || sanitation.cBoundaryEdges != 0u ||
         !( EditableMesh_SignedVolume( &candidate ) > numerical.fMinimumFaceArea ) ) {
        return Fail( report, geometry_status_t::INVALID_TOPOLOGY );
    }
    const auto geometry = MeshValidation_ValidateGeometry( &candidate, policy, {} );
    if ( geometry.status != geometry_status_t::OK ) { return Fail( report, geometry.status ); }
    if ( geometry.cShortEdges != 0u || geometry.cCoincidentPairs != 0u ) {
        return Fail( report, geometry_status_t::DEGENERATE );
    }
    status = MeshAttributeStore_Init( &attributes, pAllocator );
    if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
    for ( usize f = 0u; f < faceMap.nCount; ++f ) {
        const auto hFace = faceMap.pData[f];
        const auto *face = EditableMesh_GetFace( &candidate, hFace );
        const auto *loop = face ? EditableMesh_GetLoop( &candidate, face->hOuterLoop ) : nullptr;
        if ( loop == nullptr || loop->cHalfEdges != 4u ) { return Fail( report, geometry_status_t::CORRUPT_STATE ); }
        auto hCorner = loop->hFirstHalfEdge;
        for ( usize c = 0u; c < 4u; ++c ) {
            const auto *half = EditableMesh_GetHalfEdge( &candidate, hCorner );
            const auto *point = half ? EditableMesh_GetVertex( &candidate, half->hOrigin ) : nullptr;
            const usize sourceCorner = soup.faces.pData[f].iFirstCorner + c;
            const auto expected = soup.positions.pData[soup.corners.pData[sourceCorner]];
            if ( point == nullptr || point->position.x != expected.x || point->position.y != expected.y || point->position.z != expected.z ) {
                return Fail( report, geometry_status_t::CORRUPT_STATE );
            }
            mesh_corner_attributes_t attr{};
            attr.uv0 = attr.uv1 = uvs.pData[sourceCorner];
            status = MeshAttributeStore_TrySetCorner( &attributes, hCorner, attr, candidate.limits.cHalfEdgeMax );
            if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
            hCorner = half->hNext;
        }
        mesh_face_attributes_t attr{};
        attr.smoothingGroups = soup.faces.pData[f].iGroup < 2u ? ( 1u << soup.faces.pData[f].iGroup ) : 0u;
        status = MeshAttributeStore_TrySetFace( &attributes, hFace, attr, candidate.limits.cFaceMax );
        if ( status != geometry_status_t::OK ) { return Fail( report, status ); }
    }
    SwapHollowPool( pMeshOut->vertices, candidate.vertices );
    SwapHollowPool( pMeshOut->halfEdges, candidate.halfEdges );
    SwapHollowPool( pMeshOut->edges, candidate.edges );
    SwapHollowPool( pMeshOut->loops, candidate.loops );
    SwapHollowPool( pMeshOut->faces, candidate.faces );
    SwapHollowPool( pMeshOut->shells, candidate.shells );
    std::swap( pMeshOut->pAllocator, candidate.pAllocator );
    std::swap( pMeshOut->limits, candidate.limits );
    Vector_Move( &pAttributesOut->corners, &attributes.corners );
    Vector_Move( &pAttributesOut->faces, &attributes.faces );
    Vector_Move( &pAttributesOut->edges, &attributes.edges );
    report.status = geometry_status_t::OK;
    report.cRings = static_cast<u32>( n );
    report.cWallFaces = static_cast<u32>( 2u * sides * ( n - 1u ) );
    report.cCapFaces = static_cast<u32>( 2u * sides );
    report.bClosed = true;
    return report;
}

sweep_report_t Sweep_TryAlongPath(
    const curve_network_t *pNet,
    u32 iPath,
    span_t<const math::vec2d_t> profile,
    const sweep_options_t &options,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept
{
    sweep_report_t r{};
    if ( pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) || profile.pData == nullptr ||
         profile.nCount < 2u || ( options.bProfileClosed && profile.nCount < 3u ) ||
         profile.nCount > kSweepProfilePointsMax || !std::isfinite( options.fScaleStart ) ||
         !std::isfinite( options.fScaleEnd ) ) {
        return r;
    }
    if ( !CurveNetwork_IsInitialized( pNet ) ) { return Fail( r, geometry_status_t::NOT_INITIALIZED ); }
    if ( iPath >= pNet->paths.nCount ) { return r; }
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        if ( !math::Vec2d_IsFinite( profile.pData[k] ) ) { return Fail( r, geometry_status_t::NUMERIC_FAILURE ); }
    }
    const bool pathClosed = pNet->paths.pData[iPath].bClosed;

    vector_t<curve_sample_t> samples{};
    if ( !Vector_Init( &samples, pAllocator ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
    struct sg_t { vector_t<curve_sample_t> *v; ~sg_t() { Vector_Shutdown( v ); } } sg{ &samples };
    geometry_status_t s = CurveSampling_TrySamplePath( pNet, iPath, options.sampling, &samples );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    const usize n = samples.nCount;
    const usize m = profile.nCount;
    if ( n < 2u ) { return Fail( r, geometry_status_t::DEGENERATE ); }
    const f64 total = samples.pData[n - 1u].distance > 0.0 ? samples.pData[n - 1u].distance : 1.0;

    polygon_soup_t soup{};
    soup_guard_t guard{ &soup };
    s = PolygonSoup_Init( &soup, pAllocator );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    for ( usize i = 0u; i < n && s == geometry_status_t::OK; ++i ) {
        const curve_sample_t &cs = samples.pData[i];
        const f64 scale = options.fScaleStart + ( options.fScaleEnd - options.fScaleStart ) * ( cs.distance / total );
        for ( usize k = 0u; k < m && s == geometry_status_t::OK; ++k ) {
            const math::vec2d_t q = profile.pData[k];
            const math::vec3d_t p = math::Vec3d_Add(
                cs.position, math::Vec3d_Add( math::Vec3d_Scale( cs.normal, q.x * scale ),
                                              math::Vec3d_Scale( cs.binormal, q.y * scale ) ) );
            s = PolygonSoup_TryAddVertex( &soup, p, nullptr );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    r.cRings = static_cast<u32>( n );

    const bool ccw = !options.bProfileClosed ||
                     Planar_RingSignedArea( span_t<const math::vec2d_t>{ profile.pData, m } ) >= 0.0;
    const usize ringSpans = pathClosed ? n : n - 1u;
    const usize profileSpans = options.bProfileClosed ? m : m - 1u;
    auto V = [&]( usize i, usize k ) noexcept { return static_cast<u32>( ( i % n ) * m + ( k % m ) ); };
    for ( usize i = 0u; i < ringSpans && s == geometry_status_t::OK; ++i ) {
        for ( usize k = 0u; k < profileSpans && s == geometry_status_t::OK; ++k ) {
            u32 q[4] = { V( i, k ), V( i, k + 1u ), V( i + 1u, k + 1u ), V( i + 1u, k ) };
            if ( !ccw ) { const u32 t = q[1]; q[1] = q[3]; q[3] = t; }
            s = AddFaceDedup( &soup, q, 4u, 0u, &r.cWallFaces );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    const bool caps = !pathClosed && options.bProfileClosed && options.bCapEnds;
    if ( caps ) {
        vector_t<planar_ring_triangle_t> tris{};
        if ( !Vector_Init( &tris, pAllocator ) ) { return Fail( r, geometry_status_t::ALLOCATION_FAILED ); }
        s = Planar_TryTriangulateRing( span_t<const math::vec2d_t>{ profile.pData, m }, pAllocator, &tris );
        // Ring triangles share the profile's winding. A CCW profile's
        // triangles face +T: correct for the end cap, reversed for the start.
        for ( usize t = 0u; t < tris.nCount && s == geometry_status_t::OK; ++t ) {
            const planar_ring_triangle_t &tr = tris.pData[t];
            u32 endTri[3] = { V( n - 1u, tr.a ), V( n - 1u, tr.b ), V( n - 1u, tr.c ) };
            u32 startTri[3] = { V( 0u, tr.a ), V( 0u, tr.c ), V( 0u, tr.b ) };
            if ( !ccw ) {
                const u32 t0 = endTri[1]; endTri[1] = endTri[2]; endTri[2] = t0;
                const u32 t1 = startTri[1]; startTri[1] = startTri[2]; startTri[2] = t1;
            }
            s = AddFaceDedup( &soup, startTri, 3u, 1u, &r.cCapFaces );
            if ( s == geometry_status_t::OK ) { s = AddFaceDedup( &soup, endTri, 3u, 2u, &r.cCapFaces ); }
        }
        Vector_Shutdown( &tris );
        if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    }

    r.bClosed = options.bProfileClosed && ( pathClosed || caps );
    sanitation_policy_t sp{};
    sp.bRequireClosed = r.bClosed;
    const sanitation_report_t sr = Sanitation_TryPolygonSoupToMesh( &soup, sp, pAllocator, pMeshOut, nullptr );
    r.status = sr.status;
    return r;
}

sweep_report_t Sweep_TryLathe(
    span_t<const math::vec2d_t> profile,
    math::vec3d_t axisOrigin,
    math::vec3d_t axisDir,
    const lathe_options_t &options,
    const allocator_t *pAllocator,
    editable_mesh_t *pMeshOut ) noexcept
{
    sweep_report_t r{};
    math::vec3d_t A{};
    if ( pMeshOut == nullptr || !Allocator_IsValid( pAllocator ) || profile.pData == nullptr ||
         profile.nCount < 2u || profile.nCount > kSweepProfilePointsMax || options.segments < 3u ||
         !( options.angle > 0.0 ) || !math::Vec3d_IsFinite( axisOrigin ) ||
         !math::Vec3d_TryNormalize( axisDir, 1.0e-300, &A, nullptr ) ) {
        return r;
    }
    for ( usize k = 0u; k < profile.nCount; ++k ) {
        if ( !math::Vec2d_IsFinite( profile.pData[k] ) ) { return Fail( r, geometry_status_t::NUMERIC_FAILURE ); }
        if ( profile.pData[k].x < 0.0 ) { return r; }
    }
    const bool full = options.angle >= kTwoPi - 1.0e-9;
    const u32 segs = options.segments;
    const u32 rings = full ? segs : segs + 1u;
    const usize m = profile.nCount;

    const f64 ax = std::fabs( A.x ), ay = std::fabs( A.y ), az = std::fabs( A.z );
    const math::vec3d_t h = ( ax <= ay && ax <= az ) ? math::Vec3d_Make( 1, 0, 0 )
                          : ( ay <= az ) ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
    math::vec3d_t e1{};
    (void)math::Vec3d_TryNormalize( math::Vec3d_Cross( A, h ), 1.0e-300, &e1, nullptr );
    const math::vec3d_t e2 = math::Vec3d_Cross( A, e1 );

    polygon_soup_t soup{};
    soup_guard_t guard{ &soup };
    geometry_status_t s = PolygonSoup_Init( &soup, pAllocator );
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    // Vertex index table: poles (x == 0) get one shared vertex per profile point.
    vector_t<u32> index{};
    if ( !Vector_Init( &index, pAllocator ) || !Vector_Resize( &index, static_cast<usize>( rings ) * m ) ) {
        Vector_Shutdown( &index );
        return Fail( r, geometry_status_t::ALLOCATION_FAILED );
    }
    struct ig_t { vector_t<u32> *v; ~ig_t() { Vector_Shutdown( v ); } } ig{ &index };
    for ( usize k = 0u; k < m && s == geometry_status_t::OK; ++k ) {
        const math::vec2d_t q = profile.pData[k];
        if ( q.x == 0.0 ) {
            u32 vi = 0u;
            s = PolygonSoup_TryAddVertex( &soup, math::Vec3d_Add( axisOrigin, math::Vec3d_Scale( A, q.y ) ), &vi );
            for ( u32 j = 0u; j < rings; ++j ) { index.pData[j * m + k] = vi; }
            continue;
        }
        for ( u32 j = 0u; j < rings && s == geometry_status_t::OK; ++j ) {
            const f64 phi = options.angle * static_cast<f64>( j ) / static_cast<f64>( segs );
            const math::vec3d_t radial = math::Vec3d_Add( math::Vec3d_Scale( e1, std::cos( phi ) ),
                                                          math::Vec3d_Scale( e2, std::sin( phi ) ) );
            const math::vec3d_t p = math::Vec3d_Add( axisOrigin, math::Vec3d_Add( math::Vec3d_Scale( A, q.y ),
                                                                                  math::Vec3d_Scale( radial, q.x ) ) );
            u32 vi = 0u;
            s = PolygonSoup_TryAddVertex( &soup, p, &vi );
            index.pData[j * m + k] = vi;
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }
    r.cRings = rings;
    auto I = [&]( u32 j, usize k ) noexcept { return index.pData[( j % rings ) * m + k]; };
    for ( u32 j = 0u; j < segs && s == geometry_status_t::OK; ++j ) {
        for ( usize k = 0u; k + 1u < m && s == geometry_status_t::OK; ++k ) {
            // Profile up + revolution CCW about A: this order faces away from
            // the axis for an upward profile (see header).
            const u32 q[4] = { I( j, k ), I( j + 1u, k ), I( j + 1u, k + 1u ), I( j, k + 1u ) };
            s = AddFaceDedup( &soup, q, 4u, 0u, &r.cWallFaces );
        }
    }
    if ( s != geometry_status_t::OK ) { return Fail( r, s ); }

    // Closed iff full turn and both profile ends sit on the axis.
    r.bClosed = full && profile.pData[0].x == 0.0 && profile.pData[m - 1u].x == 0.0;
    if ( r.bClosed && SoupVolume( &soup ) < 0.0 ) { ReverseAllFaces( &soup ); }
    sanitation_policy_t sp{};
    sp.bRequireClosed = r.bClosed;
    const sanitation_report_t sr = Sanitation_TryPolygonSoupToMesh( &soup, sp, pAllocator, pMeshOut, nullptr );
    r.status = sr.status;
    return r;
}

} // namespace cypher::editor::geometry
