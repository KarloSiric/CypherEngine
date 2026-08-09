//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Function.inl
//  Purpose: Implements allocator-aware owning type-erased callables.
//  Details: Compile-time thunks manage invocation, destruction, and inline movement.
//           Heap allocation is completed before an existing binding is released.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FUNCTION_INL
#define CYPHER_COMMON_TIER1_FUNCTION_INL

#ifndef CYPHER_COMMON_TIER1_FUNCTION_H
    #include "CypherCommon_Function.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename signature_t, usize cbInline>
bool_t Function_IsCanonicalEmpty(
    const function_t<signature_t, cbInline> &function ) noexcept
{
    return function.pCallable == nullptr &&
           function.pfnInvoke == nullptr &&
           function.pfnDestroy == nullptr &&
           function.pfnMove == nullptr &&
           function.pAllocator == nullptr &&
           function.cbAllocation == 0u &&
           function.alignment == 0u &&
           !function.bHeapAllocated;
}

template <typename signature_t, usize cbInline>
void Function_ClearBinding(
    function_t<signature_t, cbInline> &function ) noexcept
{
    function.pCallable = nullptr;
    function.pfnInvoke = nullptr;
    function.pfnDestroy = nullptr;
    function.pfnMove = nullptr;
    function.cbAllocation = 0u;
    function.alignment = 0u;
    function.bHeapAllocated = CY_FALSE;
}

template <typename signature_t, usize cbInline, typename callable_arg_t>
struct function_binder_t;

template <
    usize cbInline,
    typename callable_arg_t,
    typename return_t,
    typename... args_t>
struct function_binder_t<
    return_t( args_t... ),
    cbInline,
    callable_arg_t> {
    using function_type_t = function_t<return_t( args_t... ), cbInline>;
    using callable_t = std::decay_t<callable_arg_t>;

    static bool_t Bind(
        function_type_t *pFunction,
        callable_arg_t &&callable ) noexcept
    {
        static_assert(
            std::is_nothrow_constructible_v<
                callable_t,
                callable_arg_t &&>,
            "Function callable construction must not throw." );
        static_assert(
            std::is_nothrow_move_constructible_v<callable_t>,
            "Function callable movement must not throw." );
        static_assert(
            std::is_nothrow_destructible_v<callable_t>,
            "Function callable destruction must not throw." );
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                callable_t &,
                args_t...>,
            "Function callable must match the signature and be noexcept." );

        const bool_t bInitialized =
            pFunction != nullptr && Function_IsInitialized( *pFunction );
        CY_ASSERT_MSG(
            bInitialized,
            "Function_Bind requires an initialized function object." );
        if ( !bInitialized ) {
            return CY_FALSE;
        }

        if constexpr ( std::is_pointer_v<callable_t> ) {
            const bool_t bCallablePresent = callable != nullptr;
            CY_ASSERT_MSG(
                bCallablePresent,
                "Function_Bind does not accept a null callable pointer." );
            if ( !bCallablePresent ) {
                return CY_FALSE;
            }
        }

        constexpr bool bUseInlineStorage =
            cbInline > 0u &&
            sizeof( callable_t ) <= cbInline &&
            alignof( callable_t ) <= alignof( std::max_align_t );

        void *pNewCallable = nullptr;
        if constexpr ( bUseInlineStorage ) {
            callable_t temporary(
                static_cast<callable_arg_t &&>( callable ) );
            Function_Reset( pFunction );
            pNewCallable = pFunction->inlineStorage;
            ::new ( pNewCallable ) callable_t(
                static_cast<callable_t &&>( temporary ) );
        } else {
            pNewCallable = Allocator_Allocate(
                pFunction->pAllocator,
                sizeof( callable_t ),
                alignof( callable_t ) );
            if ( pNewCallable == nullptr ) {
                return CY_FALSE;
            }
            ::new ( pNewCallable ) callable_t(
                static_cast<callable_arg_t &&>( callable ) );
            Function_Reset( pFunction );
        }

        pFunction->pCallable = pNewCallable;
        pFunction->pfnInvoke = [](
            void *pStoredCallable,
            args_t... args ) noexcept -> return_t {
            return ( *static_cast<callable_t *>( pStoredCallable ) )(
                static_cast<args_t &&>( args )... );
        };
        pFunction->pfnDestroy = []( void *pStoredCallable ) noexcept {
            static_cast<callable_t *>( pStoredCallable )->~callable_t();
        };
        pFunction->pfnMove = [](
            void *pDestination,
            void *pSource ) noexcept {
            callable_t *pSourceCallable =
                static_cast<callable_t *>( pSource );
            ::new ( pDestination ) callable_t(
                static_cast<callable_t &&>( *pSourceCallable ) );
            pSourceCallable->~callable_t();
        };
        pFunction->cbAllocation = bUseInlineStorage
            ? 0u
            : sizeof( callable_t );
        pFunction->alignment = alignof( callable_t );
        pFunction->bHeapAllocated = bUseInlineStorage
            ? CY_FALSE
            : CY_TRUE;
        return CY_TRUE;
    }
};

} // namespace detail

template <typename return_t, usize cbInline, typename... args_t>
function_t<return_t( args_t... ), cbInline>::~function_t() noexcept
{
    Function_Reset( this );
}

