//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_Schema_Tests.cpp
//  Purpose: Verifies brush-side attribute records and their bounded storage.
//  Details: Covers the Schema acceptance gate: allocate, copy, query and
//           validate bounded brush-side material and UV projection records with
//           deterministic enumeration and failure-atomic growth. The allocation
//           failure cases use a stub allocator, because failure atomicity
//           cannot be observed any other way.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_BrushSideStore.h"
#include "CypherGeometry_Attributes_Schema.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec2d_Make;
using cypher::math::Vec3d_Make;
using cypher::math::f64;

namespace {

// Refuses every request, so a mutator that reports ALLOCATION_FAILED can be
// checked for having left the store untouched.
void *FailingAllocate( void *, common::usize, common::usize ) noexcept {
	return nullptr;
}

geometry_brush_side_attributes_t MakeAssignedRecord( common::u64 materialValue ) {
	geometry_brush_side_attributes_t record = BrushSideAttributes_MakeDefault();
	record.material.value = materialValue;
	return record;
}

struct StoreFixture {
	common::allocator_t allocator{ *common::Allocator_GetSystem() };
	geometry_brush_side_attribute_store_t store{};

	StoreFixture() {
		REQUIRE( BrushSideAttributeStore_Init( &store, &allocator ) ==
				 geometry_status_t::OK );
	}
	~StoreFixture() { BrushSideAttributeStore_Shutdown( &store ); }
};

} // namespace

//==========================================================================
// Domains and material references
//==========================================================================

TEST_CASE( "attribute domains reject the sentinel values",
		   "[editor][geometry][attributes]" ) {
	REQUIRE_FALSE( GeometryAttributeDomain_IsValid(
		geometry_attribute_domain_t::INVALID ) );
	REQUIRE_FALSE( GeometryAttributeDomain_IsValid(
		geometry_attribute_domain_t::COUNT ) );
	REQUIRE( GeometryAttributeDomain_IsValid(
		geometry_attribute_domain_t::BRUSH_SIDE ) );
	REQUIRE( GeometryAttributeDomain_IsValid(
		geometry_attribute_domain_t::CORNER ) );
}

TEST_CASE( "an unassigned material reference is distinguishable from a real one",
		   "[editor][geometry][attributes]" ) {
	// Zero must mean "nothing assigned" rather than "material zero", or a
	// default-constructed record would look like it points at a real asset.
	REQUIRE_FALSE( GeometryMaterialRef_IsAssigned( GEOMETRY_MATERIAL_REF_UNASSIGNED ) );
	REQUIRE( GeometryMaterialRef_IsAssigned( geometry_material_ref_t{ 1u } ) );
	REQUIRE( GeometryMaterialRef_Equals(
		geometry_material_ref_t{ 7u }, geometry_material_ref_t{ 7u } ) );
	REQUIRE_FALSE( GeometryMaterialRef_Equals(
		geometry_material_ref_t{ 7u }, GEOMETRY_MATERIAL_REF_UNASSIGNED ) );
}

//==========================================================================
// Record validation
//==========================================================================

TEST_CASE( "the default brush-side record is usable for projection",
		   "[editor][geometry][attributes]" ) {
	// A value-initialized record has zeroed axes and zero UV scale, which
	// cannot project. The default builder must produce something valid instead.
	const geometry_numerical_policy_t policy{};
	const geometry_brush_side_attributes_t zeroed{};
	REQUIRE( BrushSideAttributes_Validate( policy, zeroed ) !=
			 geometry_status_t::OK );

	const geometry_brush_side_attributes_t usable = BrushSideAttributes_MakeDefault();
	REQUIRE( BrushSideAttributes_Validate( policy, usable ) ==
			 geometry_status_t::OK );
	REQUIRE_FALSE( GeometryMaterialRef_IsAssigned( usable.material ) );
}

