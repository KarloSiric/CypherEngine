//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherCommon_Tier1_GenerationPool_Tests.cpp
//  Purpose: Verifies typed geometry pool lifetime and stale-handle contracts.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_GenerationPool.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <type_traits>

namespace cypher::common {

namespace {

struct test_record_t {
	u32 value{ 0u };
};

struct test_record_tag_t {};
struct other_record_tag_t {};
struct tracked_record_tag_t {};
struct copied_record_tag_t {};

using test_pool_t = generation_pool_t<test_record_t, test_record_tag_t>;
using test_handle_t = generation_handle_t<test_record_tag_t>;

struct tracked_record_t {
	explicit tracked_record_t( u32 *pLiveIn ) noexcept
		: pLive( pLiveIn ) {
		++*pLive;
	}

	tracked_record_t( const tracked_record_t & ) = delete;
	tracked_record_t &operator=( const tracked_record_t & ) = delete;
	tracked_record_t( tracked_record_t &&source ) noexcept
		: pLive( source.pLive ) {
		source.pLive = nullptr;
	}
	tracked_record_t &operator=( tracked_record_t && ) = delete;

	~tracked_record_t() noexcept {
		if ( pLive != nullptr ) {
			--*pLive;
		}
	}

	u32 *pLive{ nullptr };
};

struct copied_record_t {
	explicit copied_record_t( u32 *pLiveIn ) noexcept
		: pLive( pLiveIn ) {
		++*pLive;
	}

	copied_record_t( const copied_record_t &source ) noexcept
		: pLive( source.pLive ) {
		++*pLive;
	}
	copied_record_t &operator=( const copied_record_t & ) = delete;
	copied_record_t( copied_record_t && ) = delete;
	copied_record_t &operator=( copied_record_t && ) = delete;

	~copied_record_t() noexcept {
		--*pLive;
	}

	u32 *pLive{ nullptr };
};

struct failing_allocator_state_t {
	bool_t bFailAllocations{ false };
};

void *TestPoolAllocate(
	void *pUserData,
	usize cbSize,
	usize nAlignment ) noexcept {
	auto *pState = static_cast<failing_allocator_state_t *>( pUserData );
	if ( pState->bFailAllocations ) {
		return nullptr;
	}
	return Allocator_Allocate(
		Allocator_GetSystem(),
		cbSize,
		nAlignment );
}

void TestPoolFree(
	void *,
	void *pMemory,
	usize cbSize,
	usize nAlignment ) noexcept {
	Allocator_Free(
		Allocator_GetSystem(),
		pMemory,
		cbSize,
		nAlignment );
}

allocator_t MakeTestAllocator( failing_allocator_state_t *pState ) noexcept {
	return allocator_t{
		TestPoolAllocate,
		nullptr,
		TestPoolFree,
		pState
	};
}

} // namespace

TEST_CASE( "generation pool owns a bounded typed lifecycle",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};

	REQUIRE( GenerationPool_IsValid( &pool ) );
	REQUIRE_FALSE( GenerationPool_IsInitialized( &pool ) );
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 32u,
				 4u )
			 == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_IsInitialized( &pool ) );
	REQUIRE( GenerationPool_Capacity( &pool ) == 4u );
	REQUIRE( GenerationPool_Count( &pool ) == 0u );

	const auto inserted = GenerationPool_Emplace( &pool, test_record_t{ 17u } );
	REQUIRE( inserted.status == generation_pool_status_t::OK );
	REQUIRE( inserted.handle.nSlot == 0u );
	REQUIRE( inserted.handle.nGeneration == 1u );
	REQUIRE( GenerationPool_Count( &pool ) == 1u );
	REQUIRE( GenerationPool_Get( &pool, inserted.handle )->value == 17u );

	GenerationPool_Shutdown( &pool );
	REQUIRE( GenerationPool_IsValid( &pool ) );
	REQUIRE_FALSE( GenerationPool_IsInitialized( &pool ) );
}

