//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherCommon_GenerationPool.h
//  Purpose: Declares tagged slot storage addressed by wide generation handles.
//  Details: Pool tags prevent cross-family handle use. Removed slots advance
//           their generation and retire permanently before generation wrap.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_GENERATION_POOL_H
#define CYPHER_COMMON_TIER1_GENERATION_POOL_H
#ifndef PRAGMA_ONCE
#pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common {

// CY_INVALID_INDEX remains reserved, so the largest valid slot is one smaller.
inline constexpr usize CY_GENERATION_POOL_MAX_CAPACITY =
	static_cast<usize>( CY_INVALID_INDEX );

enum class generation_pool_status_t : u8 {
	OK = 0u,
	INVALID_ARGUMENT,
	NOT_INITIALIZED,
	ALREADY_INITIALIZED,
	INVALID_HANDLE,
	STALE_HANDLE,
	LIMIT_EXCEEDED,
	ALLOCATION_FAILED,
	CORRUPT_STATE,
	COUNT
};

template <typename pool_tag_t>
struct generation_handle_t {
	u32 nSlot{ CY_INVALID_INDEX };
	u32 nGeneration{ 0u };
};

template <typename pool_tag_t>
inline constexpr generation_handle_t<pool_tag_t> GENERATION_HANDLE_INVALID{};

template <typename pool_tag_t>
[[nodiscard]] constexpr bool_t GenerationHandle_IsValid(
	generation_handle_t<pool_tag_t> handle ) noexcept {
	return handle.nSlot != CY_INVALID_INDEX && handle.nGeneration != 0u;
}

template <typename pool_tag_t>
struct generation_pool_handle_result_t {
	generation_pool_status_t status{ generation_pool_status_t::INVALID_HANDLE };
	generation_handle_t<pool_tag_t> handle{};
};

template <typename value_t>
struct generation_pool_resolve_result_t {
	generation_pool_status_t status{ generation_pool_status_t::INVALID_HANDLE };
	value_t *pValue{ nullptr };
};

template <typename record_t>
struct generation_pool_slot_t {
	alignas( record_t ) byte storage[sizeof( record_t )];
	u32 nGeneration{ 1u };
	u32 iNextFree{ CY_INVALID_INDEX };
	bool_t bOccupied{ false };
	bool_t bRetired{ false };
};

// A pool belongs to one owner and one record family. Handles do not carry an
// owner ID, so resolving one against a different pool or a later pool lifetime
// is a caller contract violation. The allocator descriptor and backend state
// must remain valid and immutable until shutdown completes.
//
// Mutable access is single-writer. External synchronization is required around
// concurrent reads and writes; immutable snapshots are the intended cross-thread
// boundary. Reserve and growth invalidate every record pointer. Remove invalidates
// the removed record pointer, and Clear/Shutdown invalidate all record pointers.
template <typename record_t, typename pool_tag_t>
struct generation_pool_t {
	generation_pool_t() noexcept = default;
	CYPHER_NO_COPY_MOVE( generation_pool_t );
	~generation_pool_t() noexcept;

	generation_pool_slot_t<record_t> *pSlots{ nullptr };
	usize cRecords{ 0u };
	usize cSlots{ 0u };
	usize cRetiredSlots{ 0u };
	usize cSlotLimit{ 0u };
	u32 iFreeHead{ CY_INVALID_INDEX };
	const allocator_t *pAllocator{ nullptr };
};

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_status_t GenerationPool_Init(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	const allocator_t *pAllocator,
	usize cSlotLimit,
	usize cInitialSlots = 0u ) noexcept;

template <typename record_t, typename pool_tag_t>
void GenerationPool_Shutdown(
	generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

template <typename record_t, typename pool_tag_t>
void GenerationPool_Clear(
	generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] bool_t GenerationPool_IsValid(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

// This is an O(1) readiness query for hot paths. IsValid performs the complete
// O(slot count) structural audit, including free-list reachability and counts.
template <typename record_t, typename pool_tag_t>
[[nodiscard]] bool_t GenerationPool_IsInitialized(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

// Successful reserve preserves handles but invalidates all record pointers.
// Allocation failure leaves the pool and all pointers completely unchanged.
template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_status_t GenerationPool_Reserve(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	usize cSlots ) noexcept;

template <typename record_t, typename pool_tag_t, typename... args_t>
[[nodiscard]] generation_pool_handle_result_t<pool_tag_t>
GenerationPool_Emplace(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	args_t &&...args ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_handle_result_t<pool_tag_t>
GenerationPool_Insert(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	const record_t &record ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_handle_result_t<pool_tag_t>
GenerationPool_InsertMove(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	record_t &&record ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_resolve_result_t<record_t>
GenerationPool_Resolve(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_resolve_result_t<const record_t>
GenerationPool_Resolve(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] record_t *GenerationPool_Get(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] const record_t *GenerationPool_Get(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] bool_t GenerationPool_Contains(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] generation_pool_status_t GenerationPool_Remove(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept;

// Iteration is deterministic in ascending slot order. The visitor receives the
// live handle and record and must not structurally mutate the pool.
template <typename record_t, typename pool_tag_t, typename visitor_t>
[[nodiscard]] usize GenerationPool_ForEach(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	visitor_t &&visitor ) noexcept;

template <typename record_t, typename pool_tag_t, typename visitor_t>
[[nodiscard]] usize GenerationPool_ForEach(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	visitor_t &&visitor ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] usize GenerationPool_Count(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] usize GenerationPool_Capacity(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

template <typename record_t, typename pool_tag_t>
[[nodiscard]] usize GenerationPool_RetiredCount(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_GENERATION_POOL_INL
#include "CypherCommon_GenerationPool.inl"
#endif

#endif // CYPHER_COMMON_TIER1_GENERATION_POOL_H
