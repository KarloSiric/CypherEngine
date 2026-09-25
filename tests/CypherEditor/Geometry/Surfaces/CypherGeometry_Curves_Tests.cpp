//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Curves_Tests.cpp
//  Purpose: Contract tests for CurveNetwork, curve sampling with
//           rotation-minimizing frames, and sweep / lathe generation.
//  Details: Geometric oracles: a quarter circle of radius r approximated by
//           the standard cubic (handle factor 0.5523) has length pi*r/2 to
//           ~1e-4; a swept circle of radius a along a straight line of length
//           L has volume ~pi*a^2*L (polygonal profile area times L exactly);
//           a lathed disc profile gives a cylinder of volume pi r^2 h
//           (polygonal: n/2 r^2 sin(2pi/n) h exactly).
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CurveNetwork.h"
#include "CypherGeometry_CurveSampling.h"
#include "CypherGeometry_Sweep.h"
#include "CypherGeometry_MeshValidation.h"
#include "CypherGeometry_MeshGeometricValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry {

using Catch::Approx;
using math::vec2d_t;
using math::vec3d_t;
using math::Vec3d_Make;

namespace {

constexpr double kPi = 3.14159265358979323846;

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

struct Net {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    curve_network_t n{};
    geometry_source_id_allocator_t ids{ geometry_source_id_t{ 100u } };
    Net() { REQUIRE( CurveNetwork_Init( &n, &allocator, Id( 1 ) ) == geometry_status_t::OK ); }
    ~Net() { CurveNetwork_Shutdown( &n ); }
    common::u32 Node( vec3d_t p ) {
        common::u32 i = 0;
        REQUIRE( CurveNetwork_TryAddNode( &n, p, GeometrySourceIdAllocator_Allocate( &ids ).id, &i ) ==
                 geometry_status_t::OK );
        return i;
    }
    common::u32 Line( common::u32 a, common::u32 b ) {
        common::u32 i = 0;
        REQUIRE( CurveNetwork_TryAddSegment( &n, a, b, curve_basis_t::LINEAR, {}, {},
                                             GeometrySourceIdAllocator_Allocate( &ids ).id, &i ) ==
                 geometry_status_t::OK );
        return i;
    }
    common::u32 Bez( common::u32 a, common::u32 b, vec3d_t h0, vec3d_t h1 ) {
        common::u32 i = 0;
        REQUIRE( CurveNetwork_TryAddSegment( &n, a, b, curve_basis_t::CUBIC_BEZIER, h0, h1,
                                             GeometrySourceIdAllocator_Allocate( &ids ).id, &i ) ==
                 geometry_status_t::OK );
        return i;
    }
    common::u32 Path( std::vector<curve_path_step_t> s, bool closed ) {
        common::u32 i = 0;
        REQUIRE( CurveNetwork_TryAddPath( &n, common::span_t<const curve_path_step_t>{ s.data(), s.size() }, closed,
                                          GeometrySourceIdAllocator_Allocate( &ids ).id, &i ) ==
                 geometry_status_t::OK );
        return i;
    }
    // Quarter circle of radius r in XY from (r,0) to (0,r).
    common::u32 QuarterArc( double r, common::u32 a, common::u32 b ) {
        const double k = 0.5522847498 * r;
        return Bez( a, b, Vec3d_Make( r, k, 0 ), Vec3d_Make( k, r, 0 ) );
    }
};

double Dist( vec3d_t a, vec3d_t b ) { return std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( a, b ) ) ); }

} // namespace

// ===========================================================================
// CurveNetwork
// ===========================================================================

