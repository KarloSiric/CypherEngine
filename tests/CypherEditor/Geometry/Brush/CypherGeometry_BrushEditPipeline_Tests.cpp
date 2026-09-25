//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushEditPipeline_Tests.cpp
//  Purpose: Verifies the orchestrated brush edit pipeline workflow.
//  Details: Covers Gate 5 pipeline acceptance:
//           - preflight → preview → validate → commit produces a delta
//           - no-op edits create no revision
//           - cancel restores exact authored state
//           - undo via inverse delta restores exact state
//           - spatial index is refitted on commit
//           - boundary is valid after validate
//           - multi-side edits produce BRUSH_REPLACED delta
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushEditPipeline.h"
#include "CypherGeometry_BrushTransform.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::Planed_Make;
using Catch::Approx;

namespace {

struct PipelineFixture {
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    geometry_policy_t policy{};
    geometry_source_id_allocator_t idAlloc{};
    geometry_document_t document{};
    geometry_spatial_index_t spatial{};
    brush_edit_pipeline_t pipeline{};
    brush_solid_t brush{};
    geometry_source_id_t brushId{};

    PipelineFixture()
    {
        REQUIRE( GeometryDocument_Init(
                     &document, &allocator, policy ) ==
                 geometry_status_t::OK );
        REQUIRE( GeometrySpatialIndex_Init(
                     &spatial, &allocator ) ==
                 geometry_status_t::OK );
        REQUIRE( BrushEditPipeline_Init(
                     &pipeline, &allocator ) ==
                 geometry_status_t::OK );

        // Create a box and add it to the document.
        REQUIRE( BrushGenerator_TryMakeBox(
                     &brush, &allocator, policy, &idAlloc,
                     Vec3d_Make( 0.0, 0.0, 0.0 ),
                     Vec3d_Make( 1.0, 1.0, 1.0 ) ) ==
                 geometry_status_t::OK );

        brushId = brush.sourceId;
        REQUIRE( GeometryDocument_TryAddBrush( &document, &brush ) ==
                 geometry_status_t::OK );

        // Insert into spatial index with initial bounds.
        const math::aabbd_t initialBounds = math::Aabbd_Make(
            Vec3d_Make( -1.0, -1.0, -1.0 ),
            Vec3d_Make( 1.0, 1.0, 1.0 ) );
        REQUIRE( GeometrySpatialIndex_TryInsert(
                     &spatial, brushId, initialBounds ) ==
                 geometry_status_t::OK );
    }

    ~PipelineFixture()
    {
        BrushSolid_Shutdown( &brush );
        BrushEditPipeline_Shutdown( &pipeline );
        GeometrySpatialIndex_Shutdown( &spatial );
        GeometryDocument_Shutdown( &document );
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Full pipeline workflow
// ---------------------------------------------------------------------------

TEST_CASE( "Pipeline: translate commit produces BRUSH_REPLACED delta",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Apply translation via the transaction's all-planes preview.
    // First transform the live brush directly, then preview all planes.
    brush_solid_t *pLive =
        GeometryDocument_FindBrushMutable( &f.document, f.brushId );
    REQUIRE( pLive != nullptr );

    REQUIRE( BrushTransform_TryTranslate(
                 pLive, Vec3d_Make( 5.0, 0.0, 0.0 ) ) ==
             geometry_status_t::OK );

    // Validate boundary.
    REQUIRE( BrushEditPipeline_Validate( &f.pipeline ) ==
             geometry_status_t::OK );
    REQUIRE( BrushEditPipeline_GetBoundary( &f.pipeline ) != nullptr );

    // Commit with spatial refit.
    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &f.pipeline, &f.spatial, &result ) ==
             geometry_status_t::OK );

    // All 6 planes changed → BRUSH_REPLACED delta.
    REQUIRE( result.delta.kind ==
             geometry_delta_kind_t::BRUSH_REPLACED );
    REQUIRE( result.delta.brushId.value == f.brushId.value );
    REQUIRE( result.newRevision == 1u );
    REQUIRE( result.bSpatialRefitted );

    // Verify spatial index was updated: bounds should now reflect
    // the translated box.
    math::aabbd_t newBounds{};
    REQUIRE( GeometrySpatialIndex_TryGetBounds(
                 &f.spatial, f.brushId, &newBounds ) ==
             geometry_status_t::OK );
    REQUIRE( newBounds.minimum.x == Approx( 4.0 ).margin( 0.1 ) );
    REQUIRE( newBounds.maximum.x == Approx( 6.0 ).margin( 0.1 ) );

