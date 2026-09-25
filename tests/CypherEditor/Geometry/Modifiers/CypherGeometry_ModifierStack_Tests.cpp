//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_ModifierStack_Tests.cpp
//  Purpose: Tests modifier stacks: mirror with weld closing a half mesh,
//           mirror refusing a face in the plane, arrays with and without
//           weld, radial arrays, bend, taper, stacking with subdivision,
//           provenance, collapse identity, validation, and allocation
//           failure.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_ModifierStack.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

geometry_source_id_t Id( common::u64 v )
{
    geometry_source_id_t id{};
    id.value = v;
    return id;
}

// The unit box [0,1]^3 with its x = 0 face left out when bOpen (a half
// mesh ready to be mirrored across x = 0).
void BoxDescription( mesh_source_description_t *pD, bool bOpen )
{
    MeshSourceDescription_Clear( pD, Id( 1 ) );
    for ( int i = 0; i < 8; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex( pD, math::Vec3d_Make( ( i & 1 ) ? 1.0 : 0.0, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                                                     Id( 2 + i ), nullptr ) == geometry_status_t::OK );
    }
    const std::vector<std::vector<common::u32>> faces = {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 } /* x = 0 */, { 1, 3, 7, 5 } };
    for ( common::usize f = 0; f < faces.size(); ++f ) {
        if ( bOpen && f == 4u ) { continue; }
        mesh_face_attributes_t a{};
        a.material.value = 5u;
        REQUIRE( MeshSourceDescription_TryAddFace( pD, common::span_t<const common::u32>{ faces[f].data(), faces[f].size() }, Id( 20 + f ), a,
                                                   nullptr ) == geometry_status_t::OK );
    }
}

struct Cage {
    mesh_source_t mesh{};
    explicit Cage( bool bOpen )
    {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, common::Allocator_GetSystem(), Id( 1 ) ) == geometry_status_t::OK );
        BoxDescription( &d, bOpen );
        REQUIRE( MeshSource_TryBuild( &d, common::Allocator_GetSystem(), &mesh ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
    }
    ~Cage() { MeshSource_Shutdown( &mesh ); }
};

struct Stack {
    modifier_stack_t s{};
    Stack() { REQUIRE( ModifierStack_Init( &s, common::Allocator_GetSystem() ) == geometry_status_t::OK ); }
    ~Stack() { ModifierStack_Shutdown( &s ); }
    void Push( const modifier_t &m ) { REQUIRE( ModifierStack_TryPush( &s, m ) == geometry_status_t::OK ); }
};

struct Result {
    modifier_result_t r{};
    Result() { REQUIRE( ModifierResult_Init( &r, common::Allocator_GetSystem() ) == geometry_status_t::OK ); }
    ~Result() { ModifierResult_Shutdown( &r ); }
};

double Volume( const mesh_source_description_t &d )
{
    double v = 0.0;
    for ( common::usize f = 0; f < d.faces.nCount; ++f ) {
        const mesh_source_face_t &face = d.faces.pData[f];
        const math::vec3d_t p0 = d.vertices.pData[d.corners.pData[face.iFirstCorner].iVertex].position;
        for ( common::u32 k = 1; k + 1 < face.cCorners; ++k ) {
            const math::vec3d_t p1 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k].iVertex].position;
            const math::vec3d_t p2 = d.vertices.pData[d.corners.pData[face.iFirstCorner + k + 1].iVertex].position;
            v += math::Vec3d_Dot( p0, math::Vec3d_Cross( p1, p2 ) );
        }
    }
    return v / 6.0;
}

modifier_t Mirror( common::u32 axis, double offset, bool bWeld = true )
{
    modifier_t m{};
    m.kind = modifier_kind_t::MIRROR;
    m.axis = axis;
    m.offset = offset;
    m.bWeld = bWeld;
    return m;
}

} // namespace

