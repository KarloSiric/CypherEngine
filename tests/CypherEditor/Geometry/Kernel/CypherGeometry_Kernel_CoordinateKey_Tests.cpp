//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_CoordinateKey_Tests.cpp
//  Purpose: Verifies canonical coordinate quantization and total ordering.
//  Details: The motivating case is a brush corner where several plane triples
//           meet: each triple computes the same corner through different
//           arithmetic and lands on a slightly different double, and all of
//           them must collapse to one key. Also pins the ordering contract and
//           the documented non-transitivity at cell boundaries, so nobody
//           later mistakes this for a weld test.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_CoordinateKey.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Vec3d_Make;
using cypher::math::f64;
using cypher::math::vec3d_t;

namespace {

geometry_coordinate_key_t RequireKey(
	const geometry_numerical_policy_t &policy, vec3d_t point )
{
	geometry_coordinate_key_t key{};
	REQUIRE( Kernel_TryQuantizePoint( policy, point, &key ) ==
			 geometry_status_t::OK );
	return key;
}

} // namespace

TEST_CASE( "coordinate keys collapse the float spread of one shared corner",
		   "[editor][geometry][kernel]" ) {
	// A pyramid apex is the intersection of four side planes, so C(4,3) = 4
	// different plane triples all compute it. Cramer's rule rounds differently
	// in each, producing four distinct doubles for one corner. If these did not
	// collapse to a single key, the brush would carry four vertices where the
	// author placed one and its topology would be wrong.
	const geometry_numerical_policy_t policy{};
	const vec3d_t apexVariants[]{
		Vec3d_Make( 0.0, 0.0, 2.0 ),
		Vec3d_Make( 0.0, 0.0, 2.0000000000000004 ),
		Vec3d_Make( 0.0, 1.1e-16, 2.0 ),
		Vec3d_Make( -2.2e-16, 0.0, 1.9999999999999998 )
	};

	const geometry_coordinate_key_t reference = RequireKey( policy, apexVariants[0] );
	for ( const vec3d_t variant : apexVariants ) {
		REQUIRE( Kernel_CoordinateKeyEquals( RequireKey( policy, variant ), reference ) );
	}
}

TEST_CASE( "coordinate keys are stable and reproduce the lattice index",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};

	// Default quantum is 1e-8, so 2.0 sits on lattice index 2e8 exactly.
	const geometry_coordinate_key_t key =
		RequireKey( policy, Vec3d_Make( 2.0, -1.0, 0.0 ) );
	REQUIRE( key.x == 200000000 );
	REQUIRE( key.y == -100000000 );
	REQUIRE( key.z == 0 );

	// Quantizing the same point twice must be bit-identical; this is the
	// property every downstream sort and hash depends on.
	REQUIRE( Kernel_CoordinateKeyEquals(
		key, RequireKey( policy, Vec3d_Make( 2.0, -1.0, 0.0 ) ) ) );
}

TEST_CASE( "coordinate key ordering is a strict weak lexicographic order",
		   "[editor][geometry][kernel]" ) {
	const geometry_coordinate_key_t origin{ 0, 0, 0 };
	const geometry_coordinate_key_t alongX{ 1, 0, 0 };
	const geometry_coordinate_key_t alongY{ 0, 1, 0 };
	const geometry_coordinate_key_t alongZ{ 0, 0, 1 };

	REQUIRE( Kernel_CoordinateKeyLess( origin, alongX ) );
	REQUIRE( Kernel_CoordinateKeyLess( origin, alongY ) );
	REQUIRE( Kernel_CoordinateKeyLess( origin, alongZ ) );

	// x dominates y, which dominates z.
	REQUIRE( Kernel_CoordinateKeyLess( alongY, alongX ) );
	REQUIRE( Kernel_CoordinateKeyLess( alongZ, alongY ) );

	// Irreflexive and antisymmetric, which std::sort requires of a comparator.
	REQUIRE_FALSE( Kernel_CoordinateKeyLess( origin, origin ) );
	REQUIRE_FALSE( Kernel_CoordinateKeyLess( alongX, origin ) );

	// Negative indices must order below positive ones rather than by magnitude.
	const geometry_coordinate_key_t negative{ -1, 0, 0 };
	REQUIRE( Kernel_CoordinateKeyLess( negative, origin ) );
	REQUIRE( Kernel_CoordinateKeyLess( negative, alongX ) );
}