TEST_CASE( "CurveNetwork: evaluation and length", "[Curves][Network]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 3, 4, 0 } );
    const auto l = net.Line( a, b );
    CHECK( CurveNetwork_SegmentLength( &net.n, l, 1e-9 ) == Approx( 5.0 ) );
    CHECK( CurveNetwork_EvaluateSegment( &net.n, l, 0.5 ).x == Approx( 1.5 ) );

    const auto c = net.Node( { 1, 0, 0 } ), d = net.Node( { 0, 1, 0 } );
    const auto arc = net.QuarterArc( 1.0, c, d );
    CHECK( CurveNetwork_SegmentLength( &net.n, arc, 1e-10 ) == Approx( kPi / 2.0 ).epsilon( 2e-4 ) );
    const vec3d_t mid = CurveNetwork_EvaluateSegment( &net.n, arc, 0.5 );
    CHECK( std::sqrt( mid.x * mid.x + mid.y * mid.y ) == Approx( 1.0 ).epsilon( 3e-4 ) );
}

TEST_CASE( "CurveNetwork: paths validate connectivity and direction", "[Curves][Network]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 1, 0, 0 } ), c = net.Node( { 1, 1, 0 } );
    const auto ab = net.Line( a, b ), cb = net.Line( c, b ), ca = net.Line( c, a );
    // a->b, b->c (cb reversed), c->a : closed triangle.
    net.Path( { { ab, false }, { cb, true }, { ca, false } }, true );
    CHECK( CurveNetwork_PathLength( &net.n, 0, 1e-9 ) == Approx( 2.0 + std::sqrt( 2.0 ) ) );
    CHECK( CurveNetwork_Validate( &net.n ) == geometry_status_t::OK );

    const curve_path_step_t broken[] = { { ab, false }, { ca, false } };
    CHECK( CurveNetwork_TryAddPath( &net.n, common::span_t<const curve_path_step_t>{ broken, 2 }, false, Id( 999 ),
                                    nullptr ) == geometry_status_t::INVALID_TOPOLOGY );
    const curve_path_step_t notClosed[] = { { ab, false } };
    CHECK( CurveNetwork_TryAddPath( &net.n, common::span_t<const curve_path_step_t>{ notClosed, 1 }, true, Id( 998 ),
                                    nullptr ) == geometry_status_t::INVALID_TOPOLOGY );
    CHECK( net.n.paths.nCount == 1u );
}

TEST_CASE( "CurveNetwork: splitting preserves shape and updates paths", "[Curves][Network]" )
{
    Net net;
    const auto a = net.Node( { 1, 0, 0 } ), b = net.Node( { 0, 1, 0 } ), c = net.Node( { -1, 0, 0 } );
    const auto arc = net.QuarterArc( 1.0, a, b );
    const auto ln = net.Line( c, b );
    net.Path( { { arc, false }, { ln, true } }, false ); // a -> b -> c
    std::vector<vec3d_t> before;
    for ( common::u32 i = 0u; i <= 10u; ++i ) {
        before.push_back( CurveNetwork_EvaluateSegment( &net.n, arc, i / 10.0 ) );
    }
    const double lenBefore = CurveNetwork_PathLength( &net.n, 0, 1e-10 );

    common::u32 newNode = 0, newSeg = 0;
    REQUIRE( CurveNetwork_TrySplitSegment( &net.n, arc, 0.3, Id( 700 ), Id( 701 ), &newNode, &newSeg ) ==
             geometry_status_t::OK );
    CHECK( net.n.paths.pData[0].cSteps == 3u );
    CHECK( CurveNetwork_Validate( &net.n ) == geometry_status_t::OK );
    CHECK( CurveNetwork_PathLength( &net.n, 0, 1e-10 ) == Approx( lenBefore ).epsilon( 1e-9 ) );
    // Old samples lie on the new halves exactly (reparameterised).
    for ( common::u32 i = 0u; i <= 10u; ++i ) {
        const double t = i / 10.0;
        const vec3d_t p = t <= 0.3 ? CurveNetwork_EvaluateSegment( &net.n, arc, t / 0.3 )
                                   : CurveNetwork_EvaluateSegment( &net.n, newSeg, ( t - 0.3 ) / 0.7 );
        CHECK( Dist( p, before[i] ) == Approx( 0.0 ).margin( 1e-12 ) );
    }
    // Splitting a reversed step inserts the second half *before* it.
    REQUIRE( CurveNetwork_TrySplitSegment( &net.n, ln, 0.5, Id( 702 ), Id( 703 ), nullptr, &newSeg ) ==
             geometry_status_t::OK );
    CHECK( net.n.paths.pData[0].cSteps == 4u );
    CHECK( CurveNetwork_Validate( &net.n ) == geometry_status_t::OK );
    CHECK( net.n.segments.pData[arc].sourceId.value != 701u ); // original keeps its ID
}

