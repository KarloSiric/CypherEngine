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
}

} // namespace cypher::editor::geometry

