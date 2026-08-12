//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Stats_Tests.cpp
//  Purpose: Tests the fixed-capacity Tier0 statistics registry.
//  Details: These tests cover stable IDs, copied metadata, strict types, checked
//           arithmetic, concurrent updates, snapshots, resets, and capacity.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stats.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace cypher::common;

TEST_CASE( "Stats registers stable typed IDs and snapshots", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    char szName[] = "renderer.draw_calls";
    stat_desc_t descriptor{
        szName,
        "Renderer",
        "Submitted draw calls",
        stat_value_type_t::U64
    };
    stat_id_t id = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister( descriptor, &id ) );
    REQUIRE( id != CY_STAT_ID_INVALID );

    szName[0] = 'x';
    REQUIRE( Cy_StatsFind( "renderer.draw_calls" ) == id );
    REQUIRE( Cy_StatsSetU64( id, 42u ) );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( id, &value ) );
    REQUIRE( value.type == stat_value_type_t::U64 );
    REQUIRE( value.u64Value == 42u );

    stat_snapshot_t snapshot{};
    REQUIRE( Cy_StatsGetSnapshot( 0u, &snapshot ) );
    REQUIRE( snapshot.id == id );
    REQUIRE( std::strcmp( snapshot.szName, "renderer.draw_calls" ) == 0 );
    REQUIRE( std::strcmp( snapshot.szCategory, "Renderer" ) == 0 );
    REQUIRE( snapshot.value.u64Value == 42u );
}

TEST_CASE( "Stats rejects type changes and arithmetic overflow", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    stat_id_t id = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister(
        { "counter", "Test", "", stat_value_type_t::I64 },
        &id ) );
    REQUIRE_FALSE( Cy_StatsRegister(
        { "counter", "Test", "", stat_value_type_t::F64 },
        nullptr ) );
    REQUIRE_FALSE( Cy_StatsSetU64( id, 1u ) );
    REQUIRE( Cy_StatsSetI64( id, CY_I64_MAX ) );
    REQUIRE_FALSE( Cy_StatsAddI64( id, 1 ) );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( id, &value ) );
    REQUIRE( value.i64Value == CY_I64_MAX );

    Cy_StatsResetValues();
    REQUIRE( Cy_StatsGet( id, &value ) );
    REQUIRE( value.i64Value == 0 );
}

TEST_CASE( "Stats updates unsigned and floating values with strict types", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    stat_id_t u64Id = CY_STAT_ID_INVALID;
    stat_id_t f64Id = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister(
        { "memory.bytes", "Memory", "", stat_value_type_t::U64 },
        &u64Id ) );
    REQUIRE( Cy_StatsRegister(
        { "frame.ms", "Frame", "", stat_value_type_t::F64 },
        &f64Id ) );

    REQUIRE( Cy_StatsSetU64( u64Id, CY_U64_MAX - 4u ) );
    REQUIRE( Cy_StatsAddU64( u64Id, 4u ) );
    REQUIRE_FALSE( Cy_StatsAddU64( u64Id, 1u ) );

    REQUIRE( Cy_StatsSetF64( f64Id, 10.5 ) );
    REQUIRE( Cy_StatsAddF64( f64Id, -2.25 ) );
    REQUIRE_FALSE( Cy_StatsSetF64( u64Id, 1.0 ) );
    REQUIRE_FALSE( Cy_StatsAddF64( u64Id, 1.0 ) );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( u64Id, &value ) );
    REQUIRE( value.u64Value == CY_U64_MAX );
    REQUIRE( Cy_StatsGetByName( "frame.ms", &value ) );
    REQUIRE( value.type == stat_value_type_t::F64 );
    REQUIRE( value.f64Value == 8.25 );
}

TEST_CASE( "Stats initializes each valid value type and rejects invalid types", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    stat_id_t i64Id = CY_STAT_ID_INVALID;
    stat_id_t u64Id = CY_STAT_ID_INVALID;
    stat_id_t f64Id = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister(
        { "typed.i64", "Test", "", stat_value_type_t::I64 },
        &i64Id ) );
    REQUIRE( Cy_StatsRegister(
        { "typed.u64", "Test", "", stat_value_type_t::U64 },
        &u64Id ) );
    REQUIRE( Cy_StatsRegister(
        { "typed.f64", "Test", "", stat_value_type_t::F64 },
        &f64Id ) );

    stat_value_t value{};
    REQUIRE( Cy_StatsGet( i64Id, &value ) );
    REQUIRE( value.type == stat_value_type_t::I64 );
    REQUIRE( value.i64Value == 0 );
    REQUIRE( Cy_StatsGet( u64Id, &value ) );
    REQUIRE( value.type == stat_value_type_t::U64 );
    REQUIRE( value.u64Value == 0u );
    REQUIRE( Cy_StatsGet( f64Id, &value ) );
    REQUIRE( value.type == stat_value_type_t::F64 );
    REQUIRE( value.f64Value == 0.0 );

    stat_id_t invalidId = 99u;
    REQUIRE_FALSE( Cy_StatsRegister(
        {
            "typed.invalid",
            "Test",
            "",
            static_cast<stat_value_type_t>( 255u )
        },
        &invalidId ) );
    REQUIRE( invalidId == CY_STAT_ID_INVALID );
}

TEST_CASE( "Stats serializes concurrent updates to one ID", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    stat_id_t id = CY_STAT_ID_INVALID;
    REQUIRE( Cy_StatsRegister(
        { "jobs.completed", "Jobs", "", stat_value_type_t::U64 },
        &id ) );

    constexpr usize THREAD_COUNT = 8u;
    constexpr usize ADDS_PER_THREAD = 1000u;
    std::array<std::thread, THREAD_COUNT> threads;
    std::atomic_bool allSucceeded = true;
    for ( std::thread &thread : threads ) {
        thread = std::thread( [id, &allSucceeded]() {
            for ( usize i = 0u; i < ADDS_PER_THREAD; ++i ) {
                if ( !Cy_StatsAddU64( id, 1u ) ) {
                    allSucceeded.store( false, std::memory_order_relaxed );
                }
            }
        } );
    }
    for ( std::thread &thread : threads ) {
        thread.join();
    }

    REQUIRE( allSucceeded.load( std::memory_order_relaxed ) );
    stat_value_t value{};
    REQUIRE( Cy_StatsGet( id, &value ) );
    REQUIRE(
        value.u64Value ==
        static_cast<u64>( THREAD_COUNT * ADDS_PER_THREAD ) );
}

TEST_CASE( "Stats reports fixed-capacity exhaustion", "[CypherCommon][Tier0][Stats]" )
{
    Cy_StatsClearRegistry();

    std::array<std::array<char, 32>, CY_STATS_MAX_COUNT + 1u> names = {};
    for ( usize i = 0u; i < names.size(); ++i ) {
        std::snprintf(
            names[i].data(),
            names[i].size(),
            "stat.%zu",
            i );
    }

    for ( usize i = 0u; i < CY_STATS_MAX_COUNT; ++i ) {
        REQUIRE( Cy_StatsRegister(
            { names[i].data(), "Capacity", "", stat_value_type_t::U64 },
            nullptr ) );
    }

    REQUIRE_FALSE( Cy_StatsRegister(
        { names.back().data(), "Capacity", "", stat_value_type_t::U64 },
        nullptr ) );
    const stats_registry_info_t info = Cy_StatsGetRegistryInfo();
    REQUIRE( info.nRegisteredCount == CY_STATS_MAX_COUNT );
    REQUIRE( info.nCapacity == CY_STATS_MAX_COUNT );
    REQUIRE( info.nDroppedRegistrations == 1u );
}