TEST_CASE( "Mirror with weld closes a half mesh into a solid", "[geometry][modifiers]" )
{
    Cage half( true );
    Stack stack;
    stack.Push( Mirror( 0u, 0.0 ) );
    Result res;
    REQUIRE( ModifierStack_TryEvaluate( &half.mesh, &stack.s, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.vertices.nCount == 12u ); // the four plane vertices are shared
    CHECK( res.r.mesh.faces.nCount == 10u );
    CHECK( std::fabs( Volume( res.r.mesh ) - 2.0 ) < 1e-12 );
    // Provenance: copies 0 and 1, five faces each, every face back to the cage.
    int perCopy[2] = { 0, 0 };
    for ( common::usize f = 0; f < res.r.faceCopies.nCount; ++f ) {
        REQUIRE( res.r.faceCopies.pData[f] < 2u );
        ++perCopy[res.r.faceCopies.pData[f]];
        CHECK( res.r.faceSources.pData[f] < 5u );
    }
    CHECK( perCopy[0] == 5 );
    CHECK( perCopy[1] == 5 );

    // Collapse: a closed, valid mesh; copy 0 keeps the cage IDs.
    geometry_source_id_allocator_t ids{};
    ids.next.value = 100u;
    mesh_source_t out{};
    REQUIRE( ModifierStack_TryCollapse( &half.mesh, &stack.s, common::Allocator_GetSystem(), &ids, &out ) == geometry_status_t::OK );
    CHECK( MeshSource_Validate( &out, common::Allocator_GetSystem() ).fault == mesh_source_fault_t::NONE );
    CHECK( MeshBoundary_CountBoundaryEdges( &out.mesh ) == 0u );
    CHECK( ids.next.value == 100u + 4u + 5u ); // four new vertices, five new faces
    geometry_mesh_face_handle_t h{};
    CHECK( MeshSource_TryFindFace( &out, Id( 20 ), &h ) );
    MeshSource_Shutdown( &out );

    // Without weld the halves stay apart (an open seam, 16 vertices).
    Stack loose;
    loose.Push( Mirror( 0u, 0.0, false ) );
    REQUIRE( ModifierStack_TryEvaluate( &half.mesh, &loose.s, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.vertices.nCount == 16u );
}

TEST_CASE( "Mirroring a face that lies in the plane is refused", "[geometry][modifiers]" )
{
    Cage closed( false );
    Stack stack;
    stack.Push( Mirror( 0u, 0.0 ) );
    Result res;
    CHECK( ModifierStack_TryEvaluate( &closed.mesh, &stack.s, &res.r ) == geometry_status_t::NON_MANIFOLD );
    CHECK( res.r.mesh.vertices.nCount == 0u );
    // Mirrored away from the box (x = 2), the two boxes are separate solids.
    Stack apart;
    apart.Push( Mirror( 0u, 2.0 ) );
    REQUIRE( ModifierStack_TryEvaluate( &closed.mesh, &apart.s, &res.r ) == geometry_status_t::OK );
    CHECK( std::fabs( Volume( res.r.mesh ) - 2.0 ) < 1e-12 );
}

TEST_CASE( "Arrays repeat, weld open strips, and turn about an axis", "[geometry][modifiers]" )
{
    Cage box( false );
    modifier_t arr{};
    arr.kind = modifier_kind_t::LINEAR_ARRAY;
    arr.cCopies = 3u;
    arr.step = math::Vec3d_Make( 2, 0, 0 );
    Stack stack;
    stack.Push( arr );
    Result res;
    REQUIRE( ModifierStack_TryEvaluate( &box.mesh, &stack.s, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.vertices.nCount == 24u );
    CHECK( res.r.mesh.faces.nCount == 18u );
    CHECK( std::fabs( Volume( res.r.mesh ) - 3.0 ) < 1e-12 );

    // An open quad strip: neighbouring copies share their edge vertices.
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    geometry_primitive_t p{};
    p.kind = geometry_primitive_kind_t::PLANE;
    p.box.lo = math::Vec3d_Make( 0, 0, 0 );
    p.box.hi = math::Vec3d_Make( 1, 1, 0 );
    geometry_source_id_allocator_t pids{};
    REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, geometry_policy_t{}, &pids, &frag ) == geometry_status_t::OK );
    Stack strip;
    arr.step = math::Vec3d_Make( 1, 0, 0 );
    strip.Push( arr );
    REQUIRE( ModifierStack_TryEvaluate( frag.meshes.pData[0], &strip.s, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.vertices.nCount == 8u );
    CHECK( res.r.mesh.faces.nCount == 3u );
    GeometryFragment_Shutdown( &frag );

    // Radial: four boxes around the Z axis through (-1, 0, 0), a full turn.
    modifier_t rad{};
    rad.kind = modifier_kind_t::RADIAL_ARRAY;
    rad.cCopies = 4u;
    rad.origin = math::Vec3d_Make( -1, 0, 0 );
    rad.direction = math::Vec3d_Make( 0, 0, 5 );
    rad.angle = 2.0 * math::CY_PI_D;
    Stack radial;
    radial.Push( rad );
    REQUIRE( ModifierStack_TryEvaluate( &box.mesh, &radial.s, &res.r ) == geometry_status_t::OK );
    CHECK( res.r.mesh.faces.nCount == 24u );
    CHECK( std::fabs( Volume( res.r.mesh ) - 4.0 ) < 1e-9 );
    double maxX = -1e9, minX = 1e9;
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
        maxX = std::fmax( maxX, res.r.mesh.vertices.pData[v].position.x );
        minX = std::fmin( minX, res.r.mesh.vertices.pData[v].position.x );
    }
    CHECK( std::fabs( maxX - 1.0 ) < 1e-9 );
    CHECK( std::fabs( minX + 3.0 ) < 1e-9 ); // the copy opposite the original
}

TEST_CASE( "Bend curls a strip onto a circle and taper narrows a box", "[geometry][modifiers]" )
{
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    geometry_primitive_t p{};
    p.kind = geometry_primitive_kind_t::PLANE;
    p.box.lo = math::Vec3d_Make( 0, 0, 0 );
    p.box.hi = math::Vec3d_Make( 8, 1, 0 );
    p.cSegments[0] = 8u;
    geometry_source_id_allocator_t pids{};
    REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, geometry_policy_t{}, &pids, &frag ) == geometry_status_t::OK );
    modifier_t bend{};
    bend.kind = modifier_kind_t::BEND;
    bend.axis = 1u;      // around Y
    bend.alongAxis = 0u; // along X; the strip curls up into Z
    bend.angle = math::CY_PI_D;
    Stack stack;
    stack.Push( bend );
    Result res;
    REQUIRE( ModifierStack_TryEvaluate( frag.meshes.pData[0], &stack.s, &res.r ) == geometry_status_t::OK );
    const double R = 8.0 / math::CY_PI_D;
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
        const math::vec3d_t q = res.r.mesh.vertices.pData[v].position;
        CHECK( std::fabs( std::hypot( q.x, q.z - R ) - R ) < 1e-9 ); // on the circle through the origin
    }
    GeometryFragment_Shutdown( &frag );

    Cage box( false );
    modifier_t taper{};
    taper.kind = modifier_kind_t::TAPER;
    taper.alongAxis = 0u;
    taper.factor = 0.5;
    Stack t;
    t.Push( taper );
    REQUIRE( ModifierStack_TryEvaluate( &box.mesh, &t.s, &res.r ) == geometry_status_t::OK );
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) {
        const math::vec3d_t q = res.r.mesh.vertices.pData[v].position;
        if ( q.x == 1.0 ) {
            CHECK( ( q.y == 0.0 || q.y == 0.5 ) );
            CHECK( ( q.z == 0.0 || q.z == 0.5 ) );
        }
    }
    // Frustum volume: h/3 (A1 + A2 + sqrt(A1 A2)) = 1/3 (1 + 0.25 + 0.5).
    CHECK( std::fabs( Volume( res.r.mesh ) - ( 1.0 + 0.25 + 0.5 ) / 3.0 ) < 0.05 ); // quads are not planar after taper
}