TEST_CASE( "generation pool rejects stale handles after slot reuse",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto first = GenerationPool_Emplace( &pool, test_record_t{ 1u } );
	REQUIRE( first.status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Remove( &pool, first.handle ) == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Get( &pool, first.handle ) == nullptr );
	REQUIRE( GenerationPool_Remove( &pool, first.handle ) == generation_pool_status_t::STALE_HANDLE );

	const auto second = GenerationPool_Emplace( &pool, test_record_t{ 2u } );
	REQUIRE( second.status == generation_pool_status_t::OK );
	REQUIRE( second.handle.nSlot == first.handle.nSlot );
	REQUIRE( second.handle.nGeneration == first.handle.nGeneration + 1u );
	REQUIRE( GenerationPool_Get( &pool, second.handle )->value == 2u );
	REQUIRE( GenerationPool_Get( &pool, first.handle ) == nullptr );
}

TEST_CASE( "generation pool resolve preserves failure reasons",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	const auto beforeInit = GenerationPool_Resolve(
		&pool,
		GENERATION_HANDLE_INVALID<test_record_tag_t> );
	REQUIRE( beforeInit.status == generation_pool_status_t::NOT_INITIALIZED );
	REQUIRE( beforeInit.pValue == nullptr );

	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 1u )
			 == generation_pool_status_t::OK );
	const auto invalid = GenerationPool_Resolve(
		&pool,
		GENERATION_HANDLE_INVALID<test_record_tag_t> );
	REQUIRE( invalid.status == generation_pool_status_t::INVALID_HANDLE );
	REQUIRE( invalid.pValue == nullptr );

	const auto inserted = GenerationPool_Emplace( &pool, test_record_t{ 71u } );
	const auto live = GenerationPool_Resolve( &pool, inserted.handle );
	REQUIRE( live.status == generation_pool_status_t::OK );
	REQUIRE( live.pValue != nullptr );
	REQUIRE( live.pValue->value == 71u );

	REQUIRE( GenerationPool_Remove( &pool, inserted.handle ) == generation_pool_status_t::OK );
	const auto stale = GenerationPool_Resolve( &pool, inserted.handle );
	REQUIRE( stale.status == generation_pool_status_t::STALE_HANDLE );
	REQUIRE( stale.pValue == nullptr );
}

TEST_CASE( "generation pool clear invalidates every live handle",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 8u,
				 2u )
			 == generation_pool_status_t::OK );

	const auto first = GenerationPool_Emplace( &pool, test_record_t{ 3u } );
	const auto second = GenerationPool_Emplace( &pool, test_record_t{ 4u } );
	REQUIRE( first.status == generation_pool_status_t::OK );
	REQUIRE( second.status == generation_pool_status_t::OK );

	GenerationPool_Clear( &pool );
	REQUIRE( GenerationPool_Count( &pool ) == 0u );
	REQUIRE( GenerationPool_Get( &pool, first.handle ) == nullptr );
	REQUIRE( GenerationPool_Get( &pool, second.handle ) == nullptr );

	const auto afterClear =
		GenerationPool_Emplace( &pool, test_record_t{ 5u } );
	REQUIRE( afterClear.status == generation_pool_status_t::OK );
	REQUIRE( afterClear.handle.nSlot == 0u );
	REQUIRE( afterClear.handle.nGeneration == 2u );
}

TEST_CASE( "generation pool traverses live slots deterministically",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 8u,
				 3u )
			 == generation_pool_status_t::OK );

	const auto first = GenerationPool_Emplace( &pool, test_record_t{ 10u } );
	const auto second = GenerationPool_Emplace( &pool, test_record_t{ 20u } );
	const auto third = GenerationPool_Emplace( &pool, test_record_t{ 30u } );
	REQUIRE( first.status == generation_pool_status_t::OK );
	REQUIRE( second.status == generation_pool_status_t::OK );
	REQUIRE( third.status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Remove( &pool, second.handle ) == generation_pool_status_t::OK );

	const auto replacement =
		GenerationPool_Emplace( &pool, test_record_t{ 25u } );
	REQUIRE( replacement.handle.nSlot == second.handle.nSlot );

	std::array<u32, 3u> values{};
	usize cValues = 0u;
	const usize cVisited = GenerationPool_ForEach(
		&pool,
		[&values, &cValues]( test_handle_t, test_record_t &record ) noexcept {
			values[cValues++] = record.value;
			return true;
		} );

	REQUIRE( cVisited == 3u );
	REQUIRE( values == std::array<u32, 3u>{ 10u, 25u, 30u } );
}

