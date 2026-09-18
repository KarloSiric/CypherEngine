//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherCommon_GenerationPool.inl
//  Purpose: Implements typed generation-pool storage.
//  Details: Growth is transactional, traversal is slot ordered, and a slot is
//           retired permanently before its 32-bit generation could wrap.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_GENERATION_POOL_INL
#define CYPHER_COMMON_TIER1_GENERATION_POOL_INL

#ifndef CYPHER_COMMON_TIER1_GENERATION_POOL_H
#include "CypherCommon_GenerationPool.h"
#endif

#ifndef PRAGMA_ONCE
#pragma once
#endif

#include <new>
#include <type_traits>

namespace cypher::common {

namespace detail {

template <typename record_t>
record_t *SlotValue( generation_pool_slot_t<record_t> &slot ) noexcept {
	return std::launder( reinterpret_cast<record_t *>( slot.storage ) );
}

template <typename record_t>
const record_t *SlotValue(
	const generation_pool_slot_t<record_t> &slot ) noexcept {
	return std::launder( reinterpret_cast<const record_t *>( slot.storage ) );
}

template <typename record_t, typename pool_tag_t>
bool_t IsCanonicalEmpty(
	const generation_pool_t<record_t, pool_tag_t> &pool ) noexcept {
	return pool.pSlots == nullptr && pool.cRecords == 0u && pool.cSlots == 0u && pool.cRetiredSlots == 0u && pool.cSlotLimit == 0u && pool.iFreeHead == CY_INVALID_INDEX && pool.pAllocator == nullptr;
}

template <typename record_t, typename pool_tag_t>
bool_t HasValidHeader(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	if ( pPool == nullptr ) {
		return false;
	}
	if ( IsCanonicalEmpty( *pPool ) ) {
		return true;
	}
	if ( !Allocator_IsValid( pPool->pAllocator ) || pPool->cSlotLimit == 0u || pPool->cSlotLimit > CY_GENERATION_POOL_MAX_CAPACITY || pPool->cSlots > pPool->cSlotLimit || pPool->cRecords > pPool->cSlots || pPool->cRetiredSlots > pPool->cSlots - pPool->cRecords ) {
		return false;
	}
	if ( pPool->pSlots == nullptr ) {
		return pPool->cSlots == 0u && pPool->cRecords == 0u && pPool->cRetiredSlots == 0u && pPool->iFreeHead == CY_INVALID_INDEX;
	}

	return pPool->cSlots > 0u && ( pPool->iFreeHead == CY_INVALID_INDEX || static_cast<usize>( pPool->iFreeHead ) < pPool->cSlots );
}

template <typename record_t, typename pool_tag_t>
generation_pool_status_t ReadinessStatus(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	if ( pPool == nullptr || IsCanonicalEmpty( *pPool ) ) {
		return generation_pool_status_t::NOT_INITIALIZED;
	}
	return HasValidHeader( pPool )
		? generation_pool_status_t::OK
		: generation_pool_status_t::CORRUPT_STATE;
}

template <typename record_t, typename pool_tag_t>
void RebuildFreeList(
	generation_pool_t<record_t, pool_tag_t> &pool ) noexcept {
	pool.iFreeHead = CY_INVALID_INDEX;

	// Reverse traversal plus push-front yields ascending free-slot order after
	// initialization, growth, and clear. Individual removal deliberately makes
	// the most recently removed slot the next reused slot.
	for ( usize iSlot = pool.cSlots; iSlot > 0u; --iSlot ) {
		const u32 iCurrent = static_cast<u32>( iSlot - 1u );
		generation_pool_slot_t<record_t> &slot = pool.pSlots[iCurrent];
		if ( slot.bOccupied || slot.bRetired ) {
			slot.iNextFree = CY_INVALID_INDEX;
			continue;
		}

		slot.iNextFree = pool.iFreeHead;
		pool.iFreeHead = iCurrent;
	}
}

template <typename record_t, typename pool_tag_t>
void DestroyValues(
	generation_pool_t<record_t, pool_tag_t> &pool ) noexcept {
	static_assert(
		std::is_nothrow_destructible_v<record_t>,
		"Generation-pool records must be nothrow destructible." );

	for ( usize iSlot = 0u; iSlot < pool.cSlots; ++iSlot ) {
		generation_pool_slot_t<record_t> &slot = pool.pSlots[iSlot];
		if ( slot.bOccupied ) {
			SlotValue( slot )->~record_t();
		}
	}
}

template <typename record_t, typename pool_tag_t>
void DestroySlotStorage(
	generation_pool_t<record_t, pool_tag_t> &pool ) noexcept {
	for ( usize iSlot = 0u; iSlot < pool.cSlots; ++iSlot ) {
		pool.pSlots[iSlot].~generation_pool_slot_t<record_t>();
	}

	Allocator_FreeArrayStorage(
		pool.pAllocator,
		pool.pSlots,
		pool.cSlots );
}

template <typename record_t, typename pool_tag_t>
bool_t CalculateGrowth(
	const generation_pool_t<record_t, pool_tag_t> &pool,
	usize &cSlotsOut ) noexcept {
	if ( pool.cSlots >= pool.cSlotLimit ) {
		return false;
	}

	constexpr usize cMinimumSlots = 8u;
	usize cCandidate = pool.cSlots < cMinimumSlots
		? cMinimumSlots
		: pool.cSlots + pool.cSlots / 2u;
	if ( cCandidate <= pool.cSlots || cCandidate > pool.cSlotLimit ) {
		cCandidate = pool.cSlotLimit;
	}

	cSlotsOut = cCandidate;
	return cCandidate > pool.cSlots;
}

template <typename record_t, typename pool_tag_t>
generation_pool_status_t DecodeLiveHandle(
	const generation_pool_t<record_t, pool_tag_t> &pool,
	generation_handle_t<pool_tag_t> handle,
	u32 &iSlotOut ) noexcept {
	if ( !GenerationHandle_IsValid( handle ) || static_cast<usize>( handle.nSlot ) >= pool.cSlots ) {
		return generation_pool_status_t::INVALID_HANDLE;
	}

	const generation_pool_slot_t<record_t> &slot =
		pool.pSlots[handle.nSlot];
	if ( !slot.bOccupied || slot.nGeneration != handle.nGeneration ) {
		return generation_pool_status_t::STALE_HANDLE;
	}
	if ( slot.bRetired || slot.iNextFree != CY_INVALID_INDEX ||
		 pool.cRecords == 0u ) {
		return generation_pool_status_t::CORRUPT_STATE;
	}

	iSlotOut = handle.nSlot;
	return generation_pool_status_t::OK;
}

// Mutation paths use this bounded check before trusting a free-list node. The
// complete O(n) audit remains GenerationPool_IsValid, but a forged head must not
// be allowed to turn a detectable corrupt list into a successful mutation.
template <typename record_t, typename pool_tag_t>
bool_t IsLocallyValidFreeNode(
	const generation_pool_t<record_t, pool_tag_t> &pool,
	u32 iSlot ) noexcept {
	if ( static_cast<usize>( iSlot ) >= pool.cSlots ) {
		return false;
	}

	const generation_pool_slot_t<record_t> &slot = pool.pSlots[iSlot];
	if ( slot.bOccupied || slot.bRetired || slot.nGeneration == 0u ) {
		return false;
	}
	if ( slot.iNextFree == CY_INVALID_INDEX ) {
		return true;
	}
	if ( slot.iNextFree == iSlot ||
		 static_cast<usize>( slot.iNextFree ) >= pool.cSlots ) {
		return false;
	}

	const generation_pool_slot_t<record_t> &successor =
		pool.pSlots[slot.iNextFree];
	return !successor.bOccupied && !successor.bRetired &&
		successor.nGeneration != 0u;
}

template <typename record_t, typename pool_tag_t>
bool_t IsLocallyValidFreeHead(
	const generation_pool_t<record_t, pool_tag_t> &pool,
	u32 iHead ) noexcept {
	if ( !IsLocallyValidFreeNode( pool, iHead ) ) {
		return false;
	}

	const u32 iSuccessor = pool.pSlots[iHead].iNextFree;
	return iSuccessor == CY_INVALID_INDEX ||
		( IsLocallyValidFreeNode( pool, iSuccessor ) &&
		  pool.pSlots[iSuccessor].iNextFree != iHead );
}

// Growth constructs the appended record while the old slot array is still
// alive. This ordering makes Insert/Emplace safe when constructor arguments
// refer to an existing pooled record. Only after the new record has consumed
// those arguments are the old records relocated and their storage released.
template <typename record_t, typename pool_tag_t, typename... args_t>
generation_pool_handle_result_t<pool_tag_t> GrowAndEmplace(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	args_t &&...args ) noexcept {
	generation_pool_handle_result_t<pool_tag_t> result{};
	usize cGrowthSlots = 0u;
	if ( !CalculateGrowth( *pPool, cGrowthSlots ) ) {
		result.status = generation_pool_status_t::LIMIT_EXCEEDED;
		return result;
	}

	using slot_t = generation_pool_slot_t<record_t>;
	slot_t *pNewSlots = Allocator_AllocateArrayStorage<slot_t>(
		pPool->pAllocator,
		cGrowthSlots );
	if ( pNewSlots == nullptr ) {
		result.status = generation_pool_status_t::ALLOCATION_FAILED;
		return result;
	}

	for ( usize iSlot = 0u; iSlot < cGrowthSlots; ++iSlot ) {
		::new ( static_cast<void *>( pNewSlots + iSlot ) ) slot_t;
	}

	const usize iInserted = pPool->cSlots;
	slot_t &insertedSlot = pNewSlots[iInserted];
	::new ( static_cast<void *>( insertedSlot.storage ) )
		record_t( static_cast<args_t &&>( args )... );
	insertedSlot.bOccupied = true;

	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		slot_t &source = pPool->pSlots[iSlot];
		slot_t &destination = pNewSlots[iSlot];
		destination.nGeneration = source.nGeneration;
		destination.bOccupied = source.bOccupied;
		destination.bRetired = source.bRetired;
		if ( !source.bOccupied ) {
			continue;
		}

		if constexpr ( std::is_nothrow_move_constructible_v<record_t> ) {
			::new ( static_cast<void *>( destination.storage ) )
				record_t( static_cast<record_t &&>(
					*SlotValue( source ) ) );
		} else {
			::new ( static_cast<void *>( destination.storage ) )
				record_t( *SlotValue( source ) );
		}
	}