TEST_CASE( "record validation rejects what cannot be projected",
		   "[editor][geometry][attributes]" ) {
	const geometry_numerical_policy_t policy{};
	const f64 nan = std::numeric_limits<f64>::quiet_NaN();

	geometry_brush_side_attributes_t nonFinite = BrushSideAttributes_MakeDefault();
	nonFinite.uvProjection.origin = Vec3d_Make( nan, 0.0, 0.0 );
	REQUIRE( BrushSideAttributes_Validate( policy, nonFinite ) ==
			 geometry_status_t::NUMERIC_FAILURE );

	geometry_brush_side_attributes_t farAway = BrushSideAttributes_MakeDefault();
	farAway.uvProjection.origin =
		Vec3d_Make( policy.fCoordinateMagnitudeLimit * 2.0, 0.0, 0.0 );
	REQUIRE( BrushSideAttributes_Validate( policy, farAway ) ==
			 geometry_status_t::LIMIT_EXCEEDED );

	// A zero UV scale would divide by zero during projection.
	geometry_brush_side_attributes_t zeroScale = BrushSideAttributes_MakeDefault();
	zeroScale.uvProjection.worldUnitsPerUv = Vec2d_Make( 0.0, 1.0 );
	REQUIRE( BrushSideAttributes_Validate( policy, zeroScale ) ==
			 geometry_status_t::DEGENERATE );

	// Non-unit axes cannot form a basis.
	geometry_brush_side_attributes_t stretched = BrushSideAttributes_MakeDefault();
	stretched.uvProjection.uAxis = Vec3d_Make( 2.0, 0.0, 0.0 );
	REQUIRE( BrushSideAttributes_Validate( policy, stretched ) ==
			 geometry_status_t::DEGENERATE );

	// Axes that are unit length but not orthogonal still project, yet
	// unprojection would no longer invert it -- a UV round trip would move the
	// point, so the record is rejected.
	geometry_brush_side_attributes_t skewed = BrushSideAttributes_MakeDefault();
	skewed.uvProjection.vAxis = skewed.uvProjection.uAxis;
	REQUIRE( BrushSideAttributes_Validate( policy, skewed ) ==
			 geometry_status_t::DEGENERATE );
}

TEST_CASE( "a mirrored mapping stays valid",
		   "[editor][geometry][attributes]" ) {
	// A negative UV scale is how mirroring is authored. Rejecting it as
	// "degenerate" would forbid a legitimate result, so only magnitude matters.
	const geometry_numerical_policy_t policy{};
	geometry_brush_side_attributes_t mirrored = BrushSideAttributes_MakeDefault();
	mirrored.uvProjection.worldUnitsPerUv = Vec2d_Make( -1.0, 1.0 );
	REQUIRE( BrushSideAttributes_Validate( policy, mirrored ) ==
			 geometry_status_t::OK );
}

//==========================================================================
// Store lifecycle and queries
//==========================================================================

TEST_CASE( "store rejects use before initialization and double initialization",
		   "[editor][geometry][attributes]" ) {
	geometry_brush_side_attribute_store_t store{};
	const geometry_policy_t policy{};
	geometry_brush_side_attributes_t record{};

	REQUIRE( BrushSideAttributeStore_Count( &store ) == 0u );
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &store, policy, BrushSideAttributes_MakeDefault(), nullptr ) ==
			 geometry_status_t::NOT_INITIALIZED );
	REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &record ) ==
			 geometry_status_t::NOT_INITIALIZED );

	// Shutting down an untouched store must be safe, so a partially built
	// operation can unwind without tracking how far it got.
	BrushSideAttributeStore_Shutdown( &store );

	common::allocator_t allocator{ *common::Allocator_GetSystem() };
	REQUIRE( BrushSideAttributeStore_Init( &store, &allocator ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_Init( &store, &allocator ) ==
			 geometry_status_t::ALREADY_INITIALIZED );
	BrushSideAttributeStore_Shutdown( &store );
}

TEST_CASE( "appended records enumerate in insertion order",
		   "[editor][geometry][attributes]" ) {
	StoreFixture fixture;
	const geometry_policy_t policy{};

	for ( common::u64 i = 1u; i <= 6u; ++i ) {
		usize index = 0u;
		REQUIRE( BrushSideAttributeStore_TryAppend(
					 &fixture.store, policy, MakeAssignedRecord( i ), &index ) ==
				 geometry_status_t::OK );
		// The returned index is the position the record landed in, which is what
		// lets a representation associate a side with its attributes.
		REQUIRE( index == static_cast<usize>( i - 1u ) );
	}
	REQUIRE( BrushSideAttributeStore_Count( &fixture.store ) == 6u );

	for ( common::u64 i = 1u; i <= 6u; ++i ) {
		geometry_brush_side_attributes_t record{};
		REQUIRE( BrushSideAttributeStore_TryGet(
					 &fixture.store, static_cast<usize>( i - 1u ), &record ) ==
				 geometry_status_t::OK );
		REQUIRE( record.material.value == i );
	}

	geometry_brush_side_attributes_t outOfRange{};
	REQUIRE( BrushSideAttributeStore_TryGet( &fixture.store, 6u, &outOfRange ) ==
			 geometry_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "storage refuses records it would never accept",
		   "[editor][geometry][attributes]" ) {
	// Validating on the way in means every later read can skip revalidation.
	StoreFixture fixture;
	const geometry_policy_t policy{};

	geometry_brush_side_attributes_t broken = BrushSideAttributes_MakeDefault();
	broken.uvProjection.worldUnitsPerUv = Vec2d_Make( 0.0, 0.0 );
	usize index = 12345u;
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &fixture.store, policy, broken, &index ) ==
			 geometry_status_t::DEGENERATE );
	REQUIRE( BrushSideAttributeStore_Count( &fixture.store ) == 0u );
	// The out-parameter must be untouched on failure, or a caller that ignores
	// the status would index into a record that does not exist.
	REQUIRE( index == 12345u );
}

