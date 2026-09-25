//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookProducts_Tests.cpp
//  Purpose: Tests the cook products over one small map (two brushes, a
//           closed mesh, a patch, a heightfield with a hole): collision for
//           every kind, the shared surface soup, navigation candidates,
//           static batches, visibility inputs, lightmap charts and atlas,
//           and the compiler interchange round trip and tamper checks.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_CompilerInterchange.h"
#include "CypherGeometry_CookBatching.h"
#include "CypherGeometry_CookCollision.h"
#include "CypherGeometry_CookLighting.h"
#include "CypherGeometry_CookNavigation.h"
#include "CypherGeometry_CookVisibility.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

struct Map {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_document_t doc{};
    geometry_snapshot_t snap{};
    cook_key_set_t keys{};
    geometry_source_id_t floorId{}, wallId{}, meshId{}, patchId{}, fieldId{};

    Map()
    {
        REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
        geometry_source_id_allocator_t ids = doc.sourceIds.allocator;
        auto brush = [&]( math::vec3d_t lo, math::vec3d_t hi, common::u64 material ) {
            brush_solid_t box{};
            REQUIRE( BrushGenerator_TryMakeBox( &box, &allocator, doc.policy, &ids, math::Vec3d_Scale( math::Vec3d_Add( lo, hi ), 0.5 ),
                                                math::Vec3d_Scale( math::Vec3d_Subtract( hi, lo ), 0.5 ) ) == geometry_status_t::OK );
            brush_source_t src{};
            REQUIRE( BrushSource_TryBuildDefault( &box, &allocator, doc.policy, &src ) == geometry_status_t::OK );
            for ( common::usize i = 0; i < BrushSideAttributeStore_Count( &src.attributes ); ++i ) {
                geometry_brush_side_attributes_t r{};
                REQUIRE( BrushSideAttributeStore_TryGet( &src.attributes, i, &r ) == geometry_status_t::OK );
                r.material.value = material;
                REQUIRE( BrushSideAttributeStore_TrySet( &src.attributes, doc.policy.numerical, i, r ) == geometry_status_t::OK );
            }
            REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &src ) == geometry_status_t::OK );
            const geometry_source_id_t id = src.solid.sourceId;
            BrushSource_Shutdown( &src );
            BrushSolid_Shutdown( &box );
            return id;
        };
        floorId = brush( math::Vec3d_Make( 0, 0, -16 ), math::Vec3d_Make( 512, 512, 0 ), 5u );
        wallId = brush( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 16, 512, 128 ), 6u );
        // A closed mesh cube.
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
        geometry_primitive_t p{};
        p.box.lo = math::Vec3d_Make( 100, 100, 0 );
        p.box.hi = math::Vec3d_Make( 164, 164, 64 );
        p.material.value = 7u;
        REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, doc.policy, &ids, &frag ) == geometry_status_t::OK );
        meshId = frag.meshes.pData[0]->sourceId;
        REQUIRE( GeometryFragment_TryInsert( &frag, &doc, nullptr ) == geometry_status_t::OK );
        GeometryFragment_Shutdown( &frag );
        ids = doc.sourceIds.allocator;
        // A gently bent patch facing up.
        patch_surface_t patch{};
        const geometry_source_id_result_t pid = GeometrySourceIdAllocator_Allocate( &ids );
        REQUIRE( Patch_TryInitFlat( &patch, &allocator, patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, math::Vec3d_Make( 200, 200, 0 ),
                                    math::Vec3d_Make( 64, 0, 0 ), math::Vec3d_Make( 0, 64, 0 ), pid.id, &ids ) == geometry_status_t::OK );
        patch_control_t mid = *Patch_Control( &patch, 1u, 1u );
        mid.position.z = 8.0;
        REQUIRE( Patch_TrySetControl( &patch, 1u, 1u, mid ) == geometry_status_t::OK );
        patch.materialId = 12u;
        patchId = patch.sourceId;
        REQUIRE( GeometryDocument_TryAddPatch( &doc, &patch ) == geometry_status_t::OK );
        Patch_Shutdown( &patch );
        // An 8 x 8 heightfield in 4 x 4 tiles with one hole.
        heightfield_t field{};
        const geometry_source_id_result_t fid = GeometrySourceIdAllocator_Allocate( &ids );
        REQUIRE( HeightField_TryInit( &field, &allocator, math::Vec3d_Make( 300, 0, 0 ), 16.0, 8u, 8u, 4u, fid.id, &ids ) == geometry_status_t::OK );
        REQUIRE( HeightField_TrySetHole( &field, 2u, 3u, true ) == geometry_status_t::OK );
        fieldId = field.sourceId;
        REQUIRE( GeometryDocument_TryAddHeightField( &doc, &field ) == geometry_status_t::OK );
        HeightField_Shutdown( &field );

        REQUIRE( GeometrySnapshot_TakeFromDocument( &snap, &doc ) == geometry_status_t::OK );
        REQUIRE( CookKeySet_Init( &keys, &allocator ) == geometry_status_t::OK );
        REQUIRE( CookKeySet_TryBuild( &keys, &snap ) == geometry_status_t::OK );
    }
    ~Map()
    {
        CookKeySet_Shutdown( &keys );
        GeometrySnapshot_Shutdown( &snap );
        GeometryDocument_Shutdown( &doc );
    }
};