	if ( pPool->pSlots != nullptr ) {
		DestroyValues( *pPool );
		DestroySlotStorage( *pPool );
	}

	pPool->pSlots = pNewSlots;
	pPool->cSlots = cGrowthSlots;
	++pPool->cRecords;
	RebuildFreeList( *pPool );

	result.status = generation_pool_status_t::OK;
	result.handle = generation_handle_t<pool_tag_t>{
		static_cast<u32>( iInserted ),
		insertedSlot.nGeneration
	};
	return result;
}

} // namespace detail

template <typename record_t, typename pool_tag_t>
generation_pool_t<record_t, pool_tag_t>::~generation_pool_t() noexcept {
	GenerationPool_Shutdown( this );
}

template <typename record_t, typename pool_tag_t>
generation_pool_status_t GenerationPool_Init(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	const allocator_t *pAllocator,
	usize cSlotLimit,
	usize cInitialSlots ) noexcept {
	if ( pPool == nullptr || !Allocator_IsValid( pAllocator ) || cSlotLimit == 0u || cSlotLimit > CY_GENERATION_POOL_MAX_CAPACITY || cInitialSlots > cSlotLimit ) {
		return generation_pool_status_t::INVALID_ARGUMENT;
	}
	if ( !detail::IsCanonicalEmpty( *pPool ) ) {
		return generation_pool_status_t::ALREADY_INITIALIZED;
	}

	pPool->pAllocator = pAllocator;
	pPool->cSlotLimit = cSlotLimit;
	if ( cInitialSlots == 0u ) {
		return generation_pool_status_t::OK;
	}

	const generation_pool_status_t reserveStatus =
		GenerationPool_Reserve( pPool, cInitialSlots );
	if ( reserveStatus != generation_pool_status_t::OK ) {
		pPool->pAllocator = nullptr;
		pPool->cSlotLimit = 0u;
	}
	return reserveStatus;
}