TEST_CASE( "CurveNetwork: Catmull-Rom path interpolates its points", "[Curves][Network]" )
{
    Net net;
    const vec3d_t pts[] = { { 0, 0, 0 }, { 2, 1, 0 }, { 4, 0, 1 }, { 6, 2, 0 } };
    common::u32 path = 0;
    const common::u64 idsBefore = net.ids.next.value;
    REQUIRE( CurveNetwork_TryAddCatmullRomPath( &net.n, common::span_t<const vec3d_t>{ pts, 4 }, false, &net.ids, &path ) ==
             geometry_status_t::OK );
    CHECK( net.ids.next.value == idsBefore + 4 + 3 + 1 );
    const curve_path_t &p = net.n.paths.pData[path];
    REQUIRE( p.cSteps == 3u );
    for ( common::u32 i = 0; i < 3; ++i ) {
        const curve_path_step_t st = net.n.steps.pData[p.iFirstStep + i];
        CHECK( Dist( CurveNetwork_EvaluateStep( &net.n, st, 0.0 ), pts[i] ) == Approx( 0.0 ).margin( 1e-12 ) );
        CHECK( Dist( CurveNetwork_EvaluateStep( &net.n, st, 1.0 ), pts[i + 1] ) == Approx( 0.0 ).margin( 1e-12 ) );
    }
    // C1 continuity at interior points.
    const vec3d_t d0 = CurveNetwork_EvaluateStepDerivative( &net.n, net.n.steps.pData[p.iFirstStep], 1.0 );
    const vec3d_t d1 = CurveNetwork_EvaluateStepDerivative( &net.n, net.n.steps.pData[p.iFirstStep + 1], 0.0 );
    CHECK( Dist( d0, d1 ) == Approx( 0.0 ).margin( 1e-12 ) );

    const vec3d_t bad[] = { { 0, 0, 0 }, { NAN, 0, 0 } };
    const auto nodes = net.n.nodes.nCount;
    CHECK( CurveNetwork_TryAddCatmullRomPath( &net.n, common::span_t<const vec3d_t>{ bad, 2 }, false, &net.ids, nullptr ) ==
           geometry_status_t::NUMERIC_FAILURE );
    CHECK( net.n.nodes.nCount == nodes );
}

TEST_CASE( "CurveNetwork: argument guards", "[Curves][Network]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } );
    CHECK( CurveNetwork_TryAddSegment( &net.n, a, a, curve_basis_t::LINEAR, {}, {}, Id( 5 ), nullptr ) ==
           geometry_status_t::INVALID_ARGUMENT );
    CHECK( CurveNetwork_TryAddSegment( &net.n, a, 99, curve_basis_t::LINEAR, {}, {}, Id( 5 ), nullptr ) ==
           geometry_status_t::INVALID_HANDLE );
    CHECK( CurveNetwork_TryAddNode( &net.n, { INFINITY, 0, 0 }, Id( 6 ), nullptr ) ==
           geometry_status_t::NUMERIC_FAILURE );
    curve_network_t un{};
    CHECK( CurveNetwork_Validate( &un ) == geometry_status_t::NOT_INITIALIZED );
}

// ===========================================================================
// Sampling and frames
// ===========================================================================

