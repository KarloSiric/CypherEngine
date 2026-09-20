//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Kernel_Classification_Tests.cpp
//  Purpose: Verifies policy-aware point/plane orientation classification.
//  Details: Covers the Kernel README acceptance gate directly: deterministic
//           classification at documented scale limits, nextafter boundary
//           behavior at the tolerance edge, reversed winding, and rejection of
//           non-finite, out-of-range, and non-normalized input without
//           publishing a trustworthy orientation.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Kernel_Classification.h"

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

namespace cypher::editor::geometry {

using cypher::math::Planed_Flip;
using cypher::math::Planed_Make;
using cypher::math::Vec3d_Make;
using cypher::math::f64;
using cypher::math::planed_t;
using cypher::math::vec3d_t;

namespace {

// Plane through the origin, normal to +Z: dot(normal, point) + d == point.z.
planed_t MakeGroundPlane() noexcept {
	return Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 ), 0.0 );
}

} // namespace

TEST_CASE( "Kernel_ClassifyPoint reports the correct side for well-separated points",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();

	const geometry_classify_result_t above =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( 0.0, 0.0, 1.0 ) );
	REQUIRE( above.status == geometry_status_t::OK );
	REQUIRE( above.orientation == geometry_orientation_t::POSITIVE );

	const geometry_classify_result_t below =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( 0.0, 0.0, -1.0 ) );
	REQUIRE( below.status == geometry_status_t::OK );
	REQUIRE( below.orientation == geometry_orientation_t::NEGATIVE );

	const geometry_classify_result_t on =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( 5.0, -3.0, 0.0 ) );
	REQUIRE( on.status == geometry_status_t::OK );
	REQUIRE( on.orientation == geometry_orientation_t::ON_PLANE );
}

TEST_CASE( "Kernel_ClassifyPoint is exactly repeatable for identical input",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const vec3d_t point = Vec3d_Make( 12.5, -4.25, 0.75 );

	const geometry_classify_result_t first =
		Kernel_ClassifyPoint( policy, ground, point );
	const geometry_classify_result_t second =
		Kernel_ClassifyPoint( policy, ground, point );

	REQUIRE( first.status == second.status );
	REQUIRE( first.orientation == second.orientation );
}

TEST_CASE( "Kernel_ClassifyPoint flips orientation under reversed plane winding",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const planed_t flipped = Planed_Flip( ground );
	const vec3d_t point = Vec3d_Make( 0.0, 0.0, 1.0 );

	const geometry_classify_result_t original =
		Kernel_ClassifyPoint( policy, ground, point );
	const geometry_classify_result_t reversed =
		Kernel_ClassifyPoint( policy, flipped, point );

	REQUIRE( original.status == geometry_status_t::OK );
	REQUIRE( reversed.status == geometry_status_t::OK );
	REQUIRE( original.orientation == geometry_orientation_t::POSITIVE );
	REQUIRE( reversed.orientation == geometry_orientation_t::NEGATIVE );
}

TEST_CASE( "Kernel_ClassifyPoint resolves the exact tolerance boundary with nextafter",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const f64 tolerance = policy.fCoplanarDistanceTolerance;

	// Planed_ClassifyPoint's contract is strict (> tolerance, not >=), so a
	// point sitting exactly on the tolerance boundary must still read as
	// ON_PLANE, and only the very next representable double above it crosses
	// into POSITIVE.
	const geometry_classify_result_t atBoundary =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( 0.0, 0.0, tolerance ) );
	REQUIRE( atBoundary.status == geometry_status_t::OK );
	REQUIRE( atBoundary.orientation == geometry_orientation_t::ON_PLANE );

	const f64 justBeyond = std::nextafter(
		tolerance, std::numeric_limits<f64>::infinity() );
	const geometry_classify_result_t beyondBoundary =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( 0.0, 0.0, justBeyond ) );
	REQUIRE( beyondBoundary.status == geometry_status_t::OK );
	REQUIRE( beyondBoundary.orientation == geometry_orientation_t::POSITIVE );

	const geometry_classify_result_t atNegativeBoundary = Kernel_ClassifyPoint(
		policy, ground, Vec3d_Make( 0.0, 0.0, -tolerance ) );
	REQUIRE( atNegativeBoundary.status == geometry_status_t::OK );
	REQUIRE( atNegativeBoundary.orientation == geometry_orientation_t::ON_PLANE );

	const f64 justBelow = std::nextafter(
		-tolerance, -std::numeric_limits<f64>::infinity() );
	const geometry_classify_result_t beyondNegativeBoundary = Kernel_ClassifyPoint(
		policy, ground, Vec3d_Make( 0.0, 0.0, justBelow ) );
	REQUIRE( beyondNegativeBoundary.status == geometry_status_t::OK );
	REQUIRE( beyondNegativeBoundary.orientation == geometry_orientation_t::NEGATIVE );
}

