//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Types_Tests.cpp
//  Purpose: Verifies editor-geometry identity and representation contracts.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Types.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace cypher::editor::geometry {

TEST_CASE( "geometry source identity reserves zero", "[editor][geometry]" ) {
	REQUIRE_FALSE( GeometrySourceId_IsValid( GEOMETRY_SOURCE_ID_INVALID ) );
	REQUIRE( GeometrySourceId_IsValid( geometry_source_id_t{ 1u } ) );
}

TEST_CASE( "geometry live handles require a slot and generation",
		   "[editor][geometry]" ) {
	REQUIRE_FALSE( GeometryHandle_IsValid(
		GEOMETRY_HANDLE_INVALID<geometry_mesh_vertex_tag_t> ) );
	REQUIRE_FALSE( GeometryHandle_IsValid(
		geometry_mesh_vertex_handle_t{ 4u, 0u } ) );
	REQUIRE_FALSE( GeometryHandle_IsValid(
		geometry_mesh_vertex_handle_t{ common::CY_INVALID_INDEX, 2u } ) );
	REQUIRE( GeometryHandle_IsValid(
		geometry_mesh_vertex_handle_t{ 4u, 2u } ) );
}

TEST_CASE( "geometry source references retain compile-time component identity",
		   "[editor][geometry]" ) {
	const geometry_source_ref_t<geometry_mesh_face_tag_t> face{
		geometry_source_id_t{ 7u }
	};
	const geometry_source_ref_t<geometry_brush_side_tag_t> side{
		geometry_source_id_t{ 8u }
	};

	REQUIRE( GeometrySourceRef_IsValid( face ) );
	REQUIRE( GeometrySourceRef_IsValid( side ) );
	REQUIRE_FALSE( GeometrySourceRef_IsValid(
		GEOMETRY_SOURCE_REF_INVALID<geometry_curve_tag_t> ) );

	static_assert( !std::is_same_v<decltype( face ), decltype( side )> );
}

TEST_CASE( "source representation tags reject default and sentinel values",
		   "[editor][geometry]" ) {
	REQUIRE_FALSE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::INVALID ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::BRUSH_SOLID ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::EDITABLE_MESH ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::PLANAR_REGION ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::PATCH_SURFACE ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::CURVE_NETWORK ) );
	REQUIRE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::HEIGHT_FIELD ) );
	REQUIRE_FALSE( GeometrySourceRepresentationKind_IsValid(
		geometry_source_representation_kind_t::COUNT ) );
}

TEST_CASE( "same-named components in different representations have distinct handles",
		   "[editor][geometry]" ) {
	static_assert( !std::is_same_v<geometry_brush_handle_t,
								   geometry_mesh_handle_t> );
	static_assert( !std::is_same_v<geometry_mesh_vertex_handle_t,
								   geometry_planar_vertex_handle_t> );
	static_assert( !std::is_same_v<geometry_patch_control_point_handle_t,
								   geometry_curve_control_point_handle_t> );
	static_assert( !std::is_same_v<geometry_planar_region_handle_t,
								   geometry_mesh_face_handle_t> );

	REQUIRE( GeometryHandle_IsValid(
		geometry_brush_side_handle_t{ 2u, 1u } ) );
	REQUIRE( GeometryHandle_IsValid(
		geometry_curve_control_point_handle_t{ 4u, 3u } ) );
}

TEST_CASE( "geometry handles remain compact POD values", "[editor][geometry]" ) {
	STATIC_REQUIRE( sizeof( geometry_mesh_vertex_handle_t ) == sizeof( common::u32 ) * 2u );
	STATIC_REQUIRE( std::is_standard_layout_v<geometry_mesh_vertex_handle_t> );
	STATIC_REQUIRE( std::is_trivially_copyable_v<geometry_mesh_vertex_handle_t> );
	STATIC_REQUIRE( std::is_standard_layout_v<geometry_planar_vertex_handle_t> );
	STATIC_REQUIRE( std::is_trivially_copyable_v<geometry_planar_vertex_handle_t> );
}

TEST_CASE( "geometry maps common generation-pool failures without losing cause",
		   "[editor][geometry]" ) {
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::OK )
			 == geometry_status_t::OK );
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::STALE_HANDLE )
			 == geometry_status_t::STALE_HANDLE );
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::LIMIT_EXCEEDED )
			 == geometry_status_t::LIMIT_EXCEEDED );
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::ALLOCATION_FAILED )
			 == geometry_status_t::ALLOCATION_FAILED );
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::CORRUPT_STATE )
			 == geometry_status_t::CORRUPT_STATE );
	REQUIRE( GeometryStatus_FromGenerationPoolStatus(
				 common::generation_pool_status_t::COUNT )
			 == geometry_status_t::CORRUPT_STATE );
}

} // namespace cypher::editor::geometry