template <typename record_t, typename pool_tag_t>
void GenerationPool_Shutdown(
	generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	if ( pPool == nullptr || detail::IsCanonicalEmpty( *pPool ) ) {
		return;
	}

	const bool_t bValidPool = GenerationPool_IsValid( pPool );
	CY_ASSERT_MSG(
		bValidPool,
		"GenerationPool_Shutdown requires structurally valid storage." );
	if ( !bValidPool ) {
		// Corrupt counts or links make best-effort destruction unsafe. Preserve the
		// evidence for the caller/debugger rather than guessing object lifetimes.
		return;
	}

	if ( pPool->pSlots != nullptr ) {
		detail::DestroyValues( *pPool );
		detail::DestroySlotStorage( *pPool );
	}

	pPool->pSlots = nullptr;
	pPool->cRecords = 0u;
	pPool->cSlots = 0u;
	pPool->cRetiredSlots = 0u;
	pPool->cSlotLimit = 0u;
	pPool->iFreeHead = CY_INVALID_INDEX;
	pPool->pAllocator = nullptr;
}

template <typename record_t, typename pool_tag_t>
void GenerationPool_Clear(
	generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	if ( !GenerationPool_IsInitialized( pPool ) || pPool->pSlots == nullptr ) {
		return;
	}
	const bool_t bValidPool = GenerationPool_IsValid( pPool );
	CY_ASSERT_MSG(
		bValidPool,
		"GenerationPool_Clear requires structurally valid storage." );
	if ( !bValidPool ) {
		return;
	}

	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
		if ( !slot.bOccupied ) {
			continue;
		}

		detail::SlotValue( slot )->~record_t();
		slot.bOccupied = false;
		if ( slot.nGeneration == CY_U32_MAX ) {
			slot.bRetired = true;
			++pPool->cRetiredSlots;
		} else {
			++slot.nGeneration;
		}
	}

	pPool->cRecords = 0u;
	detail::RebuildFreeList( *pPool );
}