TEST_CASE( "generation pool growth failure is atomic",
		   "[common][tier1][generation-pool]" ) {
	failing_allocator_state_t allocatorState{};
	const allocator_t allocator = MakeTestAllocator( &allocatorState );
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 &allocator,
				 4u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto first = GenerationPool_Emplace( &pool, test_record_t{ 41u } );
	REQUIRE( first.status == generation_pool_status_t::OK );
	test_record_t *const pBeforeFailure =
		GenerationPool_Get( &pool, first.handle );

	allocatorState.bFailAllocations = true;
	const auto failed = GenerationPool_Emplace( &pool, test_record_t{ 42u } );
	REQUIRE( failed.status == generation_pool_status_t::ALLOCATION_FAILED );
	REQUIRE_FALSE( GenerationHandle_IsValid( failed.handle ) );
	REQUIRE( GenerationPool_Count( &pool ) == 1u );
	REQUIRE( GenerationPool_Capacity( &pool ) == 1u );
	REQUIRE( GenerationPool_Get( &pool, first.handle ) == pBeforeFailure );
	REQUIRE( pBeforeFailure->value == 41u );

	allocatorState.bFailAllocations = false;
}

TEST_CASE( "generation pool growth preserves handles and object lifetime",
		   "[common][tier1][generation-pool]" ) {
	u32 cLive = 0u;
	generation_pool_t<tracked_record_t, tracked_record_tag_t> pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 16u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto first = GenerationPool_Emplace( &pool, &cLive );
	REQUIRE( first.status == generation_pool_status_t::OK );
	REQUIRE( cLive == 1u );
	REQUIRE( GenerationPool_Reserve( &pool, 8u ) == generation_pool_status_t::OK );
	REQUIRE( cLive == 1u );
	REQUIRE( GenerationPool_Contains( &pool, first.handle ) );

	const auto second = GenerationPool_Emplace( &pool, &cLive );
	REQUIRE( second.status == generation_pool_status_t::OK );
	REQUIRE( cLive == 2u );
	REQUIRE( GenerationPool_Remove( &pool, first.handle ) == generation_pool_status_t::OK );
	REQUIRE( cLive == 1u );

	GenerationPool_Clear( &pool );
	REQUIRE( cLive == 0u );
	REQUIRE( GenerationPool_IsValid( &pool ) );
}

TEST_CASE( "generation pool supports nothrow copy-only growth",
		   "[common][tier1][generation-pool]" ) {
	u32 cLive = 0u;
	generation_pool_t<copied_record_t, copied_record_tag_t> pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 8u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto inserted = GenerationPool_Emplace( &pool, &cLive );
	REQUIRE( inserted.status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Reserve( &pool, 4u ) == generation_pool_status_t::OK );
	REQUIRE( cLive == 1u );
	REQUIRE( GenerationPool_Contains( &pool, inserted.handle ) );

	GenerationPool_Shutdown( &pool );
	REQUIRE( cLive == 0u );
}

TEST_CASE( "generation pool grows from an initialized zero capacity",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 3u )
			 == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Capacity( &pool ) == 0u );

	const auto inserted = GenerationPool_Emplace( &pool, test_record_t{ 7u } );
	REQUIRE( inserted.status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Capacity( &pool ) == 3u );
	REQUIRE( GenerationPool_Get( &pool, inserted.handle )->value == 7u );
}

TEST_CASE( "generation pool validates the complete free list",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 4u )
			 == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_IsValid( &pool ) );

	const u32 iHead = pool.iFreeHead;
	const u32 iNext = pool.pSlots[iHead].iNextFree;
	pool.pSlots[iHead].iNextFree = iHead;
	REQUIRE_FALSE( GenerationPool_IsValid( &pool ) );
	REQUIRE( GenerationPool_Reserve( &pool, 4u ) == generation_pool_status_t::CORRUPT_STATE );
	pool.pSlots[iHead].iNextFree = iNext;
	REQUIRE( GenerationPool_IsValid( &pool ) );

	pool.pSlots[iHead].bRetired = true;
	REQUIRE_FALSE( GenerationPool_IsValid( &pool ) );
	pool.pSlots[iHead].bRetired = false;
	REQUIRE( GenerationPool_IsValid( &pool ) );
}