TEST_CASE( "Stacks run in order and subdivision keeps provenance", "[geometry][modifiers]" )
{
    Cage half( true );
    Stack stack;
    stack.Push( Mirror( 0u, 0.0 ) );
    modifier_t sub{};
    sub.kind = modifier_kind_t::SUBDIVIDE;
    sub.subdivision.cLevels = 1u;
    stack.Push( sub );
    modifier_t off = Mirror( 1u, 5.0 );
    off.bEnabled = false; // skipped
    stack.Push( off );
    Result res;
    REQUIRE( ModifierStack_TryEvaluate( &half.mesh, &stack.s, &res.r ) == geometry_status_t::OK );
    // 10 quads -> 40 after one level.
    CHECK( res.r.mesh.faces.nCount == 40u );
    for ( common::usize f = 0; f < res.r.mesh.faces.nCount; ++f ) {
        CHECK( res.r.faceCopies.pData[f] < 2u );
        CHECK( res.r.faceSources.pData[f] < 5u );
        CHECK( res.r.mesh.faces.pData[f].attributes.material.value == 5u );
    }
    common::usize cFromCage = 0;
    for ( common::usize v = 0; v < res.r.mesh.vertices.nCount; ++v ) { cFromCage += res.r.vertexSources.pData[v] != common::CY_U32_MAX ? 1u : 0u; }
    CHECK( cFromCage == 12u );
}