TEST_CASE( "a rejected replacement leaves the existing record intact",
		   "[editor][geometry][attributes]" ) {
	StoreFixture fixture;
	const geometry_policy_t policy{};
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &fixture.store, policy, MakeAssignedRecord( 42u ), nullptr ) ==
			 geometry_status_t::OK );

	geometry_brush_side_attributes_t broken = BrushSideAttributes_MakeDefault();
	broken.uvProjection.uAxis = Vec3d_Make( 0.0, 0.0, 0.0 );
	REQUIRE( BrushSideAttributeStore_TrySet(
				 &fixture.store, policy.numerical, 0u, broken ) !=
			 geometry_status_t::OK );

	geometry_brush_side_attributes_t survivor{};
	REQUIRE( BrushSideAttributeStore_TryGet( &fixture.store, 0u, &survivor ) ==
			 geometry_status_t::OK );
	REQUIRE( survivor.material.value == 42u );

	REQUIRE( BrushSideAttributeStore_TrySet(
				 &fixture.store, policy.numerical, 0u, MakeAssignedRecord( 99u ) ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_TryGet( &fixture.store, 0u, &survivor ) ==
			 geometry_status_t::OK );
	REQUIRE( survivor.material.value == 99u );
}

//==========================================================================
// Bounds and failure atomicity
//==========================================================================

TEST_CASE( "growth is bounded by the side limit",
		   "[editor][geometry][attributes]" ) {
	StoreFixture fixture;
	geometry_policy_t policy{};
	policy.limits.cBrushSidesPerBrushMax = 3u;

	for ( common::u64 i = 1u; i <= 3u; ++i ) {
		REQUIRE( BrushSideAttributeStore_TryAppend(
					 &fixture.store, policy, MakeAssignedRecord( i ), nullptr ) ==
				 geometry_status_t::OK );
	}
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &fixture.store, policy, MakeAssignedRecord( 4u ), nullptr ) ==
			 geometry_status_t::LIMIT_EXCEEDED );
	REQUIRE( BrushSideAttributeStore_Count( &fixture.store ) == 3u );

	// The bound applies to reservation too, and is checked before allocating so
	// an absurd request fails immediately instead of slowly.
	REQUIRE( BrushSideAttributeStore_TryReserve(
				 &fixture.store, policy.limits, 4u ) ==
			 geometry_status_t::LIMIT_EXCEEDED );
}

TEST_CASE( "a failed allocation leaves the store exactly as it was",
		   "[editor][geometry][attributes]" ) {
	// This is the failure-atomic requirement. An attribute store is mutated
	// inside a transaction that may roll back; a half-grown store would survive
	// the rollback and desynchronize from the topology it describes.
	common::allocator_t allocator{ *common::Allocator_GetSystem() };
	geometry_brush_side_attribute_store_t store{};
	REQUIRE( BrushSideAttributeStore_Init( &store, &allocator ) ==
			 geometry_status_t::OK );

	const geometry_policy_t policy{};
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &store, policy, MakeAssignedRecord( 11u ), nullptr ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &store, policy, MakeAssignedRecord( 22u ), nullptr ) ==
			 geometry_status_t::OK );

	// Must stay under cBrushSidesPerBrushMax: the limit is checked before any
	// allocation is attempted, so a request above it reports LIMIT_EXCEEDED and
	// would never reach the allocator this test is trying to fail.
	REQUIRE( store.records.nCapacity < 200u );

	allocator.pfnAllocate = FailingAllocate;
	// Reserve past capacity so growth is actually required and must fail.
	REQUIRE( BrushSideAttributeStore_TryReserve(
				 &store, policy.limits, 200u ) ==
			 geometry_status_t::ALLOCATION_FAILED );

	allocator.pfnAllocate = common::Allocator_GetSystem()->pfnAllocate;
	REQUIRE( BrushSideAttributeStore_Count( &store ) == 2u );
	geometry_brush_side_attributes_t record{};
	REQUIRE( BrushSideAttributeStore_TryGet( &store, 0u, &record ) ==
			 geometry_status_t::OK );
	REQUIRE( record.material.value == 11u );
	REQUIRE( BrushSideAttributeStore_TryGet( &store, 1u, &record ) ==
			 geometry_status_t::OK );
	REQUIRE( record.material.value == 22u );

	BrushSideAttributeStore_Shutdown( &store );
}

//==========================================================================
// Copy and validate
//==========================================================================

