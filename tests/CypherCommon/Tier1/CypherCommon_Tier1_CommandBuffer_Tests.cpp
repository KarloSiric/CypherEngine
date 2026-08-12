//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_CommandBuffer_Tests.cpp
//  Purpose: Tests allocator-backed command line queue behavior.
//  Details: Covers FIFO ordering, borrowed views, compaction, aliased growth,
//           allocation rollback, command validation, and invalid-call handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_CommandBuffer.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_commandBufferAssertCount = 0u;

assert_action_t CaptureCommandBufferAssert( const assert_info_t & ) noexcept
{
    ++g_commandBufferAssertCount;
    return assert_action_t::Continue;
}

void *FailCommandBufferAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

void RequireCommandEquals(
    string_view_t command,
    const char *pExpected )
{
    REQUIRE( StringView_Equals(
        command,
        StringView_FromCString( pExpected ) ) );
}

} // namespace

TEST_CASE( "CommandBuffer enqueues peeks and pops commands in FIFO order",
           "[CypherCommon][Tier1][CommandBuffer]" )
{
    command_buffer_t buffer{};
    REQUIRE( CommandBuffer_Init( &buffer, Allocator_GetSystem(), 64u ) );
    REQUIRE( CommandBuffer_IsValid( &buffer ) );
    REQUIRE( CommandBuffer_IsEmpty( &buffer ) );

    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "echo first" ) ) );
    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "map arena" ) ) );
    REQUIRE( CommandBuffer_Count( &buffer ) == 2u );
    REQUIRE( CommandBuffer_PendingBytes( &buffer ) == 21u );

    string_view_t command{};
    REQUIRE( CommandBuffer_Peek( &buffer, &command ) );
    RequireCommandEquals( command, "echo first" );
    const char *pFirstCommand = command.pData;

    REQUIRE( CommandBuffer_Pop( &buffer, &command ) );
    REQUIRE( command.pData == pFirstCommand );
    RequireCommandEquals( command, "echo first" );
    REQUIRE( CommandBuffer_Count( &buffer ) == 1u );

    REQUIRE( CommandBuffer_Pop( &buffer, &command ) );
    RequireCommandEquals( command, "map arena" );
    REQUIRE( CommandBuffer_IsEmpty( &buffer ) );
    REQUIRE( CommandBuffer_PendingBytes( &buffer ) == 0u );
    REQUIRE_FALSE( CommandBuffer_Peek( &buffer, &command ) );
    REQUIRE_FALSE( CommandBuffer_Pop( &buffer, &command ) );

    CommandBuffer_Compact( &buffer );
    REQUIRE( buffer.text.cchLength == 0u );
    REQUIRE( buffer.iReadOffset == 0u );

    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "echo reused" ) ) );
    CommandBuffer_Clear( &buffer );
    REQUIRE( CommandBuffer_IsEmpty( &buffer ) );
    REQUIRE( CommandBuffer_IsValid( &buffer ) );

    CommandBuffer_Shutdown( &buffer );
    REQUIRE( CommandBuffer_IsValid( &buffer ) );
    REQUIRE( CommandBuffer_IsEmpty( &buffer ) );
}

TEST_CASE( "CommandBuffer compaction removes only the consumed prefix",
           "[CypherCommon][Tier1][CommandBuffer]" )
{
    command_buffer_t buffer{};
    REQUIRE( CommandBuffer_Init( &buffer, Allocator_GetSystem(), 64u ) );
    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "one" ) ) );
    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "two" ) ) );
    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "three" ) ) );

    string_view_t command{};
    REQUIRE( CommandBuffer_Pop( &buffer, &command ) );
    RequireCommandEquals( command, "one" );
    REQUIRE( buffer.iReadOffset == 4u );

    CommandBuffer_Compact( &buffer );
    REQUIRE( buffer.iReadOffset == 0u );
    REQUIRE( CommandBuffer_Count( &buffer ) == 2u );
    REQUIRE( StringView_Equals(
        TextBuffer_View( &buffer.text ),
        StringView_FromCString( "two\nthree\n" ) ) );
    REQUIRE( CommandBuffer_Peek( &buffer, &command ) );
    RequireCommandEquals( command, "two" );
}