template <typename signature_t, usize cbInline>
bool_t Function_Init(
    function_t<signature_t, cbInline> *pFunction,
    const allocator_t *pAllocator ) noexcept
{
    const bool_t bValidDestination =
        pFunction != nullptr &&
        detail::Function_IsCanonicalEmpty( *pFunction );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "Function_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "Function_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    pFunction->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename signature_t, usize cbInline>
void Function_Reset(
    function_t<signature_t, cbInline> *pFunction ) noexcept
{
    const bool_t bValidFunction = Function_IsValid( pFunction );
    CY_ASSERT_MSG( bValidFunction, "Function_Reset requires a valid object." );
    if ( !bValidFunction || pFunction->pCallable == nullptr ) {
        return;
    }

    void *pCallable = pFunction->pCallable;
    const typename function_t<signature_t, cbInline>::destroy_fn_t pfnDestroy =
        pFunction->pfnDestroy;
    const bool_t bHeapAllocated = pFunction->bHeapAllocated;
    const usize cbAllocation = pFunction->cbAllocation;
    const usize nAlignment = pFunction->alignment;
    const allocator_t *pAllocator = pFunction->pAllocator;

    pfnDestroy( pCallable );
    if ( bHeapAllocated ) {
        Allocator_Free(
            pAllocator,
            pCallable,
            cbAllocation,
            nAlignment );
    }
    detail::Function_ClearBinding( *pFunction );
}

template <typename signature_t, usize cbInline>
bool_t Function_IsValid(
    const function_t<signature_t, cbInline> *pFunction ) noexcept
{
    if ( pFunction == nullptr ) {
        return CY_FALSE;
    }
    if ( pFunction->pCallable == nullptr ) {
        return pFunction->pfnInvoke == nullptr &&
               pFunction->pfnDestroy == nullptr &&
               pFunction->pfnMove == nullptr &&
               pFunction->cbAllocation == 0u &&
               pFunction->alignment == 0u &&
               !pFunction->bHeapAllocated &&
               ( pFunction->pAllocator == nullptr ||
                 Allocator_IsValid( pFunction->pAllocator ) );
    }

    if ( pFunction->pfnInvoke == nullptr ||
         pFunction->pfnDestroy == nullptr ||
         pFunction->pfnMove == nullptr ||
         !Allocator_IsValid( pFunction->pAllocator ) ||
         !Cy_AlignIsPowerOfTwo( pFunction->alignment ) ) {
        return CY_FALSE;
    }

    if ( pFunction->bHeapAllocated ) {
        return pFunction->cbAllocation > 0u;
    }
    return pFunction->pCallable == pFunction->inlineStorage &&
           pFunction->cbAllocation == 0u &&
           pFunction->alignment <= alignof( std::max_align_t );
}

template <typename signature_t, usize cbInline>
bool_t Function_IsInitialized(
    const function_t<signature_t, cbInline> &function ) noexcept
{
    return Function_IsValid( &function ) &&
           Allocator_IsValid( function.pAllocator );
}

template <typename signature_t, usize cbInline>
bool_t Function_IsBound(
    const function_t<signature_t, cbInline> &function ) noexcept
{
    const bool_t bValidFunction = Function_IsValid( &function );
    CY_ASSERT_MSG( bValidFunction, "Function_IsBound received a corrupted object." );
    return bValidFunction && function.pCallable != nullptr;
}

template <typename signature_t, usize cbInline, typename callable_t>
bool_t Function_Bind(
    function_t<signature_t, cbInline> *pFunction,
    callable_t &&callable ) noexcept
{
    return detail::function_binder_t<
        signature_t,
        cbInline,
        callable_t>::Bind(
            pFunction,
            static_cast<callable_t &&>( callable ) );
}

template <typename signature_t, usize cbInline>
bool_t Function_Move(
    function_t<signature_t, cbInline> *pDest,
    function_t<signature_t, cbInline> *pSource ) noexcept
{
    const bool_t bDistinctObjects =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bValidObjects =
        bDistinctObjects &&
        Function_IsValid( pDest ) &&
        Function_IsValid( pSource );
    CY_ASSERT_MSG(
        bDistinctObjects,
        "Function_Move requires distinct source and destination objects." );
    CY_ASSERT_MSG(
        bValidObjects,
        "Function_Move requires valid source and destination objects." );
    if ( !bValidObjects ) {
        return CY_FALSE;
    }

    Function_Reset( pDest );
    pDest->pAllocator = pSource->pAllocator;
    if ( !Function_IsBound( *pSource ) ) {
        return CY_TRUE;
    }

    pDest->pfnInvoke = pSource->pfnInvoke;
    pDest->pfnDestroy = pSource->pfnDestroy;
    pDest->pfnMove = pSource->pfnMove;
    pDest->cbAllocation = pSource->cbAllocation;
    pDest->alignment = pSource->alignment;
    pDest->bHeapAllocated = pSource->bHeapAllocated;
    if ( pSource->bHeapAllocated ) {
        pDest->pCallable = pSource->pCallable;
    } else {
        pDest->pCallable = pDest->inlineStorage;
        pSource->pfnMove(
            pDest->pCallable,
            pSource->pCallable );
    }
    detail::Function_ClearBinding( *pSource );
    return CY_TRUE;
}

template <typename signature_t, usize cbInline>
bool_t Function_UsesInlineStorage(
    const function_t<signature_t, cbInline> &function ) noexcept
{
    return Function_IsBound( function ) && !function.bHeapAllocated;
}

template <typename return_t, usize cbInline, typename... args_t>
return_t Function_Invoke(
    const function_t<return_t( args_t... ), cbInline> &function,
    std::type_identity_t<args_t>... args ) noexcept
{
    const bool_t bBound = Function_IsBound( function );
    CY_ASSERT_MSG( bBound, "Function_Invoke requires a bound callable." );
    return function.pfnInvoke(
        function.pCallable,
        static_cast<args_t &&>( args )... );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FUNCTION_INL