TEST_CASE( "generation pool mutations reject corrupt free-list heads atomically",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 4u )
			 == generation_pool_status_t::OK );

	const auto live = GenerationPool_Emplace( &pool, test_record_t{ 17u } );
	REQUIRE( live.status == generation_pool_status_t::OK );
	const u32 iHead = pool.iFreeHead;
	const u32 iSuccessor = pool.pSlots[iHead].iNextFree;
	REQUIRE( iHead != CY_INVALID_INDEX );
	REQUIRE( iSuccessor != CY_INVALID_INDEX );

	SECTION( "self-linked head" ) {
		const u32 iOriginalNext = pool.pSlots[iHead].iNextFree;
		pool.pSlots[iHead].iNextFree = iHead;
		const auto failed =
			GenerationPool_Emplace( &pool, test_record_t{ 23u } );
		const usize cRecordsAfter = pool.cRecords;
		const u32 iHeadAfter = pool.iFreeHead;
		const bool_t bHeadOccupiedAfter = pool.pSlots[iHead].bOccupied;
		pool.pSlots[iHead].iNextFree = iOriginalNext;

		REQUIRE( failed.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE_FALSE( GenerationHandle_IsValid( failed.handle ) );
		REQUIRE( cRecordsAfter == 1u );
		REQUIRE( iHeadAfter == iHead );
		REQUIRE_FALSE( bHeadOccupiedAfter );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "occupied successor" ) {
		const u32 iOriginalNext = pool.pSlots[iHead].iNextFree;
		pool.pSlots[iHead].iNextFree = live.handle.nSlot;
		const auto failed =
			GenerationPool_Emplace( &pool, test_record_t{ 29u } );
		const usize cRecordsAfter = pool.cRecords;
		const u32 iHeadAfter = pool.iFreeHead;
		pool.pSlots[iHead].iNextFree = iOriginalNext;

		REQUIRE( failed.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( cRecordsAfter == 1u );
		REQUIRE( iHeadAfter == iHead );
		REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 17u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "retired successor" ) {
		pool.pSlots[iSuccessor].bRetired = true;
		const auto failed =
			GenerationPool_Emplace( &pool, test_record_t{ 31u } );
		const usize cRecordsAfter = pool.cRecords;
		const u32 iHeadAfter = pool.iFreeHead;
		pool.pSlots[iSuccessor].bRetired = false;

		REQUIRE( failed.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( cRecordsAfter == 1u );
		REQUIRE( iHeadAfter == iHead );
		REQUIRE_FALSE( pool.pSlots[iHead].bOccupied );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "remove rejects a corrupt existing head before destruction" ) {
		const u32 iOriginalNext = pool.pSlots[iHead].iNextFree;
		pool.pSlots[iHead].iNextFree = iHead;
		const generation_pool_status_t failed =
			GenerationPool_Remove( &pool, live.handle );
		const usize cRecordsAfter = pool.cRecords;
		pool.pSlots[iHead].iNextFree = iOriginalNext;

		REQUIRE( failed == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( cRecordsAfter == 1u );
		REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 17u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "remove rejects a two-node free-list cycle before destruction" ) {
		const u32 iOriginalSuccessorNext =
			pool.pSlots[iSuccessor].iNextFree;
		pool.pSlots[iSuccessor].iNextFree = iHead;
		const generation_pool_status_t failed =
			GenerationPool_Remove( &pool, live.handle );
		const usize cRecordsAfter = pool.cRecords;
		pool.pSlots[iSuccessor].iNextFree = iOriginalSuccessorNext;

		REQUIRE( failed == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( cRecordsAfter == 1u );
		REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 17u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "insert rejects a full count paired with a free head" ) {
		pool.cRecords = pool.cSlots - pool.cRetiredSlots;
		const auto failed =
			GenerationPool_Emplace( &pool, test_record_t{ 37u } );
		const usize cRecordsAfter = pool.cRecords;
		const u32 iHeadAfter = pool.iFreeHead;
		const bool_t bHeadOccupiedAfter = pool.pSlots[iHead].bOccupied;
		pool.cRecords = 1u;

		REQUIRE( failed.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( cRecordsAfter == pool.cSlots - pool.cRetiredSlots );
		REQUIRE( iHeadAfter == iHead );
		REQUIRE_FALSE( bHeadOccupiedAfter );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}
}

TEST_CASE( "generation pool checked resolve rejects impossible live metadata",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 4u )
			 == generation_pool_status_t::OK );
	const auto live = GenerationPool_Emplace( &pool, test_record_t{ 41u } );
	REQUIRE( live.status == generation_pool_status_t::OK );
	const u32 iLive = live.handle.nSlot;

	SECTION( "occupied and retired" ) {
		pool.pSlots[iLive].bRetired = true;
		const auto resolved = GenerationPool_Resolve( &pool, live.handle );
		const generation_pool_status_t removed =
			GenerationPool_Remove( &pool, live.handle );
		pool.pSlots[iLive].bRetired = false;

		REQUIRE( resolved.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( resolved.pValue == nullptr );
		REQUIRE( removed == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 41u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "occupied and linked as free" ) {
		pool.pSlots[iLive].iNextFree = pool.iFreeHead;
		const auto resolved = GenerationPool_Resolve( &pool, live.handle );
		const generation_pool_status_t removed =
			GenerationPool_Remove( &pool, live.handle );
		pool.pSlots[iLive].iNextFree = CY_INVALID_INDEX;

		REQUIRE( resolved.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( removed == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( GenerationPool_Count( &pool ) == 1u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}

	SECTION( "occupied with a zero live count" ) {
		pool.cRecords = 0u;
		const auto resolved = GenerationPool_Resolve( &pool, live.handle );
		const generation_pool_status_t removed =
			GenerationPool_Remove( &pool, live.handle );
		pool.cRecords = 1u;

		REQUIRE( resolved.status == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( removed == generation_pool_status_t::CORRUPT_STATE );
		REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 41u );
		REQUIRE( GenerationPool_IsValid( &pool ) );
	}
}

TEST_CASE( "generation pool deep validation requires terminal retired generations",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 2u,
				 2u )
			 == generation_pool_status_t::OK );

	const u32 iRetired = pool.iFreeHead;
	const u32 iRemainingFree = pool.pSlots[iRetired].iNextFree;
	pool.iFreeHead = iRemainingFree;
	pool.pSlots[iRetired].iNextFree = CY_INVALID_INDEX;
	pool.pSlots[iRetired].bRetired = true;
	pool.pSlots[iRetired].nGeneration = 2u;
	pool.cRetiredSlots = 1u;

	REQUIRE_FALSE( GenerationPool_IsValid( &pool ) );
	pool.pSlots[iRetired].nGeneration = CY_U32_MAX;
	REQUIRE( GenerationPool_IsValid( &pool ) );
}

TEST_CASE( "generation pool growth consumes self-aliased copy input before relocation",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 2u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto original = GenerationPool_Emplace( &pool, test_record_t{ 73u } );
	REQUIRE( original.status == generation_pool_status_t::OK );
	const test_record_t *pAliased = GenerationPool_Get( &pool, original.handle );
	REQUIRE( pAliased != nullptr );

	const auto copied = GenerationPool_Insert( &pool, *pAliased );
	REQUIRE( copied.status == generation_pool_status_t::OK );
	REQUIRE( copied.handle.nSlot != original.handle.nSlot );
	REQUIRE( GenerationPool_Count( &pool ) == 2u );
	REQUIRE( GenerationPool_Get( &pool, original.handle )->value == 73u );
	REQUIRE( GenerationPool_Get( &pool, copied.handle )->value == 73u );
	REQUIRE( GenerationPool_IsValid( &pool ) );
}

TEST_CASE( "generation pool const traversal stops deterministically",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 3u )
			 == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Emplace( &pool, test_record_t{ 11u } ).status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Emplace( &pool, test_record_t{ 22u } ).status == generation_pool_status_t::OK );

	const test_pool_t &constPool = pool;
	u32 nFirstValue = 0u;
	const usize cVisited = GenerationPool_ForEach(
		&constPool,
		[&nFirstValue]( test_handle_t, const test_record_t &record ) noexcept {
			nFirstValue = record.value;
			return false;
		} );
	REQUIRE( cVisited == 1u );
	REQUIRE( nFirstValue == 11u );
}

TEST_CASE( "generation pool enforces limits and retires exhausted slots",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 1u,
				 1u )
			 == generation_pool_status_t::OK );

	const auto inserted = GenerationPool_Emplace( &pool, test_record_t{ 99u } );
	REQUIRE( inserted.status == generation_pool_status_t::OK );

	// White-box setup reaches the terminal generation without billions of
	// remove/insert cycles. The live handle must match the modified slot.
	pool.pSlots[inserted.handle.nSlot].nGeneration = CY_U32_MAX;
	const test_handle_t terminalHandle{
		inserted.handle.nSlot,
		CY_U32_MAX
	};
	REQUIRE( GenerationPool_Remove( &pool, terminalHandle ) == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_RetiredCount( &pool ) == 1u );
	REQUIRE( GenerationPool_Get( &pool, terminalHandle ) == nullptr );

	const auto exhausted =
		GenerationPool_Emplace( &pool, test_record_t{ 100u } );
	REQUIRE( exhausted.status == generation_pool_status_t::LIMIT_EXCEEDED );
	REQUIRE_FALSE( GenerationHandle_IsValid( exhausted.handle ) );
}

TEST_CASE( "generation pool growth preserves retired slots",
		   "[common][tier1][generation-pool]" ) {
	test_pool_t pool{};
	REQUIRE( GenerationPool_Init(
				 &pool,
				 Allocator_GetSystem(),
				 4u,
				 2u )
			 == generation_pool_status_t::OK );

	const auto terminal = GenerationPool_Emplace( &pool, test_record_t{ 1u } );
	pool.pSlots[terminal.handle.nSlot].nGeneration = CY_U32_MAX;
	const test_handle_t terminalHandle{ terminal.handle.nSlot, CY_U32_MAX };
	REQUIRE( GenerationPool_Remove( &pool, terminalHandle ) == generation_pool_status_t::OK );

	const auto live = GenerationPool_Emplace( &pool, test_record_t{ 2u } );
	REQUIRE( live.status == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_Reserve( &pool, 4u ) == generation_pool_status_t::OK );
	REQUIRE( GenerationPool_RetiredCount( &pool ) == 1u );
	REQUIRE( pool.pSlots[terminalHandle.nSlot].bRetired );
	REQUIRE( pool.pSlots[terminalHandle.nSlot].nGeneration == CY_U32_MAX );
	REQUIRE( GenerationPool_Get( &pool, live.handle )->value == 2u );
}

TEST_CASE( "generation pool initialization failure restores empty state",
		   "[common][tier1][generation-pool]" ) {
	failing_allocator_state_t allocatorState{ true };
	const allocator_t allocator = MakeTestAllocator( &allocatorState );
	test_pool_t pool{};

	REQUIRE( GenerationPool_Init( &pool, &allocator, 8u, 4u ) == generation_pool_status_t::ALLOCATION_FAILED );
	REQUIRE( GenerationPool_IsValid( &pool ) );
	REQUIRE_FALSE( GenerationPool_IsInitialized( &pool ) );
}

TEST_CASE( "generation pool tags make incompatible handles distinct",
		   "[common][tier1][generation-pool]" ) {
	STATIC_REQUIRE( !std::is_same_v<
					generation_handle_t<test_record_tag_t>,
					generation_handle_t<other_record_tag_t>> );
	STATIC_REQUIRE( sizeof( generation_handle_t<test_record_tag_t> ) == sizeof( u32 ) * 2u );
}

} // namespace cypher::common