TEST_CASE( "Kernel_ClassifyPoint classifies at the documented coordinate magnitude limit",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const f64 limit = policy.fCoordinateMagnitudeLimit;

	const geometry_classify_result_t atLimit =
		Kernel_ClassifyPoint( policy, ground, Vec3d_Make( limit, -limit, 0.0 ) );
	REQUIRE( atLimit.status == geometry_status_t::OK );
	REQUIRE( atLimit.orientation == geometry_orientation_t::ON_PLANE );
}

TEST_CASE( "Kernel_ClassifyPoint rejects non-finite input without publishing an orientation",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const f64 nan = std::numeric_limits<f64>::quiet_NaN();
	const f64 infinity = std::numeric_limits<f64>::infinity();

	REQUIRE( Kernel_ClassifyPoint( policy, ground, Vec3d_Make( nan, 0.0, 0.0 ) ).status ==
			 geometry_status_t::NUMERIC_FAILURE );
	REQUIRE( Kernel_ClassifyPoint( policy, ground, Vec3d_Make( infinity, 0.0, 0.0 ) ).status ==
			 geometry_status_t::NUMERIC_FAILURE );

	const planed_t nonFiniteNormal = Planed_Make( Vec3d_Make( nan, 0.0, 0.0 ), 0.0 );
	REQUIRE( Kernel_ClassifyPoint(
				 policy, nonFiniteNormal, Vec3d_Make( 0.0, 0.0, 0.0 ) )
				 .status == geometry_status_t::NUMERIC_FAILURE );

	const planed_t nonFiniteOffset =
		Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 ), infinity );
	REQUIRE( Kernel_ClassifyPoint(
				 policy, nonFiniteOffset, Vec3d_Make( 0.0, 0.0, 0.0 ) )
				 .status == geometry_status_t::NUMERIC_FAILURE );
}

TEST_CASE( "Kernel_ClassifyPoint rejects input beyond the coordinate magnitude limit",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const planed_t ground = MakeGroundPlane();
	const f64 beyondLimit = policy.fCoordinateMagnitudeLimit * 2.0;

	REQUIRE( Kernel_ClassifyPoint(
				 policy, ground, Vec3d_Make( beyondLimit, 0.0, 0.0 ) )
				 .status == geometry_status_t::LIMIT_EXCEEDED );

	const planed_t farPlane =
		Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 ), beyondLimit );
	REQUIRE( Kernel_ClassifyPoint( policy, farPlane, Vec3d_Make( 0.0, 0.0, 0.0 ) )
				 .status == geometry_status_t::LIMIT_EXCEEDED );
}

TEST_CASE( "Kernel_ClassifyPoint rejects a plane normal outside the unit-length tolerance",
		   "[editor][geometry][kernel]" ) {
	const geometry_numerical_policy_t policy{};
	const f64 tolerance = policy.fUnitNormalTolerance;

	// Within tolerance: still trusted for classification.
	const planed_t nearlyUnit =
		Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 + tolerance * 0.5 ), 0.0 );
	const geometry_classify_result_t nearlyUnitResult = Kernel_ClassifyPoint(
		policy, nearlyUnit, Vec3d_Make( 0.0, 0.0, 1.0 ) );
	REQUIRE( nearlyUnitResult.status == geometry_status_t::OK );

	// Outside tolerance: refused rather than silently treated as metric.
	const planed_t nonUnit =
		Planed_Make( Vec3d_Make( 0.0, 0.0, 1.0 + tolerance * 2.0 ), 0.0 );
	const geometry_classify_result_t nonUnitResult = Kernel_ClassifyPoint(
		policy, nonUnit, Vec3d_Make( 0.0, 0.0, 1.0 ) );
	REQUIRE( nonUnitResult.status == geometry_status_t::INVALID_ARGUMENT );
}

} // namespace cypher::editor::geometry
