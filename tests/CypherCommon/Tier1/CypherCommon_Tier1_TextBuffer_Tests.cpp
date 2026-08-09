//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_TextBuffer_Tests.cpp
//  Purpose: Tests allocator-backed mutable text storage.
//  Details: Protects terminators, growth, aliased edits, allocation rollback,
//           embedded bytes, ownership transfer, and invalid-call behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_TextBuffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

void *FailTextAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_textBufferAssertCount = 0u;

assert_action_t CaptureTextBufferAssert( const assert_info_t & ) noexcept
{
    ++g_textBufferAssertCount;
    return assert_action_t::Continue;
}

void RequireTextEquals( const text_buffer_t &buffer, const char *pExpected )
{
    REQUIRE( TextBuffer_IsValid( &buffer ) );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &buffer ),
        StringView_FromCString( pExpected ) ) );
    REQUIRE( buffer.pData[buffer.cchLength] == '\0' );
}

} // namespace

TEST_CASE( "TextBuffer initializes with a non-null empty C string",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_IsValid( &buffer ) );
    REQUIRE( TextBuffer_IsEmpty( &buffer ) );
    REQUIRE( TextBuffer_Length( &buffer ) == 0u );
    REQUIRE( TextBuffer_Capacity( &buffer ) == 0u );
    REQUIRE( TextBuffer_Data( &buffer ) == nullptr );
    REQUIRE( TextBuffer_CStr( &buffer )[0] == '\0' );
}

TEST_CASE( "TextBuffer assign append resize and pop preserve termination",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem(), 2u ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "cy" ) ) );
    REQUIRE( TextBuffer_Append(
        &buffer,
        StringView_FromCString( "pher" ) ) );
    REQUIRE( TextBuffer_AppendChar( &buffer, '!' ) );
    RequireTextEquals( buffer, "cypher!" );

    REQUIRE( TextBuffer_PopBack( &buffer ) );
    RequireTextEquals( buffer, "cypher" );

    REQUIRE( TextBuffer_Resize( &buffer, 8u, '_' ) );
    RequireTextEquals( buffer, "cypher__" );
    REQUIRE( TextBuffer_Resize( &buffer, 3u ) );
    RequireTextEquals( buffer, "cyp" );
}

TEST_CASE( "TextBuffer insert erase and replace edit bounded ranges",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "shaderworld" ) ) );
    REQUIRE( TextBuffer_Insert(
        &buffer,
        6u,
        StringView_FromCString( "/" ) ) );
    RequireTextEquals( buffer, "shader/world" );

    REQUIRE( TextBuffer_Replace(
        &buffer,
        7u,
        5u,
        StringView_FromCString( "weapon" ) ) );
    RequireTextEquals( buffer, "shader/weapon" );

    REQUIRE( TextBuffer_Erase( &buffer, 6u, 1u ) );
    RequireTextEquals( buffer, "shaderweapon" );
}

TEST_CASE( "TextBuffer preserves aliases through growth and structural edits",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem(), 4u ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "abcd" ) ) );

    const string_view_t self = TextBuffer_View( &buffer );
    REQUIRE( TextBuffer_Append( &buffer, self ) );
    RequireTextEquals( buffer, "abcdabcd" );

    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "abcdef" ) ) );
    const string_view_t crossingSource =
        StringView_FromRange( buffer.pData + 1u, 3u );
    REQUIRE( TextBuffer_Insert( &buffer, 3u, crossingSource ) );
    RequireTextEquals( buffer, "abcbcddef" );

    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "012345" ) ) );
    const string_view_t trailingSource =
        StringView_FromRange( buffer.pData + 4u, 2u );
    REQUIRE( TextBuffer_Replace( &buffer, 1u, 3u, trailingSource ) );
    RequireTextEquals( buffer, "04545" );
}

TEST_CASE( "TextBuffer is length-aware with embedded null bytes",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    const char text[] = { 'a', '\0', 'b' };
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem() ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromRange( text, 3u ) ) );
    REQUIRE( TextBuffer_Length( &buffer ) == 3u );
    REQUIRE( buffer.pData[0] == 'a' );
    REQUIRE( buffer.pData[1] == '\0' );
    REQUIRE( buffer.pData[2] == 'b' );
    REQUIRE( buffer.pData[3] == '\0' );
}