template <typename record_t, typename pool_tag_t>
bool_t GenerationPool_IsValid(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	if ( !detail::HasValidHeader( pPool ) ) {
		return false;
	}
	if ( detail::IsCanonicalEmpty( *pPool ) ) {
		return true;
	}
	if ( pPool->pSlots == nullptr ) {
		return true;
	}

	usize cOccupied = 0u;
	usize cRetired = 0u;
	usize cFree = 0u;
	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		const generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
		if ( slot.nGeneration == 0u || ( slot.bOccupied && slot.bRetired ) ) {
			return false;
		}
		if ( slot.bOccupied ) {
			if ( slot.iNextFree != CY_INVALID_INDEX ) {
				return false;
			}
			++cOccupied;
		} else if ( slot.bRetired ) {
			if ( slot.nGeneration != CY_U32_MAX ) {
				return false;
			}
			if ( slot.iNextFree != CY_INVALID_INDEX ) {
				return false;
			}
			++cRetired;
		} else {
			++cFree;
		}
	}

	if ( cOccupied != pPool->cRecords || cRetired != pPool->cRetiredSlots || cOccupied + cRetired + cFree != pPool->cSlots ) {
		return false;
	}
	if ( cFree == 0u ) {
		return pPool->iFreeHead == CY_INVALID_INDEX;
	}
	if ( pPool->iFreeHead == CY_INVALID_INDEX ) {
		return false;
	}

	usize cReachableFree = 0u;
	u32 iFree = pPool->iFreeHead;
	while ( iFree != CY_INVALID_INDEX ) {
		if ( static_cast<usize>( iFree ) >= pPool->cSlots ) {
			return false;
		}
		const generation_pool_slot_t<record_t> &slot = pPool->pSlots[iFree];
		if ( slot.bOccupied || slot.bRetired || ++cReachableFree > cFree ) {
			return false;
		}
		iFree = slot.iNextFree;
	}

	return cReachableFree == cFree;
}