TEST_CASE( "CurveSampling: spacing, corners, and orthonormal frames", "[Curves][Sampling]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 10, 0, 0 } ), c = net.Node( { 10, 5, 0 } );
    const auto ab = net.Line( a, b ), bc = net.Line( b, c );
    const auto p = net.Path( { { ab, false }, { bc, false } }, false );
    common::vector_t<curve_sample_t> s{};
    REQUIRE( common::Vector_Init( &s, &net.allocator ) );
    curve_sampling_options_t o{};
    o.fMaxSpacing = 2.0;
    REQUIRE( CurveSampling_TrySamplePath( &net.n, p, o, &s ) == geometry_status_t::OK );
    CHECK( s.nCount == 5u + 3u + 1u ); // 10/2 on the first leg, ceil(5/2) on the second, end point
    bool cornerHit = false;
    for ( common::usize i = 0; i < s.nCount; ++i ) {
        const curve_sample_t &x = s.pData[i];
        cornerHit |= Dist( x.position, Vec3d_Make( 10, 0, 0 ) ) == 0.0;
        CHECK( math::Vec3d_LengthSquared( x.normal ) == Approx( 1.0 ) );
        CHECK( math::Vec3d_Dot( x.normal, x.tangent ) == Approx( 0.0 ).margin( 1e-12 ) );
        CHECK( math::Vec3d_Dot( math::Vec3d_Cross( x.tangent, x.normal ), x.binormal ) == Approx( 1.0 ) );
    }
    CHECK( cornerHit );
    CHECK( s.pData[s.nCount - 1].distance == Approx( 15.0 ) );
    common::Vector_Shutdown( &s );
}

TEST_CASE( "CurveSampling: planar curve keeps its normal in plane (no twist)", "[Curves][Sampling]" )
{
    Net net;
    const auto a = net.Node( { 1, 0, 0 } ), b = net.Node( { 0, 1, 0 } );
    const auto arc = net.QuarterArc( 1.0, a, b );
    const auto p = net.Path( { { arc, false } }, false );
    common::vector_t<curve_sample_t> s{};
    REQUIRE( common::Vector_Init( &s, &net.allocator ) );
    curve_sampling_options_t o{};
    o.fMaxSpacing = 0.05;
    o.initialUp = Vec3d_Make( 0, 0, 1 );
    REQUIRE( CurveSampling_TrySamplePath( &net.n, p, o, &s ) == geometry_status_t::OK );
    for ( common::usize i = 0; i < s.nCount; ++i ) {
        // An RMF of a plane curve seeded with the plane normal keeps it.
        CHECK( s.pData[i].normal.z == Approx( 1.0 ).epsilon( 1e-9 ) );
    }
    common::Vector_Shutdown( &s );
}

TEST_CASE( "CurveSampling: closed helix-like loop has no twist seam", "[Curves][Sampling]" )
{
    Net net;
    // Closed, non-planar Catmull-Rom loop (a "saddle"): RMF holonomy != 0.
    std::vector<vec3d_t> pts;
    for ( int i = 0; i < 8; ++i ) {
        const double a = 2 * kPi * i / 8;
        pts.push_back( Vec3d_Make( 4 * std::cos( a ), 4 * std::sin( a ), 1.5 * std::sin( 2 * a ) ) );
    }
    common::u32 path = 0;
    REQUIRE( CurveNetwork_TryAddCatmullRomPath( &net.n, common::span_t<const vec3d_t>{ pts.data(), pts.size() }, true,
                                                &net.ids, &path ) == geometry_status_t::OK );
    common::vector_t<curve_sample_t> s{};
    REQUIRE( common::Vector_Init( &s, &net.allocator ) );
    curve_sampling_options_t o{};
    o.fMaxSpacing = 0.25;
    REQUIRE( CurveSampling_TrySamplePath( &net.n, path, o, &s ) == geometry_status_t::OK );
    // After correction the last frame rotates onto the first within the
    // tiny rotation of one step: compare normals across the wrap.
    const curve_sample_t &first = s.pData[0], &last = s.pData[s.nCount - 1];
    const double wrapDot = math::Vec3d_Dot( first.normal, last.normal );
    CHECK( wrapDot > 0.98 );
    common::Vector_Shutdown( &s );
}

// ===========================================================================
// Sweep and lathe
// ===========================================================================

namespace {
std::vector<vec2d_t> Circle( int n, double r )
{
    std::vector<vec2d_t> c;
    for ( int i = 0; i < n; ++i ) { c.push_back( { r * std::cos( 2 * kPi * i / n ), r * std::sin( 2 * kPi * i / n ) } ); }
    return c;
}
double PolyArea( int n, double r ) { return 0.5 * n * r * r * std::sin( 2 * kPi / n ); }
} // namespace

