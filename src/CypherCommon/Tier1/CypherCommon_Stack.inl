//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stack.inl
//  Purpose: Implements the vector-backed last-in-first-out adapter.
//  Details: Storage growth and element lifetime remain Vector responsibilities;
//           Stack adds only LIFO policy and guarded value extraction.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STACK_INL
#define CYPHER_COMMON_TIER1_STACK_INL

#ifndef CYPHER_COMMON_TIER1_STACK_H
    #include "CypherCommon_Stack.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <limits>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
bool_t Stack_OutputIsInternal(
    const cy_stack_t<type_t> &stack,
    const type_t *pValueOut ) noexcept
{
    if ( pValueOut == nullptr || stack.storage.pData == nullptr ) {
        return CY_FALSE;
    }

    // Check the complete allocation, not only live elements. Writing into spare
    // capacity would still alias storage whose lifetime the pop operation controls.
    usize cbStorage = 0u;
    if ( !Cy_TryArrayByteCount<type_t>(
             stack.storage.nCapacity,
             cbStorage ) ) {
        return CY_TRUE;
    }
    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nBegin =
        reinterpret_cast<uintptr>( stack.storage.pData );
    if ( nBegin > nMaximumAddress - cbStorage ) {
        return CY_TRUE;
    }

    const uintptr nValue = reinterpret_cast<uintptr>( pValueOut );
    return nValue >= nBegin && nValue < nBegin + cbStorage;
}

} // namespace detail

template <typename type_t>
bool_t Stack_Init(
    cy_stack_t<type_t> *pStack,
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    const bool_t bValidStack = pStack != nullptr;
    CY_ASSERT_MSG( bValidStack, "Stack_Init requires a stack object." );
    return bValidStack &&
           Vector_Init( &pStack->storage, pAllocator, nInitialCapacity );
}

template <typename type_t>
void Stack_Shutdown( cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Shutdown requires a valid stack." );
    if ( bValidStack ) {
        Vector_Shutdown( &pStack->storage );
    }
}

template <typename type_t>
void Stack_Clear( cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Clear requires a valid stack." );
    if ( bValidStack ) {
        Vector_Clear( &pStack->storage );
    }
}

template <typename type_t>
bool_t Stack_IsValid( const cy_stack_t<type_t> *pStack ) noexcept
{
    return pStack != nullptr && Vector_IsValid( &pStack->storage );
}

template <typename type_t>
bool_t Stack_Reserve(
    cy_stack_t<type_t> *pStack,
    usize nCapacity ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Reserve requires a valid stack." );
    return bValidStack && Vector_Reserve( &pStack->storage, nCapacity );
}

template <typename type_t>
bool_t Stack_Push(
    cy_stack_t<type_t> *pStack,
    const type_t &value ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Push requires a valid stack." );
    return bValidStack && Vector_PushBack( &pStack->storage, value );
}

template <typename type_t>
bool_t Stack_PushMove(
    cy_stack_t<type_t> *pStack,
    type_t &&value ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_PushMove requires a valid stack." );
    return bValidStack && Vector_PushBackMove(
        &pStack->storage,
        static_cast<type_t &&>( value ) );
}

template <typename type_t, typename... args_t>
type_t *Stack_Emplace(
    cy_stack_t<type_t> *pStack,
    args_t &&... args ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Emplace requires a valid stack." );
    return bValidStack
        ? Vector_EmplaceBack(
            &pStack->storage,
            static_cast<args_t &&>( args )... )
        : nullptr;
}

template <typename type_t>
bool_t Stack_Pop(
    cy_stack_t<type_t> *pStack,
    type_t *pValueOut ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Pop requires a valid stack." );
    if ( !bValidStack || Vector_IsEmpty( &pStack->storage ) ) {
        return CY_FALSE;
    }

    const bool_t bExternalOutput =
        !detail::Stack_OutputIsInternal( *pStack, pValueOut );
    CY_ASSERT_MSG(
        bExternalOutput,
        "Stack_Pop output may not alias stack storage." );
    if ( !bExternalOutput ) {
        return CY_FALSE;
    }

    // Extract before Vector_PopBack destroys the top object.
    type_t *pTop = Vector_Back( &pStack->storage );
    if ( pValueOut != nullptr ) {
        if constexpr ( std::is_nothrow_move_assignable_v<type_t> ) {
            *pValueOut = static_cast<type_t &&>( *pTop );
        } else if constexpr ( std::is_nothrow_copy_assignable_v<type_t> ) {
            *pValueOut = *pTop;
        } else {
            CY_ASSERT_MSG(
                CY_FALSE,
                "Stack_Pop output requires nothrow move or copy assignment." );
            return CY_FALSE;
        }
    }
    Vector_PopBack( &pStack->storage );
    return CY_TRUE;
}

template <typename type_t>
type_t *Stack_Top( cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Top requires a valid stack." );
    return bValidStack ? Vector_Back( &pStack->storage ) : nullptr;
}

template <typename type_t>
const type_t *Stack_Top( const cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Top requires a valid stack." );
    return bValidStack ? Vector_Back( &pStack->storage ) : nullptr;
}

template <typename type_t>
usize Stack_Count( const cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Count requires a valid stack." );
    return bValidStack ? Vector_Count( &pStack->storage ) : 0u;
}

template <typename type_t>
usize Stack_Capacity( const cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_Capacity requires a valid stack." );
    return bValidStack ? Vector_Capacity( &pStack->storage ) : 0u;
}

template <typename type_t>
bool_t Stack_IsEmpty( const cy_stack_t<type_t> *pStack ) noexcept
{
    const bool_t bValidStack = Stack_IsValid( pStack );
    CY_ASSERT_MSG( bValidStack, "Stack_IsEmpty requires a valid stack." );
    return bValidStack ? Vector_IsEmpty( &pStack->storage ) : CY_TRUE;
}

template <typename type_t>
void Stack_Move(
    cy_stack_t<type_t> *pDest,
    cy_stack_t<type_t> *pSource ) noexcept
{
    const bool_t bValidStacks = pDest != nullptr && pSource != nullptr;
    CY_ASSERT_MSG(
        bValidStacks,
        "Stack_Move requires source and destination stack objects." );
    // The adapter owns no state beyond its vector, so ownership transfer is delegated.
    if ( bValidStacks ) {
        Vector_Move( &pDest->storage, &pSource->storage );
    }
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STACK_INL
