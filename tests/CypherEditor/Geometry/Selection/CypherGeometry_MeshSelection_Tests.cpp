//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshSelection_Tests.cpp
//  Purpose: Contract tests for mesh component selection, topological
//           selection tools, and selection remap across edits.
//  Details: Selection gate: face, edge, and vertex selections stay stable
//           across split and merge edits, and every non-trivial outcome
//           (SPLIT, MERGED, LOST) is reported explicitly. The fixture is a
//           mesh holding two disjoint unit cubes (IDs 10-17 / 20-25 and
//           110-117 / 120-125) so connectivity tools have something to stop
//           at.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshSelection.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace cypher::editor::geometry {

using math::Vec3d_Make;

namespace {

geometry_source_id_t Id( common::u64 v ) { return geometry_source_id_t{ v }; }

void AddCube( mesh_source_description_t *pD, common::u64 base, double offset ) {
    const common::u32 v0 = static_cast<common::u32>( pD->vertices.nCount );
    for ( int i = 0; i < 8; ++i ) {
        REQUIRE( MeshSourceDescription_TryAddVertex(
                     pD, Vec3d_Make( ( ( i & 1 ) ? 1.0 : 0.0 ) + offset, ( i & 2 ) ? 1.0 : 0.0, ( i & 4 ) ? 1.0 : 0.0 ),
                     Id( base + static_cast<common::u64>( i ) ), nullptr ) == geometry_status_t::OK );
    }
    const std::vector<std::vector<common::u32>> faces = {
        { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
    for ( common::usize f = 0; f < faces.size(); ++f ) {
        std::vector<common::u32> idx;
        for ( const common::u32 k : faces[f] ) { idx.push_back( v0 + k ); }
        REQUIRE( MeshSourceDescription_TryAddFace( pD, common::span_t<const common::u32>{ idx.data(), idx.size() },
                                                   Id( base + 10u + f ), mesh_face_attributes_t{}, nullptr ) ==
                 geometry_status_t::OK );
    }
}

struct Env {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_source_t mesh{};
    mesh_selection_t sel{};
    geometry_source_id_allocator_t ids{ Id( 900 ) };
    explicit Env( bool bTwoCubes = true ) {
        mesh_source_description_t d{};
        REQUIRE( MeshSourceDescription_Init( &d, &allocator, Id( 1 ) ) == geometry_status_t::OK );
        AddCube( &d, 10, 0.0 );
        if ( bTwoCubes ) { AddCube( &d, 110, 5.0 ); }
        REQUIRE( MeshSource_TryBuild( &d, &allocator, &mesh ) == geometry_status_t::OK );
        MeshSourceDescription_Shutdown( &d );
        REQUIRE( MeshSelection_Init( &sel, &allocator, Id( 1 ) ) == geometry_status_t::OK );
    }
    ~Env() {
        MeshSelection_Shutdown( &sel );
        MeshSource_Shutdown( &mesh );
    }
    std::vector<common::u64> Faces() const {
        std::vector<common::u64> r;
        for ( common::usize i = 0; i < sel.faces.nCount; ++i ) { r.push_back( sel.faces.pData[i].value ); }
        return r;
    }
    std::vector<common::u64> Vertices() const {
        std::vector<common::u64> r;
        for ( common::usize i = 0; i < sel.vertices.nCount; ++i ) { r.push_back( sel.vertices.pData[i].value ); }
        return r;
    }
};

// Runs an edit with provenance, assigns IDs, and converts to lineage.
struct Lineage {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_edit_provenance_t prov{};
    mesh_edit_lineage_t lineage{};
    mesh_edit_report_t report{};
    Lineage() {
        REQUIRE( MeshEditProvenance_Init( &prov, &allocator ) == geometry_status_t::OK );
        REQUIRE( MeshEditLineage_Init( &lineage, &allocator ) == geometry_status_t::OK );
        report.pProvenance = &prov;
    }
    ~Lineage() {
        MeshEditLineage_Shutdown( &lineage );
        MeshEditProvenance_Shutdown( &prov );
    }
    void Finish( Env &e ) {
        REQUIRE( MeshSource_TryAssignMissingIds( &e.mesh, &e.ids, nullptr ) == geometry_status_t::OK );
        REQUIRE( MeshEditProvenance_TryToLineage( &prov, &e.mesh, &lineage ) == geometry_status_t::OK );
    }
};

struct Report {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    mesh_selection_remap_report_t r{};
    Report() { REQUIRE( MeshSelectionRemapReport_Init( &r, &allocator ) == geometry_status_t::OK ); }
    ~Report() { MeshSelectionRemapReport_Shutdown( &r ); }
};

} // namespace

TEST_CASE( "Selection sets stay sorted, unique, and normalized", "[geometry][selection]" ) {
    Env e;
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 23 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 21 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 23 ) ) == geometry_status_t::OK );
    CHECK( e.Faces() == std::vector<common::u64>{ 21, 23 } );
    REQUIRE( MeshSelection_TryAddEdge( &e.sel, mesh_edge_ref_t{ Id( 11 ), Id( 10 ) } ) == geometry_status_t::OK );
    CHECK( MeshSelection_HasEdge( &e.sel, mesh_edge_ref_t{ Id( 10 ), Id( 11 ) } ) );
    CHECK( e.sel.edges.pData[0].a.value == 10u );
    CHECK( MeshSelection_TryAddEdge( &e.sel, mesh_edge_ref_t{ Id( 10 ), Id( 10 ) } ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSelection_TryAddVertex( &e.sel, GEOMETRY_SOURCE_ID_INVALID ) == geometry_status_t::INVALID_ARGUMENT );
    CHECK( MeshSelection_RemoveFace( &e.sel, Id( 21 ) ) );
    CHECK_FALSE( MeshSelection_RemoveFace( &e.sel, Id( 21 ) ) );
    CHECK( MeshSelection_HasFace( &e.sel, Id( 23 ) ) );
    MeshSelection_Clear( &e.sel );
    CHECK( e.sel.faces.nCount == 0u );
    CHECK( e.sel.edges.nCount == 0u );
}