    GeometryDelta_Shutdown( &result.delta );
    GeometryChangeset_Shutdown( &result.changeset );
}

TEST_CASE( "Pipeline: no-op edit creates no revision",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;
    const geometry_revision_t revBefore =
        GeometryDocument_GetRevision( &f.document );

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Don't change anything — just validate and commit.
    REQUIRE( BrushEditPipeline_Validate( &f.pipeline ) ==
             geometry_status_t::OK );

    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &f.pipeline, &f.spatial, &result ) ==
             geometry_status_t::OK );

    // Revision should not have advanced.
    REQUIRE( result.newRevision == revBefore );
    REQUIRE( result.delta.kind == geometry_delta_kind_t::INVALID );

    GeometryDelta_Shutdown( &result.delta );
    GeometryChangeset_Shutdown( &result.changeset );
}

TEST_CASE( "Pipeline: cancel restores exact authored state",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;

    // Record original side planes.
    const brush_solid_t *pOriginal =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( pOriginal != nullptr );
    const common::usize cSides = BrushSolid_SideCount( pOriginal );
    math::planed_t origPlanes[6];
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( pOriginal, i, &side );
        origPlanes[i] = side.plane;
    }

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Apply a big translation.
    brush_solid_t *pLive =
        GeometryDocument_FindBrushMutable( &f.document, f.brushId );
    REQUIRE( BrushTransform_TryTranslate(
                 pLive, Vec3d_Make( 100.0, 200.0, 300.0 ) ) ==
             geometry_status_t::OK );

    // Cancel — should restore exact original planes.
    REQUIRE( BrushEditPipeline_Cancel( &f.pipeline ) ==
             geometry_status_t::OK );

    const brush_solid_t *pRestored =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( pRestored != nullptr );
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( pRestored, i, &side );
        REQUIRE( side.plane.normal.x == origPlanes[i].normal.x );
        REQUIRE( side.plane.normal.y == origPlanes[i].normal.y );
        REQUIRE( side.plane.normal.z == origPlanes[i].normal.z );
        REQUIRE( side.plane.d == origPlanes[i].d );
    }
}

TEST_CASE( "Pipeline: undo via inverse delta restores exact state",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;

    // Record original planes.
    const brush_solid_t *pOriginal =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    const common::usize cSides = BrushSolid_SideCount( pOriginal );
    math::planed_t origPlanes[6];
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( pOriginal, i, &side );
        origPlanes[i] = side.plane;
    }

    // Perform a translate edit.
    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    brush_solid_t *pLive =
        GeometryDocument_FindBrushMutable( &f.document, f.brushId );
    REQUIRE( BrushTransform_TryTranslate(
                 pLive, Vec3d_Make( 10.0, 20.0, 30.0 ) ) ==
             geometry_status_t::OK );

    REQUIRE( BrushEditPipeline_Validate( &f.pipeline ) ==
             geometry_status_t::OK );

    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &f.pipeline, nullptr, &result ) ==
             geometry_status_t::OK );

    REQUIRE( result.delta.kind ==
             geometry_delta_kind_t::BRUSH_REPLACED );

    // Compute the inverse delta.
    geometry_delta_t inverse{};
    REQUIRE( GeometryDelta_TryComputeInverse(
                 &result.delta, &f.allocator,
                 f.policy.limits, &inverse ) ==
             geometry_status_t::OK );

    // Apply the inverse delta to undo.
    REQUIRE( GeometryDelta_TryApplyToDocument(
                 &inverse, &f.document ) ==
             geometry_status_t::OK );

    // Verify exact restoration.
    const brush_solid_t *pRestored =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( pRestored != nullptr );
    for ( common::usize i = 0u; i < cSides; ++i ) {
        brush_solid_side_t side{};
        (void)BrushSolid_TryGetSide( pRestored, i, &side );
        REQUIRE( side.plane.normal.x == origPlanes[i].normal.x );
        REQUIRE( side.plane.normal.y == origPlanes[i].normal.y );
        REQUIRE( side.plane.normal.z == origPlanes[i].normal.z );
        REQUIRE( side.plane.d == origPlanes[i].d );
    }

    GeometryDelta_Shutdown( &inverse );
    GeometryDelta_Shutdown( &result.delta );
    GeometryChangeset_Shutdown( &result.changeset );
}

