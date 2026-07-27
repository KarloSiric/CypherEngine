//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_TypeTraits_Tests.cpp
//  Purpose: Tests Tier0 type-trait vocabulary and relocation policy.
//  Details: These compile-time checks protect transformations, categories,
//           construction properties, enum conversion, and relocation opt-in.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TypeTraits.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

enum class test_value_t : unsigned short {
    Value = 17u
};

struct trivial_record_t {
    int value;
};

struct owned_record_t {
    std::string value;
};

} // namespace

TEST_CASE( "TypeTraits transforms types predictably", "[CypherCommon][Tier0][TypeTraits]" )
{
    STATIC_REQUIRE( is_same_v<remove_reference_t<int &>, int> );
    STATIC_REQUIRE( is_same_v<remove_cvref_t<const volatile int &>, int> );
    STATIC_REQUIRE( is_same_v<remove_pointer_t<const int *>, const int> );
    STATIC_REQUIRE( is_same_v<make_unsigned_t<int>, unsigned int> );
    STATIC_REQUIRE( is_same_v<conditional_t<true, int, float>, int> );
    STATIC_REQUIRE( is_same_v<common_type_t<int, float>, float> );
}

TEST_CASE( "TypeTraits reports categories and construction properties", "[CypherCommon][Tier0][TypeTraits]" )
{
    STATIC_REQUIRE( is_integral_v<int> );
    STATIC_REQUIRE( is_floating_point_v<float> );
    STATIC_REQUIRE( is_arithmetic_v<double> );
    STATIC_REQUIRE( is_enum_v<test_value_t> );
    STATIC_REQUIRE( is_pointer_v<void *> );
    STATIC_REQUIRE( is_array_v<int[4]> );
    STATIC_REQUIRE( is_trivially_copyable_v<trivial_record_t> );
    STATIC_REQUIRE( is_standard_layout_v<trivial_record_t> );
    STATIC_REQUIRE( is_move_constructible_v<owned_record_t> );
    STATIC_REQUIRE( is_destructible_v<owned_record_t> );
}

TEST_CASE( "TypeTraits relocation policy is conservative", "[CypherCommon][Tier0][TypeTraits]" )
{
    STATIC_REQUIRE( is_trivially_relocatable_v<trivial_record_t> );
    STATIC_REQUIRE_FALSE( is_trivially_relocatable_v<owned_record_t> );
}

TEST_CASE( "TypeTraits converts enums without changing representation", "[CypherCommon][Tier0][TypeTraits]" )
{
    STATIC_REQUIRE( Cy_ToUnderlying( test_value_t::Value ) == 17u );
    STATIC_REQUIRE( is_same_v<decltype( Cy_ToUnderlying( test_value_t::Value ) ), unsigned short> );
}

