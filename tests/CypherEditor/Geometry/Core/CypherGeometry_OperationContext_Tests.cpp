//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_OperationContext_Tests.cpp
//  Purpose: Verifies bounded borrowed geometry operation services.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_OperationContext.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace cypher::editor::geometry
{
namespace
{

struct cancel_query_state_t {
    common::usize cPolls{ 0u };
    common::usize iCancelPoll{ common::CY_USIZE_MAX };
};

bool_t CancelOnConfiguredPoll( void *pUserData ) noexcept
{
    auto *const pState = static_cast<cancel_query_state_t *>( pUserData );
    ++pState->cPolls;
    return pState->cPolls >= pState->iCancelPoll;
}

void RequireSameContext(
    const geometry_operation_context_t &actual,
    const geometry_operation_context_t &expected )
{
    REQUIRE( actual.pPolicy == expected.pPolicy );
    REQUIRE( actual.pScratch == expected.pScratch );
    REQUIRE( actual.pDiagnostics == expected.pDiagnostics );
    REQUIRE( actual.pCancellation == expected.pCancellation );
}

} // namespace

TEST_CASE( "geometry operation context accepts explicit optional services",
           "[editor][geometry][core][operation-context]" )
{
    const geometry_policy_t policy{};
    const geometry_cancellation_t neutralCancellation{};
    geometry_operation_context_t context{};

    REQUIRE_FALSE( GeometryCancellation_IsValid( nullptr ) );
    REQUIRE( GeometryCancellation_IsValid( &neutralCancellation ) );
    REQUIRE_FALSE( GeometryCancellation_IsRequested( nullptr ) );
    REQUIRE_FALSE(
        GeometryCancellation_IsRequested( &neutralCancellation ) );

    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, nullptr, nullptr, nullptr } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_IsValid( &context ) );
    REQUIRE( context.pPolicy == &policy );
    REQUIRE( context.pScratch == nullptr );
    REQUIRE( context.pDiagnostics == nullptr );
    REQUIRE( context.pCancellation == nullptr );
    REQUIRE( GeometryOperationContext_Checkpoint( &context ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_Checkpoint( &context ) ==
             geometry_status_t::OK );

    STATIC_REQUIRE(
        std::is_trivially_copyable_v<geometry_operation_context_t> );
}