TEST_CASE( "TextBuffer failed allocation leaves ownership and bytes unchanged",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, &allocator, 4u ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "data" ) ) );
    char *pOriginalData = buffer.pData;
    const usize cchOriginalCapacity = buffer.cchCapacity;

    allocator.pfnAllocate = FailTextAllocation;
    REQUIRE_FALSE( TextBuffer_Append(
        &buffer,
        StringView_FromCString( "/shaders" ) ) );
    REQUIRE_FALSE( TextBuffer_Reserve( &buffer, 512u ) );
    RequireTextEquals( buffer, "data" );
    REQUIRE( buffer.pData == pOriginalData );
    REQUIRE( buffer.cchCapacity == cchOriginalCapacity );

    const string_view_t internal =
        StringView_FromRange( buffer.pData + 1u, 2u );
    REQUIRE_FALSE( TextBuffer_Insert( &buffer, 2u, internal ) );
    RequireTextEquals( buffer, "data" );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "TextBuffer clear and shrink release unused storage",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t buffer{};
    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem(), 128u ) );
    REQUIRE( TextBuffer_Assign(
        &buffer,
        StringView_FromCString( "asset" ) ) );
    REQUIRE( TextBuffer_ShrinkToFit( &buffer ) );
    REQUIRE( buffer.cchCapacity == 5u );
    RequireTextEquals( buffer, "asset" );

    TextBuffer_Clear( &buffer );
    REQUIRE( TextBuffer_IsEmpty( &buffer ) );
    REQUIRE( buffer.cchCapacity == 5u );
    REQUIRE( TextBuffer_ShrinkToFit( &buffer ) );
    REQUIRE( buffer.pData == nullptr );
    REQUIRE( buffer.cchCapacity == 0u );
    REQUIRE( TextBuffer_CStr( &buffer )[0] == '\0' );
}

TEST_CASE( "TextBuffer move and release transfer allocator provenance",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    text_buffer_t source{};
    text_buffer_t destination{};
    REQUIRE( TextBuffer_Init( &source, Allocator_GetSystem(), 32u ) );
    REQUIRE( TextBuffer_Assign(
        &source,
        StringView_FromCString( "material" ) ) );
    char *pOriginalData = source.pData;

    TextBuffer_Move( &destination, &source );
    REQUIRE( source.pData == nullptr );
    REQUIRE( source.pAllocator == nullptr );
    REQUIRE( destination.pData == pOriginalData );

    usize cchLength = 0u;
    owned_allocation_t allocation =
        TextBuffer_Release( &destination, &cchLength );
    REQUIRE( cchLength == 8u );
    REQUIRE( allocation.pData == pOriginalData );
    REQUIRE( allocation.cbSize == 33u );
    REQUIRE( allocation.pAllocator == Allocator_GetSystem() );
    REQUIRE( destination.pData == nullptr );
    REQUIRE( destination.pAllocator == nullptr );
    Allocator_FreeOwned( &allocation );
}

TEST_CASE( "TextBuffer invalid calls assert and fail safely",
           "[CypherCommon][Tier1][TextBuffer]" )
{
    g_textBufferAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureTextBufferAssert );

    REQUIRE_FALSE( TextBuffer_Init( nullptr, Allocator_GetSystem() ) );
    text_buffer_t buffer{};
    REQUIRE_FALSE( TextBuffer_Reserve( &buffer, 1u ) );
    REQUIRE( TextBuffer_Data(
        static_cast<text_buffer_t *>( nullptr ) ) == nullptr );
    REQUIRE( TextBuffer_Release( nullptr ).pData == nullptr );
    TextBuffer_Move( &buffer, &buffer );

    REQUIRE( TextBuffer_Init( &buffer, Allocator_GetSystem() ) );
    REQUIRE_FALSE( TextBuffer_Replace(
        &buffer,
        1u,
        0u,
        StringView_FromCString( "x" ) ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_textBufferAssertCount ==
        7u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
