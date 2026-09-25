//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgOperations_Tests.cpp
//  Purpose: Tests brush-set Booleans (carve, union decomposition,
//           disjoint intersection, surface records, identity) and the
//           document-level Boolean commands for brushes and meshes
//           (replacement, keeping the cutter, split, atomic failure).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_CsgOperations.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshBuilder.h"
#include "CypherGeometry_Primitive.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace cypher::editor::geometry
{

namespace
{

using op_t = csg_operator_t;

// A box brush with every surface record's material set.
void MakeBrush( brush_source_t *pOut, math::vec3d_t lo, math::vec3d_t hi, common::u64 material, geometry_source_id_allocator_t *pIds )
{
    const geometry_policy_t policy{};
    brush_solid_t solid{};
    const math::vec3d_t c = math::Vec3d_Scale( math::Vec3d_Add( lo, hi ), 0.5 ), h = math::Vec3d_Scale( math::Vec3d_Subtract( hi, lo ), 0.5 );
    REQUIRE( BrushGenerator_TryMakeBox( &solid, common::Allocator_GetSystem(), policy, pIds, c, h ) == geometry_status_t::OK );
    REQUIRE( BrushSource_TryBuildDefault( &solid, common::Allocator_GetSystem(), policy, pOut ) == geometry_status_t::OK );
    BrushSolid_Shutdown( &solid );
    for ( common::usize i = 0; i < BrushSideAttributeStore_Count( &pOut->attributes ); ++i ) {
        geometry_brush_side_attributes_t r{};
        REQUIRE( BrushSideAttributeStore_TryGet( &pOut->attributes, i, &r ) == geometry_status_t::OK );
        r.material.value = material;
        REQUIRE( BrushSideAttributeStore_TrySet( &pOut->attributes, policy.numerical, i, r ) == geometry_status_t::OK );
    }
}

double SolidVolume( const brush_solid_t &solid )
{
    brush_boundary_t b{};
    REQUIRE( BrushBoundary_Init( &b, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( BrushBoundary_TryReconstruct( &b, &solid, geometry_policy_t{} ) == geometry_status_t::OK );
    editable_mesh_t m{};
    REQUIRE( EditableMesh_Init( &m, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    REQUIRE( MeshBuilder_TryBuildFromBoundary( &m, &b ) == geometry_status_t::OK );
    const double v = EditableMesh_SignedVolume( &m );
    EditableMesh_Shutdown( &m );
    BrushBoundary_Shutdown( &b );
    return v;
}

double FragmentVolume( const geometry_fragment_t &f )
{
    double v = 0.0;
    for ( common::usize i = 0; i < f.brushes.nCount; ++i ) { v += SolidVolume( f.brushes.pData[i]->solid ); }
    return v;
}

struct Brushes {
    geometry_source_id_allocator_t ids{};
    std::vector<brush_source_t *> a, b;
    ~Brushes()
    {
        for ( auto *p : a ) {
            BrushSource_Shutdown( p );
            delete p;
        }
        for ( auto *p : b ) {
            BrushSource_Shutdown( p );
            delete p;
        }
    }
    void AddA( math::vec3d_t lo, math::vec3d_t hi, common::u64 m )
    {
        a.push_back( new brush_source_t{} );
        MakeBrush( a.back(), lo, hi, m, &ids );
    }
    void AddB( math::vec3d_t lo, math::vec3d_t hi, common::u64 m )
    {
        b.push_back( new brush_source_t{} );
        MakeBrush( b.back(), lo, hi, m, &ids );
    }
    geometry_status_t Run( op_t op, geometry_fragment_t *pOut, common::vector_t<csg_brush_side_provenance_t> *pProv = nullptr )
    {
        std::vector<const brush_source_t *> pa( a.begin(), a.end() ), pb( b.begin(), b.end() );
        return CsgBrush_TryEvaluate( op, common::span_t<const brush_source_t *const>{ pa.data(), pa.size() },
                                     common::span_t<const brush_source_t *const>{ pb.data(), pb.size() }, geometry_policy_t{}, &ids, pOut, pProv );
    }
};

} // namespace

TEST_CASE( "Carving a hole through a brush leaves convex pieces with the cutter's walls", "[geometry][csg][brush]" )
{
    Brushes s;
    s.AddA( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 4, 4, 4 ), 1u );
    s.AddB( math::Vec3d_Make( 1, 1, -1 ), math::Vec3d_Make( 3, 3, 5 ), 2u );
    geometry_fragment_t frag{};
    REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
    common::vector_t<csg_brush_side_provenance_t> prov{};
    REQUIRE( common::Vector_Init( &prov, common::Allocator_GetSystem() ) );
    REQUIRE( s.Run( op_t::DIFFERENCE, &frag, &prov ) == geometry_status_t::OK );
    CHECK( frag.brushes.nCount >= 4u );
    CHECK( std::fabs( FragmentVolume( frag ) - ( 64.0 - 16.0 ) ) < 1e-9 );
    // Walls of the hole come from B and carry its material; the rest A's.
    common::usize cFromB = 0, cSides = 0;
    for ( common::usize i = 0; i < frag.brushes.nCount; ++i ) {
        const brush_source_t &src = *frag.brushes.pData[i];
        CHECK( BrushSource_Validate( &src, geometry_policy_t{} ).fault == brush_source_fault_t::NONE );
        for ( common::usize k = 0; k < src.solid.sides.nCount; ++k ) {
            ++cSides;
            const csg_brush_side_provenance_t *pP = nullptr;
            for ( common::usize p = 0; p < prov.nCount; ++p ) {
                if ( prov.pData[p].destinationSideId.value == src.solid.sides.pData[k].sourceId.value ) { pP = &prov.pData[p]; }
            }
            REQUIRE( pP != nullptr );
            geometry_brush_side_attributes_t r{};
            REQUIRE( BrushSideAttributeStore_TryGet( &src.attributes, src.solid.sides.pData[k].iAttributeIndex, &r ) == geometry_status_t::OK );
            CHECK( r.material.value == ( pP->iOperand == kCsgOperandB ? 2u : 1u ) );
            cFromB += pP->iOperand == kCsgOperandB ? 1u : 0u;
        }
    }
    CHECK( cFromB >= 4u );
    CHECK( prov.nCount == cSides );
    GeometryFragment_Shutdown( &frag );
}

TEST_CASE( "Brush union decomposes, intersection stays disjoint, containment empties", "[geometry][csg][brush]" )
{
    {
        Brushes s;
        s.AddA( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u );
        s.AddB( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ), 2u );
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        REQUIRE( s.Run( op_t::UNION, &frag ) == geometry_status_t::OK );
        CHECK( std::fabs( FragmentVolume( frag ) - 15.0 ) < 1e-9 );
        GeometryFragment_Shutdown( &frag );
    }
    {
        // Two overlapping A brushes; the pairwise intersections overlap and
        // must be made disjoint: the result is the union of A cut to B.
        Brushes s;
        s.AddA( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ), 1u );
        s.AddA( math::Vec3d_Make( 1, 0, 0 ), math::Vec3d_Make( 3, 2, 2 ), 1u );
        s.AddB( math::Vec3d_Make( 0.5, 0, 0 ), math::Vec3d_Make( 2.5, 2, 2 ), 2u );
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        REQUIRE( s.Run( op_t::INTERSECTION, &frag ) == geometry_status_t::OK );
        CHECK( std::fabs( FragmentVolume( frag ) - 8.0 ) < 1e-9 );
        GeometryFragment_Shutdown( &frag );
    }
    {
        Brushes s;
        s.AddA( math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 2, 2, 2 ), 1u );
        s.AddB( math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 4, 4, 4 ), 2u );
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
        const common::u64 before = s.ids.next.value;
        CHECK( s.Run( op_t::DIFFERENCE, &frag ) == geometry_status_t::OK );
        CHECK( frag.brushes.nCount == 0u );
        CHECK( s.ids.next.value == before );
        CHECK( s.Run( op_t::CLIP, &frag ) == geometry_status_t::UNSUPPORTED );
        GeometryFragment_Shutdown( &frag );
    }
}

