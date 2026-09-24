//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_TestSupport.h
//  Purpose: Shared fixtures for editor-geometry contract tests.
//  Details: Header-only helpers: an allocator that fails on a chosen call,
//           canonical plane sets, RAII holders for brushes, boundaries,
//           documents, values, and snapshots, and a builder that turns a
//           plane list into a brush with stable side identities.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TEST_SUPPORT_H
#define CYPHER_EDITOR_GEOMETRY_TEST_SUPPORT_H

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_Document.h"
#include "CypherGeometry_Snapshot.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace cypher::editor::geometry::test {

using cypher::math::f64;
using cypher::math::planed_t;
using cypher::math::vec3d_t;

// ---------------------------------------------------------------------------
// Allocation failure injection
// ---------------------------------------------------------------------------

struct failing_allocator_state_t {
    common::usize cAllocationCalls{ 0u };
    common::usize iFailure{ common::CY_USIZE_MAX };
};

inline void *FailingAllocate(
    void *pUserData, common::usize cbSize, common::usize nAlignment ) noexcept
{
    auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
    const common::usize iAllocation = pState->cAllocationCalls++;
    if ( iAllocation == pState->iFailure ) {
        return nullptr;
    }
    return common::Allocator_Allocate( common::Allocator_GetSystem(), cbSize, nAlignment );
}