TEST_CASE( "Grow and shrink move one ring at a time", "[geometry][selection]" ) {
    Env e;
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 21 ) ) == geometry_status_t::OK ); // top of cube A
    REQUIRE( MeshSelection_TryGrow( &e.sel, &e.mesh, mesh_selection_mode_t::FACE ) == geometry_status_t::OK );
    CHECK( e.Faces() == std::vector<common::u64>{ 21, 22, 23, 24, 25 } );
    REQUIRE( MeshSelection_TryShrink( &e.sel, &e.mesh, mesh_selection_mode_t::FACE ) == geometry_status_t::OK );
    CHECK( e.Faces() == std::vector<common::u64>{ 21 } );

    REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( 10 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryGrow( &e.sel, &e.mesh, mesh_selection_mode_t::VERTEX ) == geometry_status_t::OK );
    CHECK( e.Vertices() == std::vector<common::u64>{ 10, 11, 12, 14 } );
    REQUIRE( MeshSelection_TryShrink( &e.sel, &e.mesh, mesh_selection_mode_t::VERTEX ) == geometry_status_t::OK );
    CHECK( e.Vertices() == std::vector<common::u64>{ 10 } );

    REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), Id( 11 ) ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryGrow( &e.sel, &e.mesh, mesh_selection_mode_t::EDGE ) == geometry_status_t::OK );
    // Edges touching 10 or 11: 3 + 3 - the shared one = 5.
    CHECK( e.sel.edges.nCount == 5u );
}

TEST_CASE( "Select connected stops at shell boundaries", "[geometry][selection]" ) {
    Env e;
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 122 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TrySelectConnected( &e.sel, &e.mesh, mesh_selection_mode_t::FACE ) == geometry_status_t::OK );
    CHECK( e.Faces() == std::vector<common::u64>{ 120, 121, 122, 123, 124, 125 } );
    REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( 13 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TrySelectConnected( &e.sel, &e.mesh, mesh_selection_mode_t::VERTEX ) == geometry_status_t::OK );
    CHECK( e.Vertices() == std::vector<common::u64>{ 10, 11, 12, 13, 14, 15, 16, 17 } );
    REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 110 ), Id( 111 ) ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TrySelectConnected( &e.sel, &e.mesh, mesh_selection_mode_t::EDGE ) == geometry_status_t::OK );
    CHECK( e.sel.edges.nCount == 12u );
}

