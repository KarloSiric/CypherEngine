//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Types_Tests.cpp
//  Purpose: Verifies the first shared editor-geometry identity contracts.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Types.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace cypher::editor::geometry
{

TEST_CASE( "geometry source identity reserves zero", "[editor][geometry]" )
{
    REQUIRE_FALSE( GeometrySourceId_IsValid( GEOMETRY_SOURCE_ID_INVALID ) );
    REQUIRE( GeometrySourceId_IsValid( geometry_source_id_t{ 1u } ) );
}

TEST_CASE( "geometry live handles require a slot and generation",
           "[editor][geometry]" )
{
    REQUIRE_FALSE( GeometryHandle_IsValid( GEOMETRY_HANDLE_INVALID<
                   geometry_element_kind_t::VERTEX> ) );
    REQUIRE_FALSE( GeometryHandle_IsValid(
        geometry_vertex_handle_t{ 4u, 0u } ) );
    REQUIRE_FALSE( GeometryHandle_IsValid(
        geometry_vertex_handle_t{ common::CY_INVALID_INDEX, 2u } ) );
    REQUIRE( GeometryHandle_IsValid(
        geometry_vertex_handle_t{ 4u, 2u } ) );
}

TEST_CASE( "geometry element references combine persistent identity and kind",
           "[editor][geometry]" )
{
    REQUIRE_FALSE( GeometryElementRef_IsValid( geometry_element_ref_t{} ) );
    REQUIRE_FALSE( GeometryElementRef_IsValid( geometry_element_ref_t{
        geometry_source_id_t{ 7u }, geometry_element_kind_t::COUNT } ) );
    REQUIRE( GeometryElementRef_IsValid( geometry_element_ref_t{
        geometry_source_id_t{ 7u }, geometry_element_kind_t::FACE } ) );
    REQUIRE( GeometryElementRef_IsValid( geometry_element_ref_t{
        geometry_source_id_t{ 8u }, geometry_element_kind_t::BRUSH_SIDE } ) );
    REQUIRE( GeometryElementRef_IsValid( geometry_element_ref_t{
        geometry_source_id_t{ 9u }, geometry_element_kind_t::CONTROL_POINT } ) );
}

TEST_CASE( "authored geometry representations are explicit and bounded",
           "[editor][geometry]" )
{
    REQUIRE( GeometryRepresentationKind_IsValid(
        geometry_representation_kind_t::BRUSH_SOLID ) );
    REQUIRE( GeometryRepresentationKind_IsValid(
        geometry_representation_kind_t::EDITABLE_MESH ) );
    REQUIRE( GeometryRepresentationKind_IsValid(
        geometry_representation_kind_t::POLYGON_2D ) );
    REQUIRE_FALSE( GeometryRepresentationKind_IsValid(
        geometry_representation_kind_t::COUNT ) );
}

TEST_CASE( "geometry representations retain distinct typed handles",
           "[editor][geometry]" )
{
    static_assert( !std::is_same_v<geometry_brush_handle_t,
                                   geometry_mesh_handle_t> );
    static_assert( !std::is_same_v<geometry_polygon_2d_handle_t,
                                   geometry_face_handle_t> );
    static_assert( !std::is_same_v<geometry_patch_handle_t,
                                   geometry_triangle_soup_handle_t> );

    REQUIRE( GeometryHandle_IsValid(
        geometry_brush_side_handle_t{ 2u, 1u } ) );
    REQUIRE( GeometryHandle_IsValid(
        geometry_control_point_handle_t{ 4u, 3u } ) );
}

} // namespace cypher::editor::geometry
