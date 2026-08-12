//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Function_Tests.cpp
//  Purpose: Tests allocator-aware owning callables.
//  Details: Covers inline and heap storage, over-alignment, reference arguments,
//           allocation rollback, reset, explicit movement, and automatic cleanup.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Function.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct function_allocator_state_t {
    usize cAllocations{ 0u };
    usize cFrees{ 0u };
    bool_t bFailAllocations{ CY_FALSE };
};

void *FunctionAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<function_allocator_state_t *>( pUserData );
    if ( pState->bFailAllocations ) {
        return nullptr;
    }
    ++pState->cAllocations;
    return Allocator_Allocate( Allocator_GetSystem(), cbSize, nAlignment );
}

void FunctionFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<function_allocator_state_t *>( pUserData );
    ++pState->cFrees;
    Allocator_Free( Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

allocator_t MakeFunctionAllocator(
    function_allocator_state_t *pState ) noexcept
{
    return { FunctionAllocate, nullptr, FunctionFree, pState };
}

struct large_callable_t {
    byte padding[96]{};
    i32 nBias{ 0 };

    i32 operator()( i32 value ) noexcept
    {
        return value + nBias + static_cast<i32>( padding[0] );
    }
};

struct alignas( 128 ) aligned_callable_t {
    i32 nBias{ 0 };

    i32 operator()( i32 value ) noexcept
    {
        return value + nBias;
    }
};

} // namespace

TEST_CASE( "Function stores small callables inline without allocation",
           "[CypherCommon][Tier1][Function]" )
{
    function_allocator_state_t state{};
    const allocator_t allocator = MakeFunctionAllocator( &state );
    function_t<i32( i32 )> function{};
    REQUIRE( Function_Init( &function, &allocator ) );
    REQUIRE( Function_IsValid( &function ) );
    REQUIRE_FALSE(
        Function_IsValid(
            static_cast<const function_t<i32( i32 )> *>( nullptr ) ) );

    const i32 nBias = 7;
    REQUIRE( Function_Bind(
        &function,
        [nBias]( i32 value ) noexcept { return value + nBias; } ) );
    REQUIRE( Function_IsBound( function ) );
    REQUIRE( Function_UsesInlineStorage( function ) );
    REQUIRE( Function_Invoke( function, 35 ) == 42 );
    REQUIRE( state.cAllocations == 0u );

    Function_Reset( &function );
    REQUIRE_FALSE( Function_IsBound( function ) );
    REQUIRE( Function_IsInitialized( function ) );
    REQUIRE( state.cFrees == 0u );
}

TEST_CASE( "Function allocates and releases large and over-aligned callables",
           "[CypherCommon][Tier1][Function]" )
{
    function_allocator_state_t state{};
    const allocator_t allocator = MakeFunctionAllocator( &state );
    {
        function_t<i32( i32 )> largeFunction{};
        REQUIRE( Function_Init( &largeFunction, &allocator ) );
        REQUIRE( Function_Bind( &largeFunction, large_callable_t{ {}, 9 } ) );
        REQUIRE_FALSE( Function_UsesInlineStorage( largeFunction ) );
        REQUIRE( Function_Invoke( largeFunction, 33 ) == 42 );

        function_t<i32( i32 )> alignedFunction{};
        REQUIRE( Function_Init( &alignedFunction, &allocator ) );
        REQUIRE( Function_Bind( &alignedFunction, aligned_callable_t{ 11 } ) );
        REQUIRE_FALSE( Function_UsesInlineStorage( alignedFunction ) );
        REQUIRE(
            reinterpret_cast<uintptr>( alignedFunction.pCallable ) % 128u == 0u );
        REQUIRE( Function_Invoke( alignedFunction, 31 ) == 42 );
    }
    REQUIRE( state.cAllocations == 2u );
    REQUIRE( state.cFrees == 2u );
}

TEST_CASE( "Function failed heap rebinding preserves the previous callable",
           "[CypherCommon][Tier1][Function]" )
{
    function_allocator_state_t state{};
    const allocator_t allocator = MakeFunctionAllocator( &state );
    function_t<i32( i32 )> function{};
    REQUIRE( Function_Init( &function, &allocator ) );
    REQUIRE( Function_Bind(
        &function,
        []( i32 value ) noexcept { return value * 2; } ) );

    state.bFailAllocations = CY_TRUE;
    large_callable_t large{};
    large.nBias = 17;
    REQUIRE_FALSE( Function_Bind( &function, large ) );
    REQUIRE( Function_UsesInlineStorage( function ) );
    REQUIRE( Function_Invoke( function, 21 ) == 42 );
    REQUIRE( state.cAllocations == 0u );
}

TEST_CASE( "Function explicit move transfers inline and heap ownership",
           "[CypherCommon][Tier1][Function]" )
{
    function_allocator_state_t state{};
    const allocator_t allocator = MakeFunctionAllocator( &state );

    function_t<i32( i32 )> inlineSource{};
    function_t<i32( i32 )> inlineDestination{};
    REQUIRE( Function_Init( &inlineSource, &allocator ) );
    REQUIRE( Function_Init( &inlineDestination, &allocator ) );
    REQUIRE( Function_Bind(
        &inlineSource,
        []( i32 value ) noexcept { return value + 1; } ) );
    REQUIRE( Function_Move( &inlineDestination, &inlineSource ) );
    REQUIRE_FALSE( Function_IsBound( inlineSource ) );
    REQUIRE( Function_UsesInlineStorage( inlineDestination ) );
    REQUIRE( Function_Invoke( inlineDestination, 41 ) == 42 );

    function_t<i32( i32 )> heapSource{};
    function_t<i32( i32 )> heapDestination{};
    REQUIRE( Function_Init( &heapSource, &allocator ) );
    REQUIRE( Function_Init( &heapDestination, &allocator ) );
    REQUIRE( Function_Bind( &heapSource, large_callable_t{ {}, 5 } ) );
    void *pAllocation = heapSource.pCallable;
    REQUIRE( Function_Move( &heapDestination, &heapSource ) );
    REQUIRE_FALSE( Function_IsBound( heapSource ) );
    REQUIRE( heapDestination.pCallable == pAllocation );
    REQUIRE( Function_Invoke( heapDestination, 37 ) == 42 );
}

TEST_CASE( "Function forwards reference parameters through erased dispatch",
           "[CypherCommon][Tier1][Function]" )
{
    function_t<void( i32 & )> function{};
    REQUIRE( Function_Init( &function, Allocator_GetSystem() ) );
    REQUIRE( Function_Bind(
        &function,
        []( i32 &value ) noexcept { value += 5; } ) );

    i32 value = 37;
    Function_Invoke( function, value );
    REQUIRE( value == 42 );
}