TEST_CASE( "Modifier parameters are validated", "[geometry][modifiers]" )
{
    modifier_t m{};
    m.kind = modifier_kind_t::LINEAR_ARRAY;
    m.cCopies = 1u;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    m.cCopies = kModifierCopiesMax + 1u;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    m = modifier_t{};
    m.kind = modifier_kind_t::RADIAL_ARRAY;
    m.direction = math::vec3d_t{};
    m.angle = 1.0;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    m.direction = math::Vec3d_Make( 0, 0, 1 );
    m.angle = 0.0;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    m = modifier_t{};
    m.kind = modifier_kind_t::BEND;
    m.axis = m.alongAxis = 2u;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    m = modifier_t{};
    m.kind = modifier_kind_t::SUBDIVIDE;
    m.subdivision.cLevels = kSubdivisionLevelsMax + 1u;
    CHECK( Modifier_Validate( m ) == geometry_status_t::INVALID_ARGUMENT );
    Stack stack;
    CHECK( ModifierStack_TryPush( &stack.s, m ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( stack.s.stages.nCount == 0u );
    stack.s.version = 99u;
    CHECK( ModifierStack_Validate( &stack.s ) == geometry_status_t::UNSUPPORTED );
}

namespace
{

struct fail_state_t {
    common::usize cCalls{ 0u };
    common::usize iFailOnCall{ common::CY_INVALID_SIZE };
    common::usize cLive{ 0u };
};

void *FailAllocate( void *pUser, common::usize cb, common::usize align ) noexcept
{
    auto *p = static_cast<fail_state_t *>( pUser );
    if ( ++p->cCalls == p->iFailOnCall ) { return nullptr; }
    void *pMem = common::Allocator_Allocate( common::Allocator_GetSystem(), cb, align );
    p->cLive += pMem != nullptr ? 1u : 0u;
    return pMem;
}

void FailFree( void *pUser, void *pMem, common::usize cb, common::usize align ) noexcept
{
    auto *p = static_cast<fail_state_t *>( pUser );
    p->cLive -= pMem != nullptr ? 1u : 0u;
    common::Allocator_Free( common::Allocator_GetSystem(), pMem, cb, align );
}

} // namespace

TEST_CASE( "Every modifier-stack allocation failure leaves an empty result and no leak", "[geometry][modifiers][allocation]" )
{
    Cage half( true );
    Stack stack;
    stack.Push( Mirror( 0u, 0.0 ) );
    modifier_t arr{};
    arr.kind = modifier_kind_t::LINEAR_ARRAY;
    arr.cCopies = 2u;
    arr.step = math::Vec3d_Make( 0, 3, 0 );
    stack.Push( arr );
    modifier_t sub{};
    sub.kind = modifier_kind_t::SUBDIVIDE;
    stack.Push( sub );
    common::usize cOperation = 0u;
    {
        fail_state_t probe{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
        modifier_result_t r{};
        REQUIRE( ModifierResult_Init( &r, &allocator ) == geometry_status_t::OK );
        const common::usize cStart = probe.cCalls;
        REQUIRE( ModifierStack_TryEvaluate( &half.mesh, &stack.s, &r ) == geometry_status_t::OK );
        cOperation = probe.cCalls - cStart;
        ModifierResult_Shutdown( &r );
        CHECK( probe.cLive == 0u );
    }
    REQUIRE( cOperation > 0u );
    for ( common::usize i = 1; i <= cOperation; ++i ) {
        CAPTURE( i );
        fail_state_t state{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        {
            modifier_result_t r{};
            REQUIRE( ModifierResult_Init( &r, &allocator ) == geometry_status_t::OK );
            state.iFailOnCall = state.cCalls + i;
            CHECK( ModifierStack_TryEvaluate( &half.mesh, &stack.s, &r ) == geometry_status_t::ALLOCATION_FAILED );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( r.mesh.vertices.nCount == 0u );
            ModifierResult_Shutdown( &r );
        }
        CHECK( state.cLive == 0u );
    }
}

} // namespace cypher::editor::geometry