TEST_CASE( "Sweep: straight pipe with caps is a closed prism", "[Curves][Sweep]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 0, 0, 5 } );
    const auto p = net.Path( { { net.Line( a, b ), false } }, false );
    const auto prof = Circle( 16, 0.5 );
    sweep_options_t o{};
    o.sampling.fMaxSpacing = 1.0;
    o.sampling.initialUp = Vec3d_Make( 1, 0, 0 );
    editable_mesh_t m{};
    const sweep_report_t r = Sweep_TryAlongPath( &net.n, p, common::span_t<const vec2d_t>{ prof.data(), prof.size() }, o,
                                                 &net.allocator, &m );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.bClosed );
    CHECK( r.cRings == 6u );
    CHECK( r.cWallFaces == 5u * 16u );
    CHECK( r.cCapFaces == 2u * 14u );
    CHECK( EditableMesh_SignedVolume( &m ) == Approx( PolyArea( 16, 0.5 ) * 5.0 ) );
    const mesh_validation_result_t v = MeshValidation_Validate( &m );
    CHECK( v.bReciprocalTwins );
    CHECK( v.bEulerValid );
    CHECK( MeshValidation_ValidateGeometry( &m, geometry_policy_t{}, {} ).status == geometry_status_t::OK );
    EditableMesh_Shutdown( &m );
}

TEST_CASE( "Sweep: clockwise profile still faces outward", "[Curves][Sweep]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 3, 0, 0 } );
    const auto p = net.Path( { { net.Line( a, b ), false } }, false );
    auto prof = Circle( 8, 1.0 );
    std::reverse( prof.begin(), prof.end() );
    editable_mesh_t m{};
    const sweep_report_t r = Sweep_TryAlongPath( &net.n, p, common::span_t<const vec2d_t>{ prof.data(), prof.size() },
                                                 sweep_options_t{}, &net.allocator, &m );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( EditableMesh_SignedVolume( &m ) == Approx( PolyArea( 8, 1.0 ) * 3.0 ) );
    EditableMesh_Shutdown( &m );
}

TEST_CASE( "Sweep: closed path gives a torus-like ring", "[Curves][Sweep]" )
{
    Net net;
    std::vector<vec3d_t> pts;
    for ( int i = 0; i < 12; ++i ) {
        const double a = 2 * kPi * i / 12;
        pts.push_back( Vec3d_Make( 5 * std::cos( a ), 5 * std::sin( a ), 0 ) );
    }
    common::u32 path = 0;
    REQUIRE( CurveNetwork_TryAddCatmullRomPath( &net.n, common::span_t<const vec3d_t>{ pts.data(), pts.size() }, true,
                                                &net.ids, &path ) == geometry_status_t::OK );
    const auto prof = Circle( 8, 0.5 );
    sweep_options_t o{};
    o.sampling.fMaxSpacing = 0.5;
    editable_mesh_t m{};
    const sweep_report_t r = Sweep_TryAlongPath( &net.n, path, common::span_t<const vec2d_t>{ prof.data(), prof.size() },
                                                 o, &net.allocator, &m );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK( r.bClosed );
    CHECK( r.cCapFaces == 0u );
    CHECK( EditableMesh_EulerCharacteristic( &m ) == 0 ); // genus 1
    CHECK( EditableMesh_SignedVolume( &m ) > 0.0 );
    CHECK( MeshValidation_ValidateGeometry( &m, geometry_policy_t{}, {} ).status == geometry_status_t::OK );
    EditableMesh_Shutdown( &m );
}