template <typename record_t, typename pool_tag_t>
bool_t GenerationPool_IsInitialized(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	return detail::ReadinessStatus( pPool ) == generation_pool_status_t::OK;
}

template <typename record_t, typename pool_tag_t>
generation_pool_status_t GenerationPool_Reserve(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	usize cSlots ) noexcept {
	static_assert(
		std::is_nothrow_destructible_v<record_t>,
		"Generation-pool records must be nothrow destructible." );
	static_assert(
		std::is_nothrow_move_constructible_v<record_t> || std::is_nothrow_copy_constructible_v<record_t>,
		"Generation-pool growth requires nothrow move or copy." );

	const generation_pool_status_t readiness = detail::ReadinessStatus( pPool );
	if ( readiness != generation_pool_status_t::OK ) {
		return readiness;
	}
	if ( !GenerationPool_IsValid( pPool ) ) {
		return generation_pool_status_t::CORRUPT_STATE;
	}
	if ( cSlots <= pPool->cSlots ) {
		return generation_pool_status_t::OK;
	}
	if ( cSlots > pPool->cSlotLimit || cSlots > CY_GENERATION_POOL_MAX_CAPACITY ) {
		return generation_pool_status_t::LIMIT_EXCEEDED;
	}

	using slot_t = generation_pool_slot_t<record_t>;
	slot_t *pNewSlots = Allocator_AllocateArrayStorage<slot_t>(
		pPool->pAllocator,
		cSlots );
	if ( pNewSlots == nullptr ) {
		return generation_pool_status_t::ALLOCATION_FAILED;
	}

	for ( usize iSlot = 0u; iSlot < cSlots; ++iSlot ) {
		::new ( static_cast<void *>( pNewSlots + iSlot ) ) slot_t;
	}

	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		slot_t &source = pPool->pSlots[iSlot];
		slot_t &destination = pNewSlots[iSlot];
		destination.nGeneration = source.nGeneration;
		destination.bOccupied = source.bOccupied;
		destination.bRetired = source.bRetired;
		if ( !source.bOccupied ) {
			continue;
		}

		if constexpr ( std::is_nothrow_move_constructible_v<record_t> ) {
			::new ( static_cast<void *>( destination.storage ) )
				record_t( static_cast<record_t &&>(
					*detail::SlotValue( source ) ) );
		} else {
			::new ( static_cast<void *>( destination.storage ) )
				record_t( *detail::SlotValue( source ) );
		}
	}

	if ( pPool->pSlots != nullptr ) {
		detail::DestroyValues( *pPool );
		detail::DestroySlotStorage( *pPool );
	}

	pPool->pSlots = pNewSlots;
	pPool->cSlots = cSlots;
	detail::RebuildFreeList( *pPool );
	return generation_pool_status_t::OK;
}