struct Soup {
    cook_surface_soup_t s{};
    explicit Soup( const Map &m )
    {
        REQUIRE( CookSurfaces_Init( &s, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        REQUIRE( CookSurfaces_TryBuild( &m.snap, &s ) == geometry_status_t::OK );
    }
    ~Soup() { CookSurfaces_Shutdown( &s ); }
};

} // namespace

TEST_CASE( "Collision cooks patches and heightfields as well as brushes and meshes", "[geometry][cook]" )
{
    Map m;
    cook_collision_t col{};
    REQUIRE( CookCollision_Init( &col, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( CookCollision_TryBuild( &m.snap, &m.keys, nullptr, &col ) == geometry_status_t::OK );
    REQUIRE( col.surfaces.nCount == 5u );
    CHECK( col.diagnostics.nCount == 0u );
    for ( common::usize i = 0; i < col.surfaces.nCount; ++i ) {
        const cook_collision_surface_t &s = col.surfaces.pData[i];
        CHECK( s.cTriangles > 0u );
        if ( s.objectId.value == m.patchId.value ) {
            CHECK( s.kind == cook_source_kind_t::PATCH );
            CHECK_FALSE( s.bClosed );
            for ( common::u32 t = 0; t < s.cTriangles; ++t ) { CHECK( col.triangleElement.pData[s.iFirstTriangle + t].value == m.patchId.value ); }
        }
        if ( s.objectId.value == m.fieldId.value ) {
            CHECK( s.kind == cook_source_kind_t::HEIGHTFIELD );
            CHECK( s.cVertices == 81u );          // one per sample, shared by tiles
            CHECK( s.cTriangles == 2u * 64u - 2u ); // one cell is a hole
        }
    }
    // Reuse: an unchanged rebuild copies everything.
    cook_collision_t again{};
    REQUIRE( CookCollision_Init( &again, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( CookCollision_TryBuild( &m.snap, &m.keys, &col, &again ) == geometry_status_t::OK );
    CHECK( again.cSurfacesReused == 5u );
    CHECK( common::ContentHash_Equals( again.contentHash, col.contentHash ) );
    CookCollision_Shutdown( &again );
    CookCollision_Shutdown( &col );
}

TEST_CASE( "The surface soup carries every object with provenance and materials", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    const cook_surface_soup_t &s = soup.s;
    REQUIRE( s.objects.nCount == 5u );
    CHECK( s.problems.nCount == 0u );
    for ( common::usize i = 1; i < s.objects.nCount; ++i ) { CHECK( s.objects.pData[i - 1].objectId.value < s.objects.pData[i].objectId.value ); }
    for ( common::usize t = 0; t < s.triangles.nCount; ++t ) {
        const cook_soup_triangle_t &tri = s.triangles.pData[t];
        const cook_soup_object_t &o = s.objects.pData[tri.iObject];
        CHECK( t >= o.iFirstTriangle );
        CHECK( t < o.iFirstTriangle + o.cTriangles );
        const common::u64 id = o.objectId.value;
        const common::u64 want = id == m.floorId.value ? 5u : id == m.wallId.value ? 6u : id == m.meshId.value ? 7u : id == m.patchId.value ? 12u : 0u;
        CHECK( tri.material.value == want );
        CHECK( std::fabs( math::Vec3d_LengthSquared( tri.normal ) - 1.0 ) < 1e-9 );
    }
    for ( const cook_soup_object_t &o : std::vector<cook_soup_object_t>( s.objects.pData, s.objects.pData + s.objects.nCount ) ) {
        if ( o.kind == cook_source_kind_t::BRUSH ) {
            CHECK( o.cTriangles == 12u );
            CHECK( o.bConvex );
        }
        if ( o.kind == cook_source_kind_t::MESH ) { CHECK( o.bClosed ); }
        if ( o.kind == cook_source_kind_t::PATCH || o.kind == cook_source_kind_t::HEIGHTFIELD ) { CHECK_FALSE( o.bClosed ); }
    }
}

TEST_CASE( "Navigation candidates are the up-facing triangles within the slope", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    cook_navigation_t nav{};
    REQUIRE( CookNavigation_Init( &nav, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( CookNavigation_TryBuild( &soup.s, cook_navigation_options_t{}, &nav ) == geometry_status_t::OK );
    const common::usize cCandidates = nav.triangleSlope.nCount;
    CHECK( nav.indices.nCount == 3u * cCandidates );
    std::set<common::u64> objects;
    for ( common::usize t = 0; t < cCandidates; ++t ) {
        CHECK( nav.triangleSlope.pData[t] <= 0.7853982f );
        objects.insert( nav.triangleObject.pData[t].value );
    }
    // Floor top, wall top, cube top, the patch and the terrain; no walls, no undersides.
    CHECK( objects.count( m.floorId.value ) == 1u );
    CHECK( objects.count( m.meshId.value ) == 1u );
    CHECK( objects.count( m.patchId.value ) == 1u );
    CHECK( objects.count( m.fieldId.value ) == 1u );
    // Only exactly flat triangles at slope 0: floor, wall and cube tops, the
    // flat terrain, and the bent patch's corner triangles, which lie wholly
    // on its flat border.
    common::usize cPatchFlat = 0;
    for ( common::usize t = 0; t < soup.s.triangles.nCount; ++t ) {
        const cook_soup_triangle_t &tri = soup.s.triangles.pData[t];
        if ( soup.s.objects.pData[tri.iObject].objectId.value != m.patchId.value ) { continue; }
        bool bFlat = true;
        for ( common::u32 k = 0; k < 3; ++k ) { bFlat = bFlat && soup.s.positions.pData[tri.v[k]].z == 0.0; }
        cPatchFlat += bFlat ? 1u : 0u;
    }
    CHECK( cPatchFlat < 16u ); // most of the patch is curved
    const common::usize cExpectedFlat = 2u + 2u + 2u + ( 2u * 64u - 2u ) + cPatchFlat;
    cook_navigation_options_t flatOnly{};
    flatOnly.maxSlopeRadians = 0.0;
    REQUIRE( CookNavigation_TryBuild( &soup.s, flatOnly, &nav ) == geometry_status_t::OK );
    CHECK( nav.triangleSlope.nCount == cExpectedFlat );
    cook_navigation_options_t bad{};
    bad.up = math::Vec3d_Make( 0, 0, 2 );
    CHECK( CookNavigation_TryBuild( &soup.s, bad, &nav ) == geometry_status_t::INVALID_ARGUMENT );
    CookNavigation_Shutdown( &nav );
}

TEST_CASE( "Batches never mix materials and keep every triangle's provenance", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    cook_batching_t b{};
    REQUIRE( CookBatching_Init( &b, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    cook_batching_options_t o{};
    o.chunkSize = 256.0;
    REQUIRE( CookBatching_TryBuild( &soup.s, o, &b ) == geometry_status_t::OK );
    CHECK( b.triangles.nCount == soup.s.triangles.nCount );
    std::vector<int> seen( soup.s.triangles.nCount, 0 );
    for ( common::usize i = 0; i < b.batches.nCount; ++i ) {
        const cook_batch_t &batch = b.batches.pData[i];
        common::u32 cInRanges = 0;
        for ( common::u32 r = 0; r < batch.cRanges; ++r ) {
            const cook_batch_range_t &range = b.ranges.pData[batch.iFirstRange + r];
            cInRanges += range.cTriangles;
            for ( common::u32 t = 0; t < range.cTriangles; ++t ) {
                const common::u32 tri = b.triangles.pData[range.iFirstTriangle + t];
                ++seen[tri];
                CHECK( soup.s.triangles.pData[tri].material.value == batch.material.value );
                CHECK( soup.s.objects.pData[soup.s.triangles.pData[tri].iObject].objectId.value == range.objectId.value );
            }
        }
        CHECK( cInRanges == batch.cTriangles );
    }
    for ( const int n : seen ) { CHECK( n == 1 ); }
    // The floor (512 wide) spans several 256 chunks; smaller chunks, more batches.
    const common::usize cBig = b.batches.nCount;
    o.chunkSize = 64.0;
    const common::content_hash_t before = b.contentHash;
    REQUIRE( CookBatching_TryBuild( &soup.s, o, &b ) == geometry_status_t::OK );
    CHECK( b.batches.nCount > cBig );
    o.chunkSize = 256.0;
    REQUIRE( CookBatching_TryBuild( &soup.s, o, &b ) == geometry_status_t::OK );
    CHECK( common::ContentHash_Equals( b.contentHash, before ) );
    CookBatching_Shutdown( &b );
}

TEST_CASE( "Visibility inputs separate sealing hulls and shells from detail", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    cook_visibility_t v{};
    REQUIRE( CookVisibility_Init( &v, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( CookVisibility_TryBuild( &m.snap, &soup.s, &v ) == geometry_status_t::OK );
    CHECK( v.hulls.nCount == 2u );
    CHECK( v.planes.nCount == 12u );
    REQUIRE( v.shells.nCount == 1u );
    CHECK( v.shells.pData[0].objectId.value == m.meshId.value );
    CHECK( v.detail.nCount == 2u );
    for ( common::usize h = 0; h < v.hulls.nCount; ++h ) {
        if ( v.hulls.pData[h].brushId.value == m.floorId.value ) {
            CHECK( v.hulls.pData[h].lo[2] == -16.0 );
            CHECK( v.hulls.pData[h].hi[0] == 512.0 );
        }
    }
    CookVisibility_Shutdown( &v );
}

TEST_CASE( "Lightmap charts cover every triangle once and pack without overlap", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    cook_lighting_t l{};
    REQUIRE( CookLighting_Init( &l, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    cook_lighting_options_t o{};
    o.texelsPerUnit = 0.5;
    o.pageSize = 256u;
    REQUIRE( CookLighting_TryBuild( &soup.s, o, &l ) == geometry_status_t::OK );
    REQUIRE( l.chartTriangles.nCount == soup.s.triangles.nCount );
    std::vector<int> seen( soup.s.triangles.nCount, 0 );
    for ( const common::u32 t : std::vector<common::u32>( l.chartTriangles.pData, l.chartTriangles.pData + l.chartTriangles.nCount ) ) { ++seen[t]; }
    for ( const int n : seen ) { CHECK( n == 1 ); }
    // Each brush box is six charts (its faces meet at 90 degrees).
    common::usize cFloorCharts = 0;
    for ( common::usize c = 0; c < l.charts.nCount; ++c ) { cFloorCharts += l.charts.pData[c].objectId.value == m.floorId.value ? 1u : 0u; }
    CHECK( cFloorCharts == 6u );
    // Charts in one page never overlap; UVs lie inside their chart.
    for ( common::usize a = 0; a < l.charts.nCount; ++a ) {
        const cook_light_chart_t &x = l.charts.pData[a];
        CHECK( x.x + x.w <= o.pageSize );
        CHECK( x.y + x.h <= o.pageSize );
        for ( common::usize b2 = a + 1; b2 < l.charts.nCount; ++b2 ) {
            const cook_light_chart_t &y = l.charts.pData[b2];
            if ( x.page != y.page ) { continue; }
            const bool bApart = x.x + x.w <= y.x || y.x + y.w <= x.x || x.y + x.h <= y.y || y.y + y.h <= x.y;
            CHECK( bApart );
        }
        for ( common::u32 t = 0; t < x.cTriangles; ++t ) {
            for ( common::u32 k = 0; k < 3; ++k ) {
                const float u = l.cornerUvs.pData[6u * ( x.iFirstTriangle + t ) + 2u * k] * o.pageSize;
                const float v = l.cornerUvs.pData[6u * ( x.iFirstTriangle + t ) + 2u * k + 1u] * o.pageSize;
                CHECK( u >= static_cast<float>( x.x ) );
                CHECK( u <= static_cast<float>( x.x + x.w ) );
                CHECK( v >= static_cast<float>( x.y ) );
                CHECK( v <= static_cast<float>( x.y + x.h ) );
            }
        }
    }
    // The 512-unit floor top at 0.5 texels/unit needs shrinking to fit 256.
    bool bShrunk = false;
    for ( common::usize c = 0; c < l.charts.nCount; ++c ) { bShrunk = bShrunk || l.charts.pData[c].scale < 1.0; }
    CHECK( bShrunk );
    CHECK( l.cPages >= 1u );
    const common::content_hash_t h = l.contentHash;
    REQUIRE( CookLighting_TryBuild( &soup.s, o, &l ) == geometry_status_t::OK );
    CHECK( common::ContentHash_Equals( h, l.contentHash ) );
    CookLighting_Shutdown( &l );
}

TEST_CASE( "The compiler interchange round-trips exactly and rejects tampering", "[geometry][cook]" )
{
    Map m;
    Soup soup( m );
    common::text_buffer_t text{};
    REQUIRE( common::TextBuffer_Init( &text, common::Allocator_GetSystem() ) );
    REQUIRE( GeometryInterchange_TryWrite( &m.keys, &soup.s, &text ) == geometry_status_t::OK );
    const std::string written( common::TextBuffer_Data( &text ), common::TextBuffer_Length( &text ) );
    CHECK( written.find( "cypher.geometry.interchange" ) != std::string::npos );

    geometry_interchange_t in{};
    REQUIRE( GeometryInterchange_Init( &in, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( GeometryInterchange_TryRead( common::TextBuffer_View( &text ), &in ) == geometry_status_t::OK );
    CHECK( in.revision == soup.s.revision );
    CHECK( in.sources.nCount == m.keys.keys.nCount );
    REQUIRE( in.soup.positions.nCount == soup.s.positions.nCount );
    REQUIRE( in.soup.triangles.nCount == soup.s.triangles.nCount );
    REQUIRE( in.soup.objects.nCount == soup.s.objects.nCount );
    for ( common::usize i = 0; i < soup.s.positions.nCount; ++i ) {
        CHECK( in.soup.positions.pData[i].x == soup.s.positions.pData[i].x );
        CHECK( in.soup.positions.pData[i].z == soup.s.positions.pData[i].z );
    }
    for ( common::usize t = 0; t < soup.s.triangles.nCount; ++t ) {
        CHECK( in.soup.triangles.pData[t].v[1] == soup.s.triangles.pData[t].v[1] );
        CHECK( in.soup.triangles.pData[t].elementId.value == soup.s.triangles.pData[t].elementId.value );
        CHECK( in.soup.triangles.pData[t].material.value == soup.s.triangles.pData[t].material.value );
    }
    // A changed revision no longer matches the content hash.
    std::string tampered = written;
    const std::string::size_type at = tampered.find( "\"revision\"" );
    REQUIRE( at != std::string::npos );
    const std::string::size_type digit = tampered.find_first_of( "0123456789", at + 10 );
    tampered[digit] = tampered[digit] == '9' ? '8' : static_cast<char>( tampered[digit] + 1 );
    CHECK( GeometryInterchange_TryRead( common::string_view_t{ tampered.data(), tampered.size() }, &in ) == geometry_status_t::CORRUPT_STATE );
    CHECK( in.soup.triangles.nCount == 0u );
    // Another schema is refused.
    std::string other = written;
    other.replace( other.find( "cypher.geometry.interchange" ), 27, "cypher.geometry.somethingxx" );
    CHECK( GeometryInterchange_TryRead( common::string_view_t{ other.data(), other.size() }, &in ) == geometry_status_t::UNSUPPORTED );
    GeometryInterchange_Shutdown( &in );
    common::TextBuffer_Shutdown( &text );
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

TEST_CASE( "Every allocation failure in the soup and lighting cooks fails cleanly", "[geometry][cook][allocation]" )
{
    Map m;
    auto run = [&]( const common::allocator_t *pA, bool *pSoupFailed ) {
        cook_surface_soup_t soup{};
        cook_lighting_t light{};
        REQUIRE( CookSurfaces_Init( &soup, pA ) == geometry_status_t::OK );
        REQUIRE( CookLighting_Init( &light, pA ) == geometry_status_t::OK );
        geometry_status_t st = CookSurfaces_TryBuild( &m.snap, &soup );
        *pSoupFailed = st != geometry_status_t::OK;
        if ( st != geometry_status_t::OK ) {
            CHECK( st == geometry_status_t::ALLOCATION_FAILED );
            CHECK( soup.triangles.nCount == 0u );
        } else {
            // A lower layer must never turn an allocation failure into "bad
            // geometry" - that would silently drop an object.
            for ( common::usize k = 0; k < soup.problems.nCount; ++k ) { UNSCOPED_INFO( "problem object " << soup.problems.pData[k].objectId.value << " status " << static_cast<int>( soup.problems.pData[k].status ) ); }
            CHECK( soup.problems.nCount == 0u );
            CHECK( soup.objects.nCount == 5u );
            st = CookLighting_TryBuild( &soup, cook_lighting_options_t{}, &light );
            if ( st != geometry_status_t::OK ) {
                CHECK( st == geometry_status_t::ALLOCATION_FAILED );
                CHECK( light.charts.nCount == 0u );
            }
        }
        CookLighting_Shutdown( &light );
        CookSurfaces_Shutdown( &soup );
        return st;
    };
    common::usize cOperation = 0u;
    {
        fail_state_t probe{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
        bool bFailed = false;
        REQUIRE( run( &allocator, &bFailed ) == geometry_status_t::OK );
        cOperation = probe.cCalls;
        CHECK( probe.cLive == 0u );
    }
    for ( common::usize i = 1; i <= cOperation; ++i ) {
        CAPTURE( i );
        fail_state_t state{};
        state.iFailOnCall = i;
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        bool bFailed = false;
        (void)run( &allocator, &bFailed );
        CHECK( state.cLive == 0u );
    }
}

} // namespace cypher::editor::geometry