TEST_CASE( "Sweep: open profile makes a ribbon", "[Curves][Sweep]" )
{
    Net net;
    const auto a = net.Node( { 0, 0, 0 } ), b = net.Node( { 4, 0, 0 } );
    const auto p = net.Path( { { net.Line( a, b ), false } }, false );
    const vec2d_t prof[] = { { 0, -1 }, { 0, 1 } };
    sweep_options_t o{};
    o.bProfileClosed = false;
    editable_mesh_t m{};
    const sweep_report_t r = Sweep_TryAlongPath( &net.n, p, common::span_t<const vec2d_t>{ prof, 2 }, o, &net.allocator, &m );
    REQUIRE( r.status == geometry_status_t::OK );
    CHECK_FALSE( r.bClosed );
    CHECK( r.cWallFaces == 4u );
    CHECK( EditableMesh_SurfaceArea( &m ) == Approx( 8.0 ) );
    EditableMesh_Shutdown( &m );
}

TEST_CASE( "Lathe: rectangle profile makes a cylinder; arc makes a sphere-like solid", "[Curves][Lathe]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    SECTION( "cylinder from a profile touching the axis at both ends" ) {
        const vec2d_t prof[] = { { 0, 0 }, { 2, 0 }, { 2, 3 }, { 0, 3 } };
        lathe_options_t o{};
        o.segments = 24;
        editable_mesh_t m{};
        const sweep_report_t r = Sweep_TryLathe( common::span_t<const vec2d_t>{ prof, 4 }, Vec3d_Make( 0, 0, 0 ),
                                                 Vec3d_Make( 0, 0, 1 ), o, &allocator, &m );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.bClosed );
        CHECK( EditableMesh_SignedVolume( &m ) == Approx( PolyArea( 24, 2.0 ) * 3.0 ) );
        CHECK( MeshValidation_Validate( &m ).bEulerValid );
        EditableMesh_Shutdown( &m );
    }
    SECTION( "sphere from a half circle" ) {
        std::vector<vec2d_t> prof;
        const int k = 12;
        for ( int i = 0; i <= k; ++i ) {
            const double a = -kPi / 2 + kPi * i / k;
            prof.push_back( { i == 0 || i == k ? 0.0 : std::cos( a ), std::sin( a ) } );
        }
        lathe_options_t o{};
        o.segments = 24;
        editable_mesh_t m{};
        const sweep_report_t r = Sweep_TryLathe( common::span_t<const vec2d_t>{ prof.data(), prof.size() },
                                                 Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 0, 0, 1 ), o, &allocator, &m );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK( r.bClosed );
        const double vol = EditableMesh_SignedVolume( &m );
        CHECK( vol > 0.95 * 4.0 / 3.0 * kPi * 0.9 );
        CHECK( vol < 4.0 / 3.0 * kPi );
        CHECK( EditableMesh_VertexCount( &m ) == 2u + 11u * 24u ); // poles shared
        CHECK( MeshValidation_ValidateGeometry( &m, geometry_policy_t{}, {} ).status == geometry_status_t::OK );
        EditableMesh_Shutdown( &m );
    }
    SECTION( "partial revolution is open" ) {
        const vec2d_t prof[] = { { 1, 0 }, { 1, 2 } };
        lathe_options_t o{};
        o.angle = kPi;
        o.segments = 8;
        editable_mesh_t m{};
        const sweep_report_t r = Sweep_TryLathe( common::span_t<const vec2d_t>{ prof, 2 }, Vec3d_Make( 0, 0, 0 ),
                                                 Vec3d_Make( 0, 0, 1 ), o, &allocator, &m );
        REQUIRE( r.status == geometry_status_t::OK );
        CHECK_FALSE( r.bClosed );
        CHECK( r.cWallFaces == 8u );
        EditableMesh_Shutdown( &m );
    }
    SECTION( "negative radius rejected" ) {
        const vec2d_t prof[] = { { -1, 0 }, { 1, 2 } };
        editable_mesh_t m{};
        CHECK( Sweep_TryLathe( common::span_t<const vec2d_t>{ prof, 2 }, Vec3d_Make( 0, 0, 0 ), Vec3d_Make( 0, 0, 1 ),
                               lathe_options_t{}, &allocator, &m ).status == geometry_status_t::INVALID_ARGUMENT );
        CHECK_FALSE( EditableMesh_IsInitialized( &m ) );
    }
}

} // namespace cypher::editor::geometry
