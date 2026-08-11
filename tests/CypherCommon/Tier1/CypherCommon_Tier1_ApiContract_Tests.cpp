//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ApiContract_Tests.cpp
//  Purpose: Verifies the complete Tier1 public API composes as one contract.
//  Details: These tests cover declarations, value layouts, aliases, and default states.
//           Runtime behavior is tested separately as each implementation is completed.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Tier1.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Tier1 view contracts remain lightweight values",
           "[CypherCommon][Tier1][ApiContract]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<string_view_t> );
    STATIC_REQUIRE( is_standard_layout_v<string_view_t> );
    STATIC_REQUIRE( is_trivially_copyable_v<span_t<byte>> );
    STATIC_REQUIRE( is_standard_layout_v<span_t<byte>> );
    STATIC_REQUIRE( is_same_v<array_view_t<u32>, span_t<const u32>> );
    STATIC_REQUIRE( sizeof( binary_block_t ) == sizeof( const void * ) + sizeof( usize ) );
}

TEST_CASE( "Tier1 fixed binary values retain stable widths",
           "[CypherCommon][Tier1][ApiContract]" )
{
    STATIC_REQUIRE( sizeof( character_set_t ) == 32u );
    STATIC_REQUIRE( sizeof( content_hash_t ) == 16u );
    STATIC_REQUIRE( sizeof( unique_id_t ) == CY_UNIQUE_ID_BYTE_COUNT );
    STATIC_REQUIRE( sizeof( color32_t ) == 4u );
    STATIC_REQUIRE( CY_UNIQUE_ID_STRING_LENGTH == 36u );
}

TEST_CASE( "Tier1 owning values default to empty non-owning states",
           "[CypherCommon][Tier1][ApiContract]" )
{
    const array_t<u32> array{};
    const vector_t<u32> vector{};
    const blob_t blob{};
    const text_buffer_t text{};

    REQUIRE( array.pData == nullptr );
    REQUIRE( array.nCount == 0u );
    REQUIRE( array.pAllocator == nullptr );

    REQUIRE( vector.pData == nullptr );
    REQUIRE( vector.nCount == 0u );
    REQUIRE( vector.nCapacity == 0u );
    REQUIRE( vector.pAllocator == nullptr );

    REQUIRE( blob.pData == nullptr );
    REQUIRE( blob.cbSize == 0u );
    REQUIRE( blob.cbCapacity == 0u );
    REQUIRE( blob.pAllocator == nullptr );

    REQUIRE( text.pData == nullptr );
    REQUIRE( text.cchLength == 0u );
    REQUIRE( text.cchCapacity == 0u );
    REQUIRE( text.pAllocator == nullptr );
}

TEST_CASE( "Tier1 owning values reject implicit shallow copies",
           "[CypherCommon][Tier1][ApiContract]" )
{
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<array_t<u32>> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<vector_t<u32>> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<small_vector_t<u32, 8u>> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<blob_t> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<text_buffer_t> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<hash_table_t<u32, u32>> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<rb_tree_t<u32, u32>> );
}

TEST_CASE( "Tier1 const containers expose read-only access",
           "[CypherCommon][Tier1][ApiContract]" )
{
    STATIC_REQUIRE( is_same_v<
        decltype( Vector_Front( static_cast<const vector_t<u32> *>( nullptr ) ) ),
        const u32 *> );
    STATIC_REQUIRE( is_same_v<
        decltype( SmallVector_At(
            static_cast<const small_vector_t<u32, 8u> *>( nullptr ),
            0u ) ),
        const u32 *> );
    STATIC_REQUIRE( is_same_v<
        decltype( RingBuffer_At(
            static_cast<const ring_buffer_t<u32> *>( nullptr ),
            0u ) ),
        const u32 *> );
    STATIC_REQUIRE( is_same_v<
        decltype( HashTable_Find(
            static_cast<const hash_table_t<u32, u32> *>( nullptr ),
            static_cast<const u32 &>( 0u ) ) ),
        const u32 *> );
}

TEST_CASE( "Tier1 inline-capacity values reserve storage without allocation",
           "[CypherCommon][Tier1][ApiContract]" )
{
    fixed_string_t<31u> string{};
    fixed_array_t<u32, 8u> array{};
    small_vector_t<u32, 8u> vector{};
    function_t<void(), 48u> function{};

    STATIC_REQUIRE( sizeof( string.data ) == 32u );
    STATIC_REQUIRE( FixedArray_Count( array ) == 8u );
    STATIC_REQUIRE( sizeof( vector.inlineStorage ) == sizeof( u32 ) * 8u );
    STATIC_REQUIRE( sizeof( function.inlineStorage ) == 48u );

    REQUIRE( string.cchLength == 0u );
    REQUIRE( vector.nCount == 0u );
    REQUIRE( vector.nCapacity == 8u );
    REQUIRE( function.pCallable == nullptr );
}

TEST_CASE( "Tier1 network and serialization defaults are invalid or empty",
           "[CypherCommon][Tier1][ApiContract]" )
{
    const net_address_t address{};
    const result_t result{};
    const byte_reader_t reader{};
    const bit_reader_t bitReader{};

    REQUIRE( address.family == net_address_family_t::INVALID );
    REQUIRE( Result_Succeeded( result ) );
    REQUIRE( reader.iOffset == 0u );
    REQUIRE( reader.status == byte_cursor_status_t::OK );
    REQUIRE( bitReader.iBit == 0u );
    REQUIRE( bitReader.status == bit_cursor_status_t::OK );
}

TEST_CASE( "Tier1 processor vocabulary aliases the Tier0 detector",
           "[CypherCommon][Tier1][ApiContract][ProcessorDetect]" )
{
    STATIC_REQUIRE( is_same_v<processor_info_t, cy_cpu_detect_info_t> );
    STATIC_REQUIRE( is_same_v<processor_vendor_t, cy_cpu_vendor_t> );
    STATIC_REQUIRE( is_same_v<
        processor_feature_flags_t,
        cy_cpu_feature_flags_t> );
}
