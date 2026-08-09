//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Blob_Tests.cpp
//  Purpose: Tests allocator-backed owning byte storage.
//  Details: Protects growth, fill, overlapping operations, allocation rollback,
//           ownership transfer, mutable views, and allocator provenance.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Blob.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

void *FailBlobAllocation( void *, usize, usize ) noexcept
{
    return nullptr;
}

u32 g_blobAssertCount = 0u;

assert_action_t CaptureBlobAssert( const assert_info_t & ) noexcept
{
    ++g_blobAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Blob initializes, grows, fills, and exposes consistent views",
           "[CypherCommon][Tier1][Blob]" )
{
    blob_t blob{};
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem(), 4u ) );
    REQUIRE( Blob_IsValid( &blob ) );
    REQUIRE( Blob_IsEmpty( &blob ) );
    REQUIRE( Blob_Capacity( &blob ) == 4u );

    REQUIRE( Blob_Resize( &blob, 6u, 0xA5u ) );
    REQUIRE( Blob_Size( &blob ) == 6u );
    REQUIRE( Blob_Capacity( &blob ) >= 6u );
    for ( usize iByte = 0u; iByte < 6u; ++iByte ) {
        REQUIRE( Blob_Data( &blob )[iByte] == 0xA5u );
    }

    byte_span_t writable = Blob_WritableSpan( &blob );
    REQUIRE( writable.nCount == 6u );
    writable.pData[2] = 0x3Cu;
    const binary_block_t block = Blob_Block( &blob );
    REQUIRE( block.pData == Blob_Data( &blob ) );
    REQUIRE( block.pData[2] == 0x3Cu );
}

TEST_CASE( "Blob append and assign support overlapping internal ranges",
           "[CypherCommon][Tier1][Blob]" )
{
    blob_t blob{};
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem(), 4u ) );
    const byte initial[] = { 1u, 2u, 3u, 4u };
    REQUIRE( Blob_Assign(
        &blob,
        BinaryBlock_FromData( initial, sizeof( initial ) ) ) );

    REQUIRE( Blob_Append(
        &blob,
        BinaryBlock_FromData( blob.pData + 1u, 3u ) ) );
    const byte appended[] = { 1u, 2u, 3u, 4u, 2u, 3u, 4u };
    REQUIRE( Blob_Size( &blob ) == sizeof( appended ) );
    REQUIRE( Cy_MemEqual( blob.pData, appended, sizeof( appended ) ) );

    REQUIRE( Blob_Assign(
        &blob,
        BinaryBlock_FromData( blob.pData + 2u, 4u ) ) );
    const byte assigned[] = { 3u, 4u, 2u, 3u };
    REQUIRE( Blob_Size( &blob ) == sizeof( assigned ) );
    REQUIRE( Cy_MemEqual( blob.pData, assigned, sizeof( assigned ) ) );
}

TEST_CASE( "Blob failed growth leaves allocation and contents unchanged",
           "[CypherCommon][Tier1][Blob]" )
{
    allocator_t allocator = *Allocator_GetSystem();
    blob_t blob{};
    REQUIRE( Blob_Init( &blob, &allocator, 4u ) );
    const byte initial[] = { 5u, 7u, 11u, 13u };
    REQUIRE( Blob_Assign(
        &blob,
        BinaryBlock_FromData( initial, sizeof( initial ) ) ) );
    byte *pOriginalData = blob.pData;

    allocator.pfnAllocate = FailBlobAllocation;
    REQUIRE_FALSE( Blob_Resize( &blob, 128u, 0xFFu ) );
    REQUIRE_FALSE( Blob_Reserve( &blob, 256u ) );
    const byte extra[] = { 17u };
    REQUIRE_FALSE( Blob_Append(
        &blob,
        BinaryBlock_FromData( extra, sizeof( extra ) ) ) );
    REQUIRE( blob.pData == pOriginalData );
    REQUIRE( Blob_Size( &blob ) == sizeof( initial ) );
    REQUIRE( Cy_MemEqual( blob.pData, initial, sizeof( initial ) ) );

    allocator.pfnAllocate = Allocator_GetSystem()->pfnAllocate;
}

TEST_CASE( "Blob clear retains capacity and allocator for reuse",
           "[CypherCommon][Tier1][Blob]" )
{
    blob_t blob{};
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem(), 32u ) );
    REQUIRE( Blob_Resize( &blob, 16u, 1u ) );
    byte *pData = blob.pData;

    Blob_Clear( &blob );
    REQUIRE( Blob_IsEmpty( &blob ) );
    REQUIRE( Blob_Capacity( &blob ) == 32u );
    REQUIRE( Blob_Data( &blob ) == pData );
    REQUIRE( Blob_Resize( &blob, 8u, 2u ) );
    REQUIRE( Blob_Data( &blob ) == pData );
}

TEST_CASE( "Blob move and release transfer ownership destructively",
           "[CypherCommon][Tier1][Blob]" )
{
    blob_t source{};
    blob_t destination{};
    REQUIRE( Blob_Init( &source, Allocator_GetSystem(), 32u ) );
    REQUIRE( Blob_Resize( &source, 5u, 0x44u ) );
    byte *pOriginalData = source.pData;

    Blob_Move( &destination, &source );
    REQUIRE( source.pData == nullptr );
    REQUIRE( source.pAllocator == nullptr );
    REQUIRE( destination.pData == pOriginalData );

    usize cbLogicalSize = 0u;
    owned_allocation_t allocation = Blob_Release(
        &destination,
        &cbLogicalSize );
    REQUIRE( cbLogicalSize == 5u );
    REQUIRE( allocation.pData == pOriginalData );
    REQUIRE( allocation.cbSize == 32u );
    REQUIRE( allocation.pAllocator == Allocator_GetSystem() );
    REQUIRE( destination.pData == nullptr );
    REQUIRE( destination.pAllocator == nullptr );
    Allocator_FreeOwned( &allocation );
    REQUIRE( Allocator_OwnedIsValid( &allocation ) );
}

TEST_CASE( "Blob canonical empty release is valid and carries no allocation",
           "[CypherCommon][Tier1][Blob]" )
{
    blob_t blob{};
    REQUIRE( Blob_Init( &blob, Allocator_GetSystem() ) );
    usize cbLogicalSize = 99u;
    owned_allocation_t allocation = Blob_Release( &blob, &cbLogicalSize );
    REQUIRE( cbLogicalSize == 0u );
    REQUIRE( allocation.pData == nullptr );
    REQUIRE( blob.pAllocator == nullptr );
}

TEST_CASE( "Blob invalid operations assert and fail safely",
           "[CypherCommon][Tier1][Blob]" )
{
    g_blobAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureBlobAssert );

    blob_t blob{};
    REQUIRE_FALSE( Blob_Init( nullptr, Allocator_GetSystem() ) );
    REQUIRE_FALSE( Blob_Reserve( &blob, 1u ) );
    REQUIRE( Blob_Data( static_cast<blob_t *>( nullptr ) ) == nullptr );
    REQUIRE_FALSE( Blob_Assign( &blob, { nullptr, 1u } ) );
    Blob_Move( &blob, &blob );
    REQUIRE( Blob_Release( nullptr ).pData == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_blobAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