TEST_CASE( "geometry operation context borrows bounded scratch and diagnostics",
           "[editor][geometry][core][operation-context]" )
{
    geometry_policy_t policy{};
    alignas( std::max_align_t ) common::byte scratchStorage[256]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { scratchStorage, sizeof( scratchStorage ) },
                   nullptr,
                   sizeof( scratchStorage ),
                   sizeof( scratchStorage ),
                   alignof( std::max_align_t ) } ) ==
             geometry_status_t::OK );

    geometry_diagnostic_t diagnosticStorage[2]{};
    geometry_diagnostic_buffer_t diagnostics{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &diagnostics,
                 common::Span_FromArray( diagnosticStorage ) ) ==
             geometry_status_t::OK );

    common::atomic_bool_t requested{ common::CY_FALSE };
    cancel_query_state_t queryState{};
    const geometry_cancellation_t cancellation{
        &requested,
        CancelOnConfiguredPoll,
        &queryState
    };
    geometry_operation_context_t context{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, &cancellation } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_IsValid( &context ) );
    REQUIRE( context.pPolicy == &policy );
    REQUIRE( context.pScratch == &scratch );
    REQUIRE( context.pDiagnostics == &diagnostics );
    REQUIRE( context.pCancellation == &cancellation );

    geometry_scratch_stats_t before{};
    REQUIRE( GeometryScratch_QueryStats( &scratch, &before ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_Checkpoint( &context ) ==
             geometry_status_t::OK );
    REQUIRE( queryState.cPolls == 1u );

    geometry_scratch_stats_t after{};
    REQUIRE( GeometryScratch_QueryStats( &scratch, &after ) ==
             geometry_status_t::OK );
    REQUIRE( after.cbUsed == before.cbUsed );
    REQUIRE( after.cbRemaining == before.cbRemaining );
    REQUIRE( after.cbHighWater == before.cbHighWater );
    REQUIRE( GeometryDiagnosticBuffer_Count( &diagnostics ) == 0u );
    REQUIRE( GeometryDiagnosticBuffer_TruncatedCount( &diagnostics ) == 0u );

    GeometryDiagnosticBuffer_Shutdown( &diagnostics );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry cancellation checkpoints poll once in deterministic order",
           "[editor][geometry][core][operation-context]" )
{
    const geometry_policy_t policy{};
    cancel_query_state_t callbackState{ 0u, 2u };
    const geometry_cancellation_t callbackCancellation{
        nullptr,
        CancelOnConfiguredPoll,
        &callbackState
    };
    geometry_operation_context_t callbackContext{};
    REQUIRE( GeometryOperationContext_Bind(
                 &callbackContext,
                 { &policy, nullptr, nullptr, &callbackCancellation } ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryOperationContext_Checkpoint( &callbackContext ) ==
             geometry_status_t::OK );
    REQUIRE( callbackState.cPolls == 1u );
    REQUIRE( GeometryOperationContext_Checkpoint( &callbackContext ) ==
             geometry_status_t::CANCELLED );
    REQUIRE( callbackState.cPolls == 2u );
    REQUIRE( GeometryOperationContext_Checkpoint( &callbackContext ) ==
             geometry_status_t::CANCELLED );
    REQUIRE( callbackState.cPolls == 3u );

    common::atomic_bool_t requested{ common::CY_TRUE };
    cancel_query_state_t skippedCallback{ 0u, 1u };
    const geometry_cancellation_t atomicCancellation{
        &requested,
        CancelOnConfiguredPoll,
        &skippedCallback
    };
    geometry_operation_context_t atomicContext{};
    REQUIRE( GeometryOperationContext_Bind(
                 &atomicContext,
                 { &policy, nullptr, nullptr, &atomicCancellation } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_Checkpoint( &atomicContext ) ==
             geometry_status_t::CANCELLED );
    REQUIRE( skippedCallback.cPolls == 0u );

    common::Cy_AtomicStore(
        &requested,
        common::CY_FALSE,
        common::CY_MEMORY_ORDER_RELEASE );
    REQUIRE( GeometryOperationContext_Checkpoint( &atomicContext ) ==
             geometry_status_t::CANCELLED );
    REQUIRE( skippedCallback.cPolls == 1u );
}

TEST_CASE( "geometry operation context binding rejects invalid dependencies transactionally",
           "[editor][geometry][core][operation-context]" )
{
    const geometry_policy_t policy{};
    geometry_operation_context_t context{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, nullptr, nullptr, nullptr } ) ==
             geometry_status_t::OK );
    const geometry_operation_context_t before = context;

    REQUIRE( GeometryOperationContext_Bind(
                 nullptr,
                 { &policy, nullptr, nullptr, nullptr } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { nullptr, nullptr, nullptr, nullptr } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireSameContext( context, before );

    geometry_policy_t invalidPolicy{};
    invalidPolicy.limits.cDiagnosticsMax = 0u;
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &invalidPolicy, nullptr, nullptr, nullptr } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireSameContext( context, before );

    geometry_scratch_t uninitializedScratch{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy,
                   &uninitializedScratch,
                   nullptr,
                   nullptr } ) == geometry_status_t::NOT_INITIALIZED );
    RequireSameContext( context, before );

    geometry_diagnostic_buffer_t uninitializedDiagnostics{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy,
                   nullptr,
                   &uninitializedDiagnostics,
                   nullptr } ) == geometry_status_t::NOT_INITIALIZED );
    RequireSameContext( context, before );

    common::u32 staleUserData = 7u;
    const geometry_cancellation_t invalidCancellation{
        nullptr,
        nullptr,
        &staleUserData
    };
    REQUIRE_FALSE( GeometryCancellation_IsValid( &invalidCancellation ) );
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy,
                   nullptr,
                   nullptr,
                   &invalidCancellation } ) ==
             geometry_status_t::INVALID_ARGUMENT );
    RequireSameContext( context, before );
}