TEST_CASE( "Pipeline: single-side edit produces SIDE_PLANE_CHANGED delta",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    // Change just one side plane via the transaction.
    geometry_transaction_t *pTx =
        BrushEditPipeline_GetTransaction( &f.pipeline );
    REQUIRE( pTx != nullptr );

    brush_solid_side_t side0{};
    const brush_solid_t *pBrush =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    (void)BrushSolid_TryGetSide( pBrush, 0u, &side0 );

    REQUIRE( GeometryTransaction_TryPreviewSidePlane(
                 pTx, 0u,
                 Planed_Make( side0.plane.normal,
                              side0.plane.d + 0.5 ) ) ==
             geometry_status_t::OK );

    REQUIRE( BrushEditPipeline_Validate( &f.pipeline ) ==
             geometry_status_t::OK );

    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &f.pipeline, nullptr, &result ) ==
             geometry_status_t::OK );

    // Only one side changed → lightweight delta.
    REQUIRE( result.delta.kind ==
             geometry_delta_kind_t::BRUSH_SIDE_PLANE_CHANGED );
    REQUIRE( result.delta.sideIndex == 0u );
    REQUIRE( result.newRevision == 1u );

    GeometryDelta_Shutdown( &result.delta );
    GeometryChangeset_Shutdown( &result.changeset );
}

// ---------------------------------------------------------------------------
// Error paths
// ---------------------------------------------------------------------------

TEST_CASE( "Pipeline: double init rejected", "[Gate5][Pipeline]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_edit_pipeline_t pipeline{};
    REQUIRE( BrushEditPipeline_Init( &pipeline, &allocator ) ==
             geometry_status_t::OK );
    REQUIRE( BrushEditPipeline_Init( &pipeline, &allocator ) ==
             geometry_status_t::ALREADY_INITIALIZED );
    BrushEditPipeline_Shutdown( &pipeline );
}

TEST_CASE( "Pipeline: commit without begin fails", "[Gate5][Pipeline]" )
{
    common::allocator_t allocator{ *common::Allocator_GetSystem() };
    brush_edit_pipeline_t pipeline{};
    REQUIRE( BrushEditPipeline_Init( &pipeline, &allocator ) ==
             geometry_status_t::OK );

    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &pipeline, nullptr, &result ) ==
             geometry_status_t::NO_ACTIVE_TRANSACTION );

    BrushEditPipeline_Shutdown( &pipeline );
}

TEST_CASE( "Pipeline: commit requires successful validation",
           "[Gate5][Pipeline][Contract]" )
{
    PipelineFixture f;
    const geometry_revision_t revisionBefore =
        GeometryDocument_GetRevision( &f.document );

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );

    brush_solid_t *pLive =
        GeometryDocument_FindBrushMutable( &f.document, f.brushId );
    REQUIRE( pLive != nullptr );
    REQUIRE( BrushTransform_TryTranslate(
                 pLive, Vec3d_Make( 3.0, 0.0, 0.0 ) ) ==
             geometry_status_t::OK );

    brush_edit_result_t result{};
    REQUIRE( BrushEditPipeline_Commit(
                 &f.pipeline, nullptr, &result ) ==
             geometry_status_t::INVALID_TOPOLOGY );

    // A failed publication gate must preserve both the transaction and the
    // document revision so the caller can validate or cancel safely.
    REQUIRE( BrushEditPipeline_IsActive( &f.pipeline ) );
    REQUIRE( GeometryDocument_GetRevision( &f.document ) ==
             revisionBefore );
    REQUIRE( result.delta.kind == geometry_delta_kind_t::INVALID );

    REQUIRE( BrushEditPipeline_Cancel( &f.pipeline ) ==
             geometry_status_t::OK );
    REQUIRE_FALSE( BrushEditPipeline_IsActive( &f.pipeline ) );

    const brush_solid_t *pRestored =
        GeometryDocument_FindBrush( &f.document, f.brushId );
    REQUIRE( pRestored != nullptr );
    brush_solid_side_t side{};
    REQUIRE( BrushSolid_TryGetSide( pRestored, 0u, &side ) ==
             geometry_status_t::OK );
    REQUIRE( side.plane.d == Approx( -1.0 ) );
}

TEST_CASE( "Pipeline: shutdown cancels active transaction",
           "[Gate5][Pipeline]" )
{
    PipelineFixture f;

    REQUIRE( BrushEditPipeline_Begin(
                 &f.pipeline, &f.document, f.brushId ) ==
             geometry_status_t::OK );
    REQUIRE( BrushEditPipeline_IsActive( &f.pipeline ) );

    // Shutdown should cancel without crashing.
    BrushEditPipeline_Shutdown( &f.pipeline );
    REQUIRE_FALSE( BrushEditPipeline_IsActive( &f.pipeline ) );

    // Reinit for fixture cleanup.
    REQUIRE( BrushEditPipeline_Init(
                 &f.pipeline, &f.allocator ) ==
             geometry_status_t::OK );
}

TEST_CASE( "Pipeline: null args rejected", "[Gate5][Pipeline]" )
{
    REQUIRE( BrushEditPipeline_Init( nullptr, nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );
}

} // namespace cypher::editor::geometry
