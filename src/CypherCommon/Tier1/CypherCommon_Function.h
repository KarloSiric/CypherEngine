//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Function.h
//  Purpose: Declares allocator-aware owning type-erased callables.
//  Details: Small callables use inline storage; larger callables use the supplied
//           allocator. Runtime code remains exception-free and move-only by contract;
//           failed heap rebinding preserves the previously bound callable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Function Contract

Unlike delegate_t, this wrapper owns its callable. Small targets live in inlineStorage; larger or
over-aligned targets use pAllocator. The thunk trio is an internal vtable without C++ virtual
dispatch. A callable is considered bound only when all thunks and pCallable describe one target.
================
*/

#ifndef CYPHER_COMMON_TIER1_FUNCTION_H
#define CYPHER_COMMON_TIER1_FUNCTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

#include <cstddef>
#include <type_traits>

namespace cypher::common
{

constexpr usize CY_FUNCTION_DEFAULT_INLINE_BYTES = 48u; // Covers common captures without heap traffic.

template <typename signature_t, usize cbInline = CY_FUNCTION_DEFAULT_INLINE_BYTES>
struct function_t;

template <typename return_t, usize cbInline, typename... args_t>
struct function_t<return_t( args_t... ), cbInline> {
    using invoke_fn_t = return_t ( * )( void *pCallable, args_t... args ) noexcept;
    using destroy_fn_t = void ( * )( void *pCallable ) noexcept;
    using move_fn_t = void ( * )( void *pDest, void *pSource ) noexcept;

    function_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( function_t );
    ~function_t() noexcept;

    alignas( std::max_align_t ) byte inlineStorage[cbInline > 0u ? cbInline : 1u]{}; // Local target storage.
    void *pCallable{ nullptr };                    // Inline target or allocator-owned target address.
    invoke_fn_t pfnInvoke{ nullptr };              // Calls the erased concrete callable.
    destroy_fn_t pfnDestroy{ nullptr };            // Ends the concrete callable lifetime.
    move_fn_t pfnMove{ nullptr };                  // Move-constructs an inline target into new storage.
    const allocator_t *pAllocator{ nullptr };      // Allocator used for an out-of-line target.
    usize cbAllocation{ 0u };                      // Heap byte count needed by Allocator_Free.
    usize alignment{ 0u };                         // Heap alignment needed by Allocator_Free.
    bool_t bHeapAllocated{ CY_FALSE };             // Selects pCallable ownership and move behavior.
};

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_Init(
    function_t<signature_t, cbInline> *pFunction,
    const allocator_t *pAllocator ) noexcept;

template <typename signature_t, usize cbInline>
void Function_Reset(
    function_t<signature_t, cbInline> *pFunction ) noexcept;

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_IsValid(
    const function_t<signature_t, cbInline> *pFunction ) noexcept;

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_IsInitialized(
    const function_t<signature_t, cbInline> &function ) noexcept;

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_IsBound(
    const function_t<signature_t, cbInline> &function ) noexcept;

template <typename signature_t, usize cbInline, typename callable_t>
CYPHER_NODISCARD bool_t Function_Bind(
    function_t<signature_t, cbInline> *pFunction,
    callable_t &&callable ) noexcept;

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_Move(
    function_t<signature_t, cbInline> *pDest,
    function_t<signature_t, cbInline> *pSource ) noexcept;

template <typename signature_t, usize cbInline>
CYPHER_NODISCARD bool_t Function_UsesInlineStorage(
    const function_t<signature_t, cbInline> &function ) noexcept;

template <typename return_t, usize cbInline, typename... args_t>
return_t Function_Invoke(
    const function_t<return_t( args_t... ), cbInline> &function,
    std::type_identity_t<args_t>... args ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_FUNCTION_INL
    #include "CypherCommon_Function.inl"
#endif

#endif // CYPHER_COMMON_TIER1_FUNCTION_H