TEST_CASE( "Document brush carve replaces operands atomically", "[geometry][csg][brush][document]" )
{
    for ( const bool bKeepB : { true, false } ) {
        CAPTURE( bKeepB );
        common::allocator_t allocator{ *common::Allocator_GetSystem() };
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
        geometry_source_id_allocator_t ids = doc.sourceIds.allocator;
        brush_source_t a{}, b{};
        MakeBrush( &a, math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 4, 4, 4 ), 1u, &ids );
        MakeBrush( &b, math::Vec3d_Make( 1, 1, -1 ), math::Vec3d_Make( 3, 3, 5 ), 2u, &ids );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &a ) == geometry_status_t::OK );
        REQUIRE( GeometryDocument_TryAddBrushSource( &doc, &b ) == geometry_status_t::OK );
        common::vector_t<geometry_source_id_t> pieces{};
        REQUIRE( common::Vector_Init( &pieces, &allocator ) );
        const geometry_source_id_t idA = a.solid.sourceId, idB = b.solid.sourceId;
        REQUIRE( GeometryDocument_TryCsgBrushes( &doc, op_t::DIFFERENCE, common::span_t<const geometry_source_id_t>{ &idA, 1u },
                                                 common::span_t<const geometry_source_id_t>{ &idB, 1u }, bKeepB, &pieces ) == geometry_status_t::OK );
        CHECK( GeometryDocument_FindBrush( &doc, idA ) == nullptr );
        CHECK( ( GeometryDocument_FindBrush( &doc, idB ) != nullptr ) == bKeepB );
        CHECK( doc.brushes.nCount == pieces.nCount + ( bKeepB ? 1u : 0u ) );
        double v = 0.0;
        for ( common::usize i = 0; i < pieces.nCount; ++i ) {
            const brush_solid_t *pS = GeometryDocument_FindBrush( &doc, pieces.pData[i] );
            REQUIRE( pS != nullptr );
            v += SolidVolume( *pS );
        }
        CHECK( std::fabs( v - 48.0 ) < 1e-9 );
        CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
        CHECK( GeometryDocument_ValidateBrushAttributes( &doc ) == geometry_status_t::OK );
        // Bad operands change nothing.
        const common::usize cBrushes = doc.brushes.nCount;
        const geometry_source_id_t bogus = pieces.pData[0];
        CHECK( GeometryDocument_TryCsgBrushes( &doc, op_t::DIFFERENCE, common::span_t<const geometry_source_id_t>{ &bogus, 1u },
                                               common::span_t<const geometry_source_id_t>{ &bogus, 1u }, false, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
        CHECK( doc.brushes.nCount == cBrushes );
        BrushSource_Shutdown( &a );
        BrushSource_Shutdown( &b );
        GeometryDocument_Shutdown( &doc );
    }
}