template <typename record_t, typename pool_tag_t, typename... args_t>
generation_pool_handle_result_t<pool_tag_t> GenerationPool_Emplace(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	args_t &&...args ) noexcept {
	static_assert(
		std::is_nothrow_constructible_v<record_t, args_t...>,
		"Generation-pool record construction must not throw." );
	static_assert(
		std::is_nothrow_destructible_v<record_t>,
		"Generation-pool records must be nothrow destructible." );
	static_assert(
		std::is_nothrow_move_constructible_v<record_t> ||
			std::is_nothrow_copy_constructible_v<record_t>,
		"Generation-pool growth requires nothrow move or copy." );

	generation_pool_handle_result_t<pool_tag_t> result{};
	result.status = detail::ReadinessStatus( pPool );
	if ( result.status != generation_pool_status_t::OK ) {
		return result;
	}

	if ( pPool->iFreeHead == CY_INVALID_INDEX ) {
		if ( !GenerationPool_IsValid( pPool ) ) {
			result.status = generation_pool_status_t::CORRUPT_STATE;
			return result;
		}
		return detail::GrowAndEmplace(
			pPool,
			static_cast<args_t &&>( args )... );
	}
	if ( pPool->cRecords >= pPool->cSlots - pPool->cRetiredSlots ) {
		result.status = generation_pool_status_t::CORRUPT_STATE;
		return result;
	}

	const u32 iSlot = pPool->iFreeHead;
	generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
	const bool_t bValidFreeSlot =
		detail::IsLocallyValidFreeHead( *pPool, iSlot );
	if ( !bValidFreeSlot ) {
		result.status = generation_pool_status_t::CORRUPT_STATE;
		return result;
	}
	pPool->iFreeHead = slot.iNextFree;
	slot.iNextFree = CY_INVALID_INDEX;

	::new ( static_cast<void *>( slot.storage ) )
		record_t( static_cast<args_t &&>( args )... );
	slot.bOccupied = true;
	++pPool->cRecords;

	result.status = generation_pool_status_t::OK;
	result.handle = generation_handle_t<pool_tag_t>{ iSlot, slot.nGeneration };
	return result;
}

template <typename record_t, typename pool_tag_t>
generation_pool_handle_result_t<pool_tag_t> GenerationPool_Insert(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	const record_t &record ) noexcept {
	return GenerationPool_Emplace( pPool, record );
}

template <typename record_t, typename pool_tag_t>
generation_pool_handle_result_t<pool_tag_t> GenerationPool_InsertMove(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	record_t &&record ) noexcept {
	return GenerationPool_Emplace(
		pPool,
		static_cast<record_t &&>( record ) );
}

template <typename record_t, typename pool_tag_t>
generation_pool_resolve_result_t<record_t> GenerationPool_Resolve(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	generation_pool_resolve_result_t<record_t> result{};
	result.status = detail::ReadinessStatus( pPool );
	if ( result.status != generation_pool_status_t::OK ) {
		return result;
	}

	u32 iSlot = 0u;
	result.status = detail::DecodeLiveHandle( *pPool, handle, iSlot );
	if ( result.status == generation_pool_status_t::OK ) {
		result.pValue = detail::SlotValue( pPool->pSlots[iSlot] );
	}
	return result;
}

template <typename record_t, typename pool_tag_t>
generation_pool_resolve_result_t<const record_t> GenerationPool_Resolve(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	generation_pool_resolve_result_t<const record_t> result{};
	result.status = detail::ReadinessStatus( pPool );
	if ( result.status != generation_pool_status_t::OK ) {
		return result;
	}

	u32 iSlot = 0u;
	result.status = detail::DecodeLiveHandle( *pPool, handle, iSlot );
	if ( result.status == generation_pool_status_t::OK ) {
		result.pValue = detail::SlotValue( pPool->pSlots[iSlot] );
	}
	return result;
}

template <typename record_t, typename pool_tag_t>
record_t *GenerationPool_Get(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	return GenerationPool_Resolve( pPool, handle ).pValue;
}

template <typename record_t, typename pool_tag_t>
const record_t *GenerationPool_Get(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	return GenerationPool_Resolve( pPool, handle ).pValue;
}

template <typename record_t, typename pool_tag_t>
bool_t GenerationPool_Contains(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	return GenerationPool_Get( pPool, handle ) != nullptr;
}