TEST_CASE( "Mode conversion maps between component kinds", "[geometry][selection]" ) {
    Env e;
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 21 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::FACE, mesh_selection_mode_t::VERTEX ) ==
             geometry_status_t::OK );
    CHECK( e.Vertices() == std::vector<common::u64>{ 14, 15, 16, 17 } );
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::FACE, mesh_selection_mode_t::EDGE ) ==
             geometry_status_t::OK );
    CHECK( e.sel.edges.nCount == 4u );
    MeshSelection_Clear( &e.sel );
    for ( const common::u64 v : { 14u, 15u, 16u, 17u } ) {
        REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( v ) ) == geometry_status_t::OK );
    }
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::VERTEX, mesh_selection_mode_t::FACE ) ==
             geometry_status_t::OK );
    CHECK( e.Faces() == std::vector<common::u64>{ 21 } ); // only the face fully covered
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::VERTEX, mesh_selection_mode_t::EDGE ) ==
             geometry_status_t::OK );
    CHECK( e.sel.edges.nCount == 4u );
    MeshSelection_Clear( &e.sel );
    REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 14 ), Id( 15 ) ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::EDGE, mesh_selection_mode_t::VERTEX ) ==
             geometry_status_t::OK );
    CHECK( e.Vertices() == std::vector<common::u64>{ 14, 15 } );
    REQUIRE( MeshSelection_TryConvert( &e.sel, &e.mesh, mesh_selection_mode_t::EDGE, mesh_selection_mode_t::FACE ) ==
             geometry_status_t::OK );
    CHECK( e.sel.faces.nCount == 0u ); // one edge covers no face
}

TEST_CASE( "Edge ring walks all the way around a quad strip", "[geometry][selection]" ) {
    Env e( false );
    REQUIRE( MeshSelection_TrySelectEdgeRing( &e.sel, &e.mesh, MeshEdgeRef_Make( Id( 10 ), Id( 11 ) ) ) ==
             geometry_status_t::OK );
    CHECK( e.sel.edges.nCount == 4u ); // the four x-parallel edges
    for ( common::usize i = 0; i < e.sel.edges.nCount; ++i ) {
        const common::u64 a = e.sel.edges.pData[i].a.value, b = e.sel.edges.pData[i].b.value;
        CHECK( b - a == 1u ); // IDs differ in bit 0 = x
    }
}

TEST_CASE( "Edge ring rejects non-edges; prune drops stale references", "[geometry][selection]" ) {
    Env e( false );
    CHECK( MeshSelection_TrySelectEdgeRing( &e.sel, &e.mesh, MeshEdgeRef_Make( Id( 10 ), Id( 17 ) ) ) ==
           geometry_status_t::INVALID_HANDLE );
    REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), Id( 11 ) ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 555 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( 10 ) ) == geometry_status_t::OK );
    REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), Id( 17 ) ) ) == geometry_status_t::OK );
    common::u32 cRemoved = 0;
    REQUIRE( MeshSelection_TryPrune( &e.sel, &e.mesh, &cRemoved ) == geometry_status_t::OK );
    CHECK( cRemoved == 2u );
    CHECK( MeshSelection_HasVertex( &e.sel, Id( 10 ) ) );
    CHECK( e.sel.edges.nCount == 1u );

    mesh_selection_t other{};
    REQUIRE( MeshSelection_Init( &other, &e.allocator, Id( 77 ) ) == geometry_status_t::OK );
    CHECK( MeshSelection_TryGrow( &other, &e.mesh, mesh_selection_mode_t::FACE ) == geometry_status_t::INVALID_ARGUMENT );
    MeshSelection_Shutdown( &other );
}