inline void FailingFree(
    void *, void *pMemory, common::usize cbSize, common::usize nAlignment ) noexcept
{
    common::Allocator_Free( common::Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

inline common::allocator_t MakeFailingAllocator( failing_allocator_state_t *pState ) noexcept
{
    return common::allocator_t{ FailingAllocate, nullptr, FailingFree, pState };
}

// ---------------------------------------------------------------------------
// Plane fixtures
// ---------------------------------------------------------------------------

// n·p + d = 0 with n normalized; `offset` is the plane's signed distance
// from the origin along n.
inline planed_t UnitPlane( f64 nx, f64 ny, f64 nz, f64 offset ) noexcept
{
    const f64 length = std::sqrt( nx * nx + ny * ny + nz * nz );
    return cypher::math::Planed_Make(
        cypher::math::Vec3d_Make( nx / length, ny / length, nz / length ), -offset );
}

// Axis-aligned box [min, max] in generator side order (+X -X +Y -Y +Z -Z).
inline std::vector<planed_t> AabbPlanes( vec3d_t minimum, vec3d_t maximum )
{
    return {
        UnitPlane( 1.0, 0.0, 0.0, maximum.x ), UnitPlane( -1.0, 0.0, 0.0, -minimum.x ),
        UnitPlane( 0.0, 1.0, 0.0, maximum.y ), UnitPlane( 0.0, -1.0, 0.0, -minimum.y ),
        UnitPlane( 0.0, 0.0, 1.0, maximum.z ), UnitPlane( 0.0, 0.0, -1.0, -minimum.z ),
    };
}

inline std::vector<planed_t> UnitBoxPlanes()
{
    return AabbPlanes( cypher::math::Vec3d_Make( -1.0, -1.0, -1.0 ),
                       cypher::math::Vec3d_Make( 1.0, 1.0, 1.0 ) );
}

// ---------------------------------------------------------------------------
// RAII holders
// ---------------------------------------------------------------------------

struct brush_holder_t {
    brush_solid_t brush{};
    brush_holder_t() = default;
    brush_holder_t( const brush_holder_t & ) = delete;
    brush_holder_t &operator=( const brush_holder_t & ) = delete;
    ~brush_holder_t() { BrushSolid_Shutdown( &brush ); }
};

struct boundary_holder_t {
    brush_boundary_t boundary{};
    boundary_holder_t() = default;
    boundary_holder_t( const boundary_holder_t & ) = delete;
    boundary_holder_t &operator=( const boundary_holder_t & ) = delete;
    ~boundary_holder_t() { BrushBoundary_Shutdown( &boundary ); }
};

struct value_ref_t {
    const geometry_brush_value_t *p{ nullptr };
    value_ref_t() = default;
    value_ref_t( const value_ref_t & ) = delete;
    value_ref_t &operator=( const value_ref_t & ) = delete;
    value_ref_t( value_ref_t &&other ) noexcept : p( std::exchange( other.p, nullptr ) ) {}
    value_ref_t &operator=( value_ref_t &&other ) noexcept
    {
        if ( this != &other ) {
            BrushValue_Release( p );
            p = std::exchange( other.p, nullptr );
        }
        return *this;
    }
    ~value_ref_t() { BrushValue_Release( p ); }
};

struct snapshot_ref_t {
    const geometry_document_snapshot_t *p{ nullptr };
    snapshot_ref_t() = default;
    snapshot_ref_t( const snapshot_ref_t & ) = delete;
    snapshot_ref_t &operator=( const snapshot_ref_t & ) = delete;
    ~snapshot_ref_t() { GeometrySnapshot_Release( p ); }
};

// Uses the process-lifetime system allocator so values and snapshots may
// outlive the holder.
struct document_holder_t {
    geometry_document_t document{};
    explicit document_holder_t( const geometry_policy_t &policy = {} )
    {
        geometry_document_desc_t desc{};
        desc.pAllocator = common::Allocator_GetSystem();
        desc.policy = policy;
        REQUIRE( GeometryDocument_Init( &document, desc ) == geometry_status_t::OK );
    }
    ~document_holder_t() { GeometryDocument_Shutdown( &document ); }
};

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

// Builds a brush from planes; side i gets source ID firstSideId + i and
// attribute index i. The brush ID is brushId.
inline void BuildBrush(
    brush_solid_t *pBrush,
    const std::vector<planed_t> &planes,
    geometry_source_id_t brushId = geometry_source_id_t{ 1u },
    common::u64 firstSideId = 100u,
    const geometry_policy_t &policy = {} )
{
    REQUIRE( BrushSolid_Init( pBrush, common::Allocator_GetSystem(), brushId ) ==
             geometry_status_t::OK );
    for ( common::usize i = 0u; i < planes.size(); ++i ) {
        brush_solid_side_t side{};
        side.plane = planes[i];
        side.sourceId = geometry_source_id_t{ firstSideId + i };
        side.iAttributeIndex = static_cast<common::u32>( i );
        REQUIRE( BrushSolid_TryAddSide( pBrush, policy.limits, side, nullptr ) ==
                 geometry_status_t::OK );
    }
}

inline void Reconstruct(
    brush_boundary_t *pBoundary, const brush_solid_t *pBrush, const geometry_policy_t &policy = {} )
{
    if ( pBoundary->vertices.pAllocator == nullptr ) {
        REQUIRE( BrushBoundary_Init( pBoundary, common::Allocator_GetSystem() ) ==
                 geometry_status_t::OK );
    }
    REQUIRE( BrushBoundary_TryReconstruct( pBoundary, pBrush, policy ) == geometry_status_t::OK );
}

// Creates a document value for the given planes with document-issued IDs
// and default side attributes.
inline void MakeDocumentValue(
    geometry_document_t *pDocument, const std::vector<planed_t> &planes, value_ref_t *pOut )
{
    std::vector<geometry_source_id_t> ids( planes.size() + 1u );
    REQUIRE( GeometryDocument_TryAllocateSourceIds(
                 pDocument, common::span_t<geometry_source_id_t>{ ids.data(), ids.size() } ) ==
             geometry_status_t::OK );
    brush_holder_t holder{};
    REQUIRE( BrushSolid_Init( &holder.brush, common::Allocator_GetSystem(), ids[0] ) ==
             geometry_status_t::OK );
    for ( common::usize i = 0u; i < planes.size(); ++i ) {
        brush_solid_side_t side{};
        side.plane = planes[i];
        side.sourceId = ids[i + 1u];
        side.iAttributeIndex = static_cast<common::u32>( i );
        REQUIRE( BrushSolid_TryAddSide( &holder.brush, pDocument->policy.limits, side, nullptr ) ==
                 geometry_status_t::OK );
    }
    geometry_brush_side_attribute_store_t attributes{};
    REQUIRE( BrushSideAttributeStore_Init( &attributes, common::Allocator_GetSystem() ) ==
             geometry_status_t::OK );
    REQUIRE( BrushSideAttributeStore_TryAppendDefaults(
                 &attributes, pDocument->policy, planes.size() ) == geometry_status_t::OK );
    const geometry_status_t status = GeometryDocument_TryCreateBrushValue(
        pDocument, &holder.brush, &attributes, &pOut->p, nullptr );
    BrushSideAttributeStore_Shutdown( &attributes );
    REQUIRE( status == geometry_status_t::OK );
}

// Inserts a value into the document in its own revision.
inline void InsertValue( geometry_document_t *pDocument, const geometry_brush_value_t *pValue )
{
    const geometry_document_change_t change{
        geometry_document_change_kind_t::INSERT_BRUSH, pValue->brush.sourceId, pValue };
    REQUIRE( GeometryDocument_TryApply(
                 pDocument, GeometryDocument_Revision( pDocument ),
                 common::span_t<const geometry_document_change_t>{ &change, 1u }, nullptr ) ==
             geometry_status_t::OK );
}

} // namespace cypher::editor::geometry::test

#endif // CYPHER_EDITOR_GEOMETRY_TEST_SUPPORT_H