TEST_CASE( "geometry operation context enforces policy budgets and live validity",
           "[editor][geometry][core][operation-context]" )
{
    geometry_policy_t policy{};
    alignas( std::max_align_t ) common::byte scratchStorage[128]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { scratchStorage, sizeof( scratchStorage ) },
                   nullptr,
                   sizeof( scratchStorage ),
                   sizeof( scratchStorage ),
                   alignof( std::max_align_t ) } ) ==
             geometry_status_t::OK );

    geometry_diagnostic_t diagnosticStorage[2]{};
    geometry_diagnostic_buffer_t diagnostics{};
    REQUIRE( GeometryDiagnosticBuffer_Init(
                 &diagnostics,
                 common::Span_FromArray( diagnosticStorage ) ) ==
             geometry_status_t::OK );

    policy.limits.cbScratchMax = sizeof( scratchStorage ) - 1u;
    geometry_operation_context_t context{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, nullptr } ) ==
             geometry_status_t::LIMIT_EXCEEDED );

    policy = {};
    policy.limits.cDiagnosticsMax = 1u;
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, nullptr } ) ==
             geometry_status_t::LIMIT_EXCEEDED );

    policy = {};
    const common::usize savedAlignment = scratch.nBaseAlignment;
    scratch.nBaseAlignment = 3u;
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, nullptr } ) ==
             geometry_status_t::CORRUPT_STATE );
    scratch.nBaseAlignment = savedAlignment;

    diagnostics.cStored = diagnostics.storage.nCount + 1u;
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, nullptr } ) ==
             geometry_status_t::CORRUPT_STATE );
    diagnostics.cStored = 0u;

    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, &scratch, &diagnostics, nullptr } ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryOperationContext_IsValid( &context ) );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
    REQUIRE_FALSE( GeometryOperationContext_IsValid( &context ) );
    REQUIRE( GeometryOperationContext_Checkpoint( &context ) ==
             geometry_status_t::CORRUPT_STATE );

    GeometryDiagnosticBuffer_Shutdown( &diagnostics );
}

TEST_CASE( "geometry operation checkpoint rejects invalid contexts before polling",
           "[editor][geometry][core][operation-context]" )
{
    REQUIRE( GeometryOperationContext_Checkpoint( nullptr ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const geometry_operation_context_t unbound{};
    REQUIRE_FALSE( GeometryOperationContext_IsValid( &unbound ) );
    REQUIRE( GeometryOperationContext_Checkpoint( &unbound ) ==
             geometry_status_t::NOT_INITIALIZED );

    geometry_policy_t policy{};
    cancel_query_state_t queryState{ 0u, 1u };
    const geometry_cancellation_t cancellation{
        nullptr,
        CancelOnConfiguredPoll,
        &queryState
    };
    geometry_operation_context_t context{};
    REQUIRE( GeometryOperationContext_Bind(
                 &context,
                 { &policy, nullptr, nullptr, &cancellation } ) ==
             geometry_status_t::OK );

    policy.limits.cDiagnosticsMax = 0u;
    REQUIRE_FALSE( GeometryOperationContext_IsValid( &context ) );
    REQUIRE( GeometryOperationContext_Checkpoint( &context ) ==
             geometry_status_t::CORRUPT_STATE );
    REQUIRE( queryState.cPolls == 0u );
}

TEST_CASE( "geometry status naming includes cooperative cancellation",
           "[editor][geometry][core][operation-context]" )
{
    REQUIRE( std::string_view(
                 CypherGeometry_StatusName( geometry_status_t::OK ) ) ==
             "OK" );
    REQUIRE( std::string_view(
                 CypherGeometry_StatusName(
                     geometry_status_t::CANCELLED ) ) == "CANCELLED" );
    REQUIRE( std::string_view(
                 CypherGeometry_StatusName(
                     geometry_status_t::TRANSACTION_ACTIVE ) ) ==
             "TRANSACTION_ACTIVE" );
    REQUIRE( std::string_view(
                 CypherGeometry_StatusName( geometry_status_t::COUNT ) ) ==
             "UNKNOWN" );
    REQUIRE( std::string_view(
                 CypherGeometry_StatusName(
                     static_cast<geometry_status_t>( 0xffu ) ) ) ==
             "UNKNOWN" );
}

} // namespace cypher::editor::geometry