TEST_CASE( "Document mesh Booleans replace, keep the cutter, and split", "[geometry][csg][document]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    auto addBox = [&]( geometry_document_t *pDoc, math::vec3d_t lo, math::vec3d_t hi ) {
        geometry_fragment_t frag{};
        REQUIRE( GeometryFragment_Init( &frag, &allocator ) == geometry_status_t::OK );
        geometry_primitive_t p{};
        p.box.lo = lo;
        p.box.hi = hi;
        geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
        REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, pDoc->policy, &ids, &frag ) == geometry_status_t::OK );
        const geometry_source_id_t id = frag.meshes.pData[0]->sourceId;
        REQUIRE( GeometryFragment_TryInsert( &frag, pDoc, nullptr ) == geometry_status_t::OK );
        GeometryFragment_Shutdown( &frag );
        return id;
    };
    auto volume = [&]( const geometry_document_t &doc, geometry_source_id_t id ) {
        const mesh_source_t *pM = GeometryDocument_FindMesh( &doc, id );
        REQUIRE( pM != nullptr );
        return EditableMesh_SignedVolume( &pM->mesh );
    };
    for ( const bool bKeepB : { false, true } ) {
        CAPTURE( bKeepB );
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
        const geometry_source_id_t a = addBox( &doc, math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ) );
        const geometry_source_id_t b = addBox( &doc, math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ) );
        csg_document_options_t o{};
        o.mesh.op = op_t::DIFFERENCE;
        o.bKeepB = bKeepB;
        geometry_source_id_t result{};
        REQUIRE( GeometryDocument_TryCsgMeshes( &doc, a, b, o, &result, nullptr ) == geometry_status_t::OK );
        CHECK( result.value == a.value );
        CHECK( GeometryDocument_MeshCount( &doc ) == ( bKeepB ? 2u : 1u ) );
        CHECK( std::fabs( volume( doc, a ) - 7.0 ) < 1e-9 );
        CHECK( ( GeometryDocument_FindMesh( &doc, b ) != nullptr ) == bKeepB );
        CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
        GeometryDocument_Shutdown( &doc );
    }
    {
        geometry_document_t doc{};
        REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
        const geometry_source_id_t a = addBox( &doc, math::Vec3d_Make( 0, 0, 0 ), math::Vec3d_Make( 2, 2, 2 ) );
        const geometry_source_id_t b = addBox( &doc, math::Vec3d_Make( 1, 1, 1 ), math::Vec3d_Make( 3, 3, 3 ) );
        geometry_source_id_t in{}, out{};
        REQUIRE( GeometryDocument_TryCsgSplitMesh( &doc, a, b, csg_mesh_options_t{}, &in, &out, nullptr ) == geometry_status_t::OK );
        CHECK( in.value == a.value );
        CHECK( GeometryDocument_MeshCount( &doc ) == 3u );
        CHECK( std::fabs( volume( doc, in ) - 1.0 ) < 1e-9 );
        CHECK( std::fabs( volume( doc, out ) - 7.0 ) < 1e-9 );
        CHECK( std::fabs( volume( doc, b ) - 8.0 ) < 1e-9 );
        CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
        // A failing Boolean (mesh against itself) changes nothing.
        const common::usize cIds = GeometrySourceIdRegistry_Count( &doc.sourceIds );
        CHECK( GeometryDocument_TryCsgMeshes( &doc, in, in, csg_document_options_t{}, nullptr, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
        CHECK( GeometrySourceIdRegistry_Count( &doc.sourceIds ) == cIds );
        CHECK( GeometryDocument_MeshCount( &doc ) == 3u );
        GeometryDocument_Shutdown( &doc );
    }
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

TEST_CASE( "Every allocation failure in a document mesh Boolean leaves the document unchanged", "[geometry][csg][allocation]" )
{
    auto setup = [&]( geometry_document_t *pDoc, geometry_source_id_t *pA, geometry_source_id_t *pB ) {
        for ( int k = 0; k < 2; ++k ) {
            geometry_fragment_t frag{};
            REQUIRE( GeometryFragment_Init( &frag, common::Allocator_GetSystem() ) == geometry_status_t::OK );
            geometry_primitive_t p{};
            p.box.lo = k == 0 ? math::Vec3d_Make( 0, 0, 0 ) : math::Vec3d_Make( 1, 1, 1 );
            p.box.hi = k == 0 ? math::Vec3d_Make( 2, 2, 2 ) : math::Vec3d_Make( 3, 3, 3 );
            geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
            REQUIRE( Primitive_TryBuild( p, geometry_primitive_output_t::MESH, pDoc->policy, &ids, &frag ) == geometry_status_t::OK );
            ( k == 0 ? *pA : *pB ) = frag.meshes.pData[0]->sourceId;
            REQUIRE( GeometryFragment_TryInsert( &frag, pDoc, nullptr ) == geometry_status_t::OK );
            GeometryFragment_Shutdown( &frag );
        }
    };
    csg_document_options_t o{};
    o.mesh.op = op_t::DIFFERENCE;
    common::usize cOperation = 0u;
    {
        fail_state_t probe{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &probe };
        {
            geometry_document_t doc{};
            REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
            geometry_source_id_t a{}, b{};
            setup( &doc, &a, &b );
            const common::usize cStart = probe.cCalls;
            REQUIRE( GeometryDocument_TryCsgMeshes( &doc, a, b, o, nullptr, nullptr ) == geometry_status_t::OK );
            cOperation = probe.cCalls - cStart;
            GeometryDocument_Shutdown( &doc );
        }
        CHECK( probe.cLive == 0u );
    }
    REQUIRE( cOperation > 0u );
    for ( common::usize i = 1; i <= cOperation; ++i ) {
        CAPTURE( i );
        fail_state_t state{};
        common::allocator_t allocator{ FailAllocate, nullptr, FailFree, &state };
        {
            geometry_document_t doc{};
            REQUIRE( GeometryDocument_Init( &doc, &allocator, geometry_policy_t{} ) == geometry_status_t::OK );
            geometry_source_id_t a{}, b{};
            setup( &doc, &a, &b );
            const common::usize cIds = GeometrySourceIdRegistry_Count( &doc.sourceIds );
            state.iFailOnCall = state.cCalls + i;
            const geometry_status_t st = GeometryDocument_TryCsgMeshes( &doc, a, b, o, nullptr, nullptr );
            state.iFailOnCall = common::CY_INVALID_SIZE;
            CHECK( st == geometry_status_t::ALLOCATION_FAILED );
            CHECK( GeometryDocument_MeshCount( &doc ) == 2u );
            CHECK( GeometryDocument_FindMesh( &doc, a ) != nullptr );
            CHECK( GeometryDocument_FindMesh( &doc, b ) != nullptr );
            CHECK( GeometrySourceIdRegistry_Count( &doc.sourceIds ) == cIds );
            CHECK( GeometrySourceIdRegistry_ValidateDeep( &doc.sourceIds ) );
            GeometryDocument_Shutdown( &doc );
        }
        CHECK( state.cLive == 0u );
    }
}

} // namespace cypher::editor::geometry