template <typename record_t, typename pool_tag_t>
generation_pool_status_t GenerationPool_Remove(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	generation_handle_t<pool_tag_t> handle ) noexcept {
	const generation_pool_status_t readiness = detail::ReadinessStatus( pPool );
	if ( readiness != generation_pool_status_t::OK ) {
		return readiness;
	}

	u32 iSlot = 0u;
	const generation_pool_status_t decodeStatus =
		detail::DecodeLiveHandle( *pPool, handle, iSlot );
	if ( decodeStatus != generation_pool_status_t::OK ) {
		return decodeStatus;
	}
	if ( pPool->iFreeHead != CY_INVALID_INDEX ) {
		if ( !detail::IsLocallyValidFreeHead(
				 *pPool,
				 pPool->iFreeHead ) ) {
			return generation_pool_status_t::CORRUPT_STATE;
		}
	}

	generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
	detail::SlotValue( slot )->~record_t();
	slot.bOccupied = false;
	--pPool->cRecords;

	if ( slot.nGeneration == CY_U32_MAX ) {
		slot.bRetired = true;
		slot.iNextFree = CY_INVALID_INDEX;
		++pPool->cRetiredSlots;
		return generation_pool_status_t::OK;
	}

	++slot.nGeneration;
	slot.iNextFree = pPool->iFreeHead;
	pPool->iFreeHead = iSlot;
	return generation_pool_status_t::OK;
}

template <typename record_t, typename pool_tag_t, typename visitor_t>
usize GenerationPool_ForEach(
	generation_pool_t<record_t, pool_tag_t> *pPool,
	visitor_t &&visitor ) noexcept {
	static_assert(
		std::is_nothrow_invocable_r_v<
			bool_t,
			visitor_t &,
			generation_handle_t<pool_tag_t>,
			record_t &>,
		"Generation-pool visitors must return bool_t and be noexcept." );

	if ( !GenerationPool_IsInitialized( pPool ) ) {
		return 0u;
	}

	usize cVisited = 0u;
	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
		if ( !slot.bOccupied ) {
			continue;
		}

		const generation_handle_t<pool_tag_t> handle{
			static_cast<u32>( iSlot ),
			slot.nGeneration
		};
		++cVisited;
		if ( !visitor( handle, *detail::SlotValue( slot ) ) ) {
			break;
		}
	}
	return cVisited;
}

template <typename record_t, typename pool_tag_t, typename visitor_t>
usize GenerationPool_ForEach(
	const generation_pool_t<record_t, pool_tag_t> *pPool,
	visitor_t &&visitor ) noexcept {
	static_assert(
		std::is_nothrow_invocable_r_v<
			bool_t,
			visitor_t &,
			generation_handle_t<pool_tag_t>,
			const record_t &>,
		"Generation-pool visitors must return bool_t and be noexcept." );

	if ( !GenerationPool_IsInitialized( pPool ) ) {
		return 0u;
	}

	usize cVisited = 0u;
	for ( usize iSlot = 0u; iSlot < pPool->cSlots; ++iSlot ) {
		const generation_pool_slot_t<record_t> &slot = pPool->pSlots[iSlot];
		if ( !slot.bOccupied ) {
			continue;
		}

		const generation_handle_t<pool_tag_t> handle{
			static_cast<u32>( iSlot ),
			slot.nGeneration
		};
		++cVisited;
		if ( !visitor( handle, *detail::SlotValue( slot ) ) ) {
			break;
		}
	}
	return cVisited;
}

template <typename record_t, typename pool_tag_t>
usize GenerationPool_Count(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	return GenerationPool_IsInitialized( pPool )
		? pPool->cRecords
		: 0u;
}

template <typename record_t, typename pool_tag_t>
usize GenerationPool_Capacity(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	return GenerationPool_IsInitialized( pPool )
		? pPool->cSlots
		: 0u;
}

template <typename record_t, typename pool_tag_t>
usize GenerationPool_RetiredCount(
	const generation_pool_t<record_t, pool_tag_t> *pPool ) noexcept {
	return GenerationPool_IsInitialized( pPool )
		? pPool->cRetiredSlots
		: 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_GENERATION_POOL_INL