TEST_CASE( "copying reproduces contents and order",
		   "[editor][geometry][attributes]" ) {
	StoreFixture source;
	StoreFixture destination;
	const geometry_policy_t policy{};

	for ( common::u64 i = 1u; i <= 4u; ++i ) {
		REQUIRE( BrushSideAttributeStore_TryAppend(
					 &source.store, policy, MakeAssignedRecord( i ), nullptr ) ==
				 geometry_status_t::OK );
	}
	// Pre-existing destination content must be replaced, not appended to.
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &destination.store, policy, MakeAssignedRecord( 900u ), nullptr ) ==
			 geometry_status_t::OK );

	REQUIRE( BrushSideAttributeStore_TryCopyFrom(
				 &destination.store, &source.store, policy.limits ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_Count( &destination.store ) == 4u );
	for ( common::u64 i = 1u; i <= 4u; ++i ) {
		geometry_brush_side_attributes_t record{};
		REQUIRE( BrushSideAttributeStore_TryGet(
					 &destination.store, static_cast<usize>( i - 1u ), &record ) ==
				 geometry_status_t::OK );
		REQUIRE( record.material.value == i );
	}

	// Self-copy is the identity and must not clear the store out from under
	// its own read.
	REQUIRE( BrushSideAttributeStore_TryCopyFrom(
				 &source.store, &source.store, policy.limits ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_Count( &source.store ) == 4u );
}

TEST_CASE( "a failed copy leaves the destination unchanged",
		   "[editor][geometry][attributes]" ) {
	StoreFixture source;
	const geometry_policy_t policy{};
	// Large enough that the destination's capacity cannot already cover it --
	// otherwise Vector_Reserve is a no-op, no allocation happens, and the test
	// would pass without ever exercising the failure path.
	constexpr common::u64 cSourceRecords = 200u;
	for ( common::u64 i = 1u; i <= cSourceRecords; ++i ) {
		REQUIRE( BrushSideAttributeStore_TryAppend(
					 &source.store, policy, MakeAssignedRecord( i ), nullptr ) ==
				 geometry_status_t::OK );
	}

	common::allocator_t allocator{ *common::Allocator_GetSystem() };
	geometry_brush_side_attribute_store_t destination{};
	REQUIRE( BrushSideAttributeStore_Init( &destination, &allocator ) ==
			 geometry_status_t::OK );
	REQUIRE( BrushSideAttributeStore_TryAppend(
				 &destination, policy, MakeAssignedRecord( 777u ), nullptr ) ==
			 geometry_status_t::OK );

	REQUIRE( destination.records.nCapacity < cSourceRecords );

	allocator.pfnAllocate = FailingAllocate;
	REQUIRE( BrushSideAttributeStore_TryCopyFrom(
				 &destination, &source.store, policy.limits ) ==
			 geometry_status_t::ALLOCATION_FAILED );
	allocator.pfnAllocate = common::Allocator_GetSystem()->pfnAllocate;

	// Had the copy cleared before reserving, this record would be gone.
	REQUIRE( BrushSideAttributeStore_Count( &destination ) == 1u );
	geometry_brush_side_attributes_t record{};
	REQUIRE( BrushSideAttributeStore_TryGet( &destination, 0u, &record ) ==
			 geometry_status_t::OK );
	REQUIRE( record.material.value == 777u );

	BrushSideAttributeStore_Shutdown( &destination );
}

TEST_CASE( "store validation reports the first bad record deterministically",
		   "[editor][geometry][attributes]" ) {
	StoreFixture fixture;
	geometry_policy_t policy{};
	for ( common::u64 i = 1u; i <= 3u; ++i ) {
		REQUIRE( BrushSideAttributeStore_TryAppend(
					 &fixture.store, policy, MakeAssignedRecord( i ), nullptr ) ==
				 geometry_status_t::OK );
	}
	REQUIRE( BrushSideAttributeStore_Validate( &fixture.store, policy ) ==
			 geometry_status_t::OK );

	// Tightening policy after the fact is one of the ways a store can come to
	// hold records it would no longer accept -- deserialization is the other.
	policy.numerical.fCoordinateMagnitudeLimit = 1.0e-3;
	geometry_brush_side_attributes_t moved = BrushSideAttributes_MakeDefault();
	moved.uvProjection.origin = Vec3d_Make( 1.0, 0.0, 0.0 );
	REQUIRE( BrushSideAttributeStore_TrySet(
				 &fixture.store, geometry_numerical_policy_t{}, 1u, moved ) ==
			 geometry_status_t::OK );

	REQUIRE( BrushSideAttributeStore_Validate( &fixture.store, policy ) ==
			 geometry_status_t::LIMIT_EXCEEDED );
}

} // namespace cypher::editor::geometry