TEST_CASE( "Selection gate: remap across split and merge with explicit reporting", "[geometry][selection]" ) {
    SECTION( "loop cut splits a selected face" ) {
        Env e( false );
        REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 21 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 24 ) ) == geometry_status_t::OK ); // not cut
        Lineage l;
        common::u32 cSplit = 0;
        REQUIRE( MeshSourceEdit_TryLoopCut( &e.mesh, Id( 10 ), Id( 11 ), 0.5, &cSplit, &l.report ) == geometry_status_t::OK );
        l.Finish( e );
        Report rep;
        REQUIRE( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, &rep.r ) == geometry_status_t::OK );
        CHECK( e.sel.faces.nCount == 3u ); // both halves of 21, plus 24
        CHECK( MeshSelection_HasFace( &e.sel, Id( 21 ) ) );
        CHECK( MeshSelection_HasFace( &e.sel, Id( 24 ) ) );
        CHECK( rep.r.cSplit == 1u );
        CHECK( rep.r.cKept == 1u );
        CHECK( MeshSelectionRemapReport_IsAmbiguous( &rep.r ) );
        REQUIRE( rep.r.entries.nCount == 1u );
        CHECK( rep.r.entries.pData[0].outcome == mesh_selection_outcome_t::SPLIT );
        CHECK( rep.r.entries.pData[0].id.value == 21u );
    }
    SECTION( "split edge replaces a selected edge by its halves" ) {
        Env e( false );
        REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), Id( 11 ) ) ) == geometry_status_t::OK );
        Lineage l;
        geometry_mesh_vertex_handle_t hNew{};
        REQUIRE( MeshSourceEdit_TrySplitEdge( &e.mesh, Id( 10 ), Id( 11 ), 0.5, &hNew, &l.report ) == geometry_status_t::OK );
        l.Finish( e );
        const geometry_source_id_t mid = MeshSource_VertexId( &e.mesh, hNew );
        Report rep;
        REQUIRE( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, &rep.r ) == geometry_status_t::OK );
        CHECK( MeshSelection_HasEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), mid ) ) );
        CHECK( MeshSelection_HasEdge( &e.sel, MeshEdgeRef_Make( mid, Id( 11 ) ) ) );
        CHECK( e.sel.edges.nCount == 2u );
        CHECK( rep.r.cSplit == 1u );
    }
    SECTION( "collapse merges a selected vertex into the survivor" ) {
        Env e( false );
        REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( 10 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSelection_TryAddVertex( &e.sel, Id( 17 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSelection_TryAddEdge( &e.sel, MeshEdgeRef_Make( Id( 10 ), Id( 12 ) ) ) == geometry_status_t::OK );
        Lineage l;
        REQUIRE( MeshSourceEdit_TryCollapseEdge( &e.mesh, Id( 11 ), Id( 10 ), &l.report ) == geometry_status_t::OK );
        l.Finish( e );
        REQUIRE( l.lineage.merges.nCount == 1u );
        Report rep;
        REQUIRE( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, &rep.r ) == geometry_status_t::OK );
        CHECK( e.Vertices() == std::vector<common::u64>{ 11, 17 } );
        // Edge (10, 12) became (11, 12) through the merge.
        CHECK( MeshSelection_HasEdge( &e.sel, MeshEdgeRef_Make( Id( 11 ), Id( 12 ) ) ) );
        CHECK( rep.r.cMerged == 2u );
        CHECK( rep.r.cKept == 1u );
    }
    SECTION( "dissolve loses the absorbed face and says so" ) {
        Env e( false );
        REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 20 ) ) == geometry_status_t::OK );
        REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 22 ) ) == geometry_status_t::OK );
        Lineage l;
        REQUIRE( MeshSourceEdit_TryDissolveEdge( &e.mesh, Id( 10 ), Id( 11 ), &l.report ) == geometry_status_t::OK );
        l.Finish( e );
        Report rep;
        REQUIRE( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, &rep.r ) == geometry_status_t::OK );
        CHECK( e.sel.faces.nCount == 1u );
        CHECK( rep.r.cKept == 1u );
        CHECK( rep.r.cLost == 1u );
        REQUIRE( rep.r.entries.nCount == 1u );
        CHECK( rep.r.entries.pData[0].outcome == mesh_selection_outcome_t::LOST );
    }
    SECTION( "extrude keeps the cap selected without ambiguity" ) {
        Env e( false );
        REQUIRE( MeshSelection_TryAddFace( &e.sel, Id( 21 ) ) == geometry_status_t::OK );
        Lineage l;
        REQUIRE( MeshSourceEdit_TryExtrudeFace( &e.mesh, Id( 21 ), 1.0, &l.report ) == geometry_status_t::OK );
        l.Finish( e );
        Report rep;
        REQUIRE( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, &rep.r ) == geometry_status_t::OK );
        CHECK( e.Faces() == std::vector<common::u64>{ 21 } );
        CHECK_FALSE( MeshSelectionRemapReport_IsAmbiguous( &rep.r ) );
    }
}

TEST_CASE( "Lineage conversion requires identified elements", "[geometry][selection]" ) {
    Env e( false );
    Lineage l;
    REQUIRE( MeshSourceEdit_TrySplitFace( &e.mesh, Id( 21 ), Id( 14 ), Id( 17 ), &l.report ) == geometry_status_t::OK );
    // New face has no ID yet.
    CHECK( MeshEditProvenance_TryToLineage( &l.prov, &e.mesh, &l.lineage ) == geometry_status_t::INVALID_ARGUMENT );
    // Remap on an unidentified mesh is refused rather than guessed.
    CHECK( MeshSelection_TryRemap( &e.sel, &e.mesh, &l.lineage, nullptr ) == geometry_status_t::INVALID_ARGUMENT );
    l.Finish( e );
    CHECK( l.lineage.faces.nCount == 1u );
    CHECK( l.lineage.faces.pData[0].parentId.value == 21u );
}

} // namespace cypher::editor::geometry