TEST_CASE( "sorting by coordinate key is independent of insertion order",
		   "[editor][geometry][kernel]" ) {
	// This is the Gate 2 requirement in miniature: permuting the order in which
	// corners are discovered must not change the canonical traversal. Two
	// different discovery orders of the same cube corners must sort identically.
	const geometry_numerical_policy_t policy{};
	const vec3d_t cornersA[]{
		Vec3d_Make( 1.0, 1.0, 1.0 ),  Vec3d_Make( -1.0, 1.0, 1.0 ),
		Vec3d_Make( 1.0, -1.0, 1.0 ), Vec3d_Make( 1.0, 1.0, -1.0 )
	};
	const vec3d_t cornersB[]{
		Vec3d_Make( 1.0, 1.0, -1.0 ), Vec3d_Make( 1.0, -1.0, 1.0 ),
		Vec3d_Make( 1.0, 1.0, 1.0 ),  Vec3d_Make( -1.0, 1.0, 1.0 )
	};

	geometry_coordinate_key_t sortedA[4]{};
	geometry_coordinate_key_t sortedB[4]{};
	for ( int i = 0; i < 4; ++i ) {
		sortedA[i] = RequireKey( policy, cornersA[i] );
		sortedB[i] = RequireKey( policy, cornersB[i] );
	}

	// Insertion sort, so the test does not depend on a particular std::sort.
	for ( auto &keys : { std::ref( sortedA ), std::ref( sortedB ) } ) {
		auto &array = keys.get();
		for ( int i = 1; i < 4; ++i ) {
			const geometry_coordinate_key_t value = array[i];
			int j = i;
			while ( j > 0 && Kernel_CoordinateKeyLess( value, array[j - 1] ) ) {
				array[j] = array[j - 1];
				--j;
			}
			array[j] = value;
		}
	}
	for ( int i = 0; i < 4; ++i ) {
		REQUIRE( Kernel_CoordinateKeyEquals( sortedA[i], sortedB[i] ) );
	}
}

TEST_CASE( "coordinate keys separate at cell boundaries and are not a weld test",
		   "[editor][geometry][kernel]" ) {
	// Documented limitation, pinned so it cannot be quietly relied upon the
	// wrong way. These two points are a tenth of a quantum apart -- far closer
	// than fWeldDistance -- yet they straddle a cell boundary and receive
	// different keys. Anything that needs "are these the same vertex?" must run
	// a distance test, not compare keys.
	geometry_numerical_policy_t policy{};
	const f64 quantum = policy.fCanonicalQuantization;

	const geometry_coordinate_key_t below =
		RequireKey( policy, Vec3d_Make( 0.4 * quantum, 0.0, 0.0 ) );
	const geometry_coordinate_key_t above =
		RequireKey( policy, Vec3d_Make( 0.6 * quantum, 0.0, 0.0 ) );

	REQUIRE( below.x == 0 );
	REQUIRE( above.x == 1 );
	REQUIRE_FALSE( Kernel_CoordinateKeyEquals( below, above ) );
	REQUIRE( ( 0.6 * quantum - 0.4 * quantum ) < policy.fWeldDistance );
}

TEST_CASE( "quantization rejects input it cannot represent canonically",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const f64 nan = std::numeric_limits<f64>::quiet_NaN();
	const f64 infinity = std::numeric_limits<f64>::infinity();
	geometry_coordinate_key_t key{};

	REQUIRE( Kernel_TryQuantizePoint(
				 policy, Vec3d_Make( nan, 0.0, 0.0 ), &key ) ==
			 geometry_status_t::NUMERIC_FAILURE );
	REQUIRE( Kernel_TryQuantizePoint(
				 policy, Vec3d_Make( infinity, 0.0, 0.0 ), &key ) ==
			 geometry_status_t::NUMERIC_FAILURE );

	// Beyond the coordinate limit the lattice index is no longer guaranteed
	// exactly representable, so a key would silently lose precision.
	const f64 beyondLimit = policy.fCoordinateMagnitudeLimit * 2.0;
	REQUIRE( Kernel_TryQuantizePoint(
				 policy, Vec3d_Make( beyondLimit, 0.0, 0.0 ), &key ) ==
			 geometry_status_t::LIMIT_EXCEEDED );

	// At the limit exactly it must still succeed, or the documented range would
	// be off by one cell.
	REQUIRE( Kernel_TryQuantizePoint(
				 policy,
				 Vec3d_Make( policy.fCoordinateMagnitudeLimit, 0.0, 0.0 ),
				 &key ) == geometry_status_t::OK );

	REQUIRE( Kernel_TryQuantizePoint( policy, Vec3d_Make( 0.0, 0.0, 0.0 ), nullptr ) ==
			 geometry_status_t::INVALID_ARGUMENT );

	geometry_numerical_policy_t brokenPolicy{};
	brokenPolicy.fCanonicalQuantization = 0.0;
	REQUIRE( Kernel_TryQuantizePoint(
				 brokenPolicy, Vec3d_Make( 1.0, 0.0, 0.0 ), &key ) ==
			 geometry_status_t::INVALID_ARGUMENT );
}

} // namespace cypher::editor::geometry