TEST_CASE( "CommandBuffer rebases an aliased command across growth",
           "[CypherCommon][Tier1][CommandBuffer]" )
{
    command_buffer_t buffer{};
    REQUIRE( CommandBuffer_Init( &buffer, Allocator_GetSystem(), 4u ) );
    const string_view_t longCommand = StringView_FromCString(
        "exec facility_survival_extremely_long_configuration_name.cfg" );
    REQUIRE( CommandBuffer_Enqueue( &buffer, longCommand ) );

    string_view_t aliasedCommand{};
    REQUIRE( CommandBuffer_Peek( &buffer, &aliasedCommand ) );
    char *pOriginalAllocation = buffer.text.pData;
    const usize cchOriginalCapacity = buffer.text.cchCapacity;
    REQUIRE( CommandBuffer_Enqueue( &buffer, aliasedCommand ) );
    REQUIRE( buffer.text.cchCapacity > cchOriginalCapacity );
    REQUIRE( buffer.text.pData != pOriginalAllocation );

    string_view_t command{};
    REQUIRE( CommandBuffer_Pop( &buffer, &command ) );
    REQUIRE( StringView_Equals( command, longCommand ) );
    REQUIRE( CommandBuffer_Pop( &buffer, &command ) );
    REQUIRE( StringView_Equals( command, longCommand ) );
}

TEST_CASE( "CommandBuffer allocation failure preserves all observable state",
           "[CypherCommon][Tier1][CommandBuffer]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    command_buffer_t buffer{};
    REQUIRE( CommandBuffer_Init( &buffer, &allocator, 8u ) );
    REQUIRE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString( "one" ) ) );

    char *pOriginalData = buffer.text.pData;
    const usize cchOriginalLength = buffer.text.cchLength;
    const usize cchOriginalCapacity = buffer.text.cchCapacity;
    const usize iOriginalReadOffset = buffer.iReadOffset;
    const usize nOriginalCommandCount = buffer.nCommandCount;

    allocator.pfnAllocate = FailCommandBufferAllocation;
    REQUIRE_FALSE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromCString(
            "command_that_requires_a_larger_allocation" ) ) );
    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;

    REQUIRE( buffer.text.pData == pOriginalData );
    REQUIRE( buffer.text.cchLength == cchOriginalLength );
    REQUIRE( buffer.text.cchCapacity == cchOriginalCapacity );
    REQUIRE( buffer.iReadOffset == iOriginalReadOffset );
    REQUIRE( buffer.nCommandCount == nOriginalCommandCount );
    string_view_t command{};
    REQUIRE( CommandBuffer_Peek( &buffer, &command ) );
    RequireCommandEquals( command, "one" );
}

TEST_CASE( "CommandBuffer rejects malformed lines and uninitialized mutation",
           "[CypherCommon][Tier1][CommandBuffer]" )
{
    g_commandBufferAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureCommandBufferAssert );

    command_buffer_t uninitialized{};
    REQUIRE_FALSE( CommandBuffer_Enqueue(
        &uninitialized,
        StringView_FromCString( "echo" ) ) );

    command_buffer_t buffer{};
    REQUIRE( CommandBuffer_Init( &buffer, Allocator_GetSystem() ) );
    REQUIRE_FALSE( CommandBuffer_Enqueue( &buffer, {} ) );

    const char embeddedNull[]{ 'a', '\0', 'b' };
    const char embeddedCarriageReturn[]{ 'a', '\r', 'b' };
    const char embeddedNewline[]{ 'a', '\n', 'b' };
    REQUIRE_FALSE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromRange( embeddedNull, 3u ) ) );
    REQUIRE_FALSE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromRange( embeddedCarriageReturn, 3u ) ) );
    REQUIRE_FALSE( CommandBuffer_Enqueue(
        &buffer,
        StringView_FromRange( embeddedNewline, 3u ) ) );
    REQUIRE_FALSE( CommandBuffer_Peek( &buffer, nullptr ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_commandBufferAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
