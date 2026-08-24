//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Delegate.inl
//  Purpose: Implements non-owning type-erased callbacks.
//  Details: Compile-time thunks erase free functions, methods, and borrowed callable
//           objects without allocation or virtual dispatch.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DELEGATE_INL
#define CYPHER_COMMON_TIER1_DELEGATE_INL

#ifndef CYPHER_COMMON_TIER1_DELEGATE_H
    #include "CypherCommon_Delegate.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <type_traits>

namespace cypher::common
{

template <typename return_t, typename... args_t>
bool_t Delegate_IsBound(
    const delegate_t<return_t( args_t... )> &delegate ) noexcept
{
    return delegate.pfnInvoke != nullptr;
}

template <typename signature_t, auto pfnFunction>
struct delegate_function_binder_t;

template <auto pfnFunction, typename return_t, typename... args_t>
struct delegate_function_binder_t<return_t( args_t... ), pfnFunction> {
    static delegate_t<return_t( args_t... )> Bind() noexcept
    {
        static_assert(
            pfnFunction != nullptr,
            "Delegate target must not be null." );
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                decltype( pfnFunction ),
                args_t...>,
            "Delegate free function must match the signature and be noexcept." );

        // A compile-time thunk supplies the concrete function while preserving
        // one uniform (void*, args...) invocation signature.
        delegate_t<return_t( args_t... )> delegate{};
        delegate.pfnInvoke = []( void *, args_t... args ) noexcept -> return_t {
            return pfnFunction( static_cast<args_t &&>( args )... );
        };
        return delegate;
    }
};

template <typename signature_t, auto pfnFunction>
delegate_t<signature_t> Delegate_BindFunction() noexcept
{
    return delegate_function_binder_t<signature_t, pfnFunction>::Bind();
}

template <typename signature_t, auto pfnMethod, typename object_t>
struct delegate_method_binder_t;

template <
    auto pfnMethod,
    typename object_t,
    typename return_t,
    typename... args_t>
struct delegate_method_binder_t<
    return_t( args_t... ),
    pfnMethod,
    object_t> {
    static delegate_t<return_t( args_t... )> Bind(
        object_t *pObject ) noexcept
    {
        static_assert(
            std::is_member_function_pointer_v<decltype( pfnMethod )>,
            "Delegate_BindMethod requires a member function pointer." );
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                decltype( pfnMethod ),
                object_t &,
                args_t...>,
            "Delegate method must match the signature and be noexcept." );

        delegate_t<return_t( args_t... )> delegate{};
        if ( pObject == nullptr ) {
            return delegate;
        }
        // The delegate borrows this object. The caller must keep it alive until
        // every copy of the delegate has been reset or discarded.
        delegate.pObject = pObject;
        delegate.pfnInvoke = []( void *pBoundObject, args_t... args ) noexcept
            -> return_t {
            object_t &object = *static_cast<object_t *>( pBoundObject );
            return ( object.*pfnMethod )(
                static_cast<args_t &&>( args )... );
        };
        return delegate;
    }

    static delegate_t<return_t( args_t... )> Bind(
        const object_t *pObject ) noexcept
    {
        static_assert(
            std::is_member_function_pointer_v<decltype( pfnMethod )>,
            "Delegate_BindMethod requires a member function pointer." );
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                decltype( pfnMethod ),
                const object_t &,
                args_t...>,
            "Const delegate method must match the signature and be noexcept." );

        delegate_t<return_t( args_t... )> delegate{};
        if ( pObject == nullptr ) {
            return delegate;
        }
        // Erased storage is void*, but the thunk restores const before invocation.
        delegate.pObject = const_cast<object_t *>( pObject );
        delegate.pfnInvoke = []( void *pBoundObject, args_t... args ) noexcept
            -> return_t {
            const object_t &object =
                *static_cast<const object_t *>( pBoundObject );
            return ( object.*pfnMethod )(
                static_cast<args_t &&>( args )... );
        };
        return delegate;
    }
};

template <typename signature_t, auto pfnMethod, typename object_t>
delegate_t<signature_t> Delegate_BindMethod( object_t *pObject ) noexcept
{
    return delegate_method_binder_t<
        signature_t,
        pfnMethod,
        object_t>::Bind( pObject );
}

template <typename signature_t, auto pfnMethod, typename object_t>
delegate_t<signature_t> Delegate_BindMethod(
    const object_t *pObject ) noexcept
{
    return delegate_method_binder_t<
        signature_t,
        pfnMethod,
        object_t>::Bind( pObject );
}

template <typename signature_t, typename callable_t>
struct delegate_callable_binder_t;

template <typename callable_t, typename return_t, typename... args_t>
struct delegate_callable_binder_t<return_t( args_t... ), callable_t> {
    static delegate_t<return_t( args_t... )> Bind(
        callable_t *pCallable ) noexcept
    {
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                callable_t &,
                args_t...>,
            "Delegate callable must match the signature and be noexcept." );

        delegate_t<return_t( args_t... )> delegate{};
        if ( pCallable == nullptr ) {
            return delegate;
        }
        delegate.pObject = pCallable;
        delegate.pfnInvoke = []( void *pObject, args_t... args ) noexcept
            -> return_t {
            return ( *static_cast<callable_t *>( pObject ) )(
                static_cast<args_t &&>( args )... );
        };
        return delegate;
    }

    static delegate_t<return_t( args_t... )> Bind(
        const callable_t *pCallable ) noexcept
    {
        static_assert(
            std::is_nothrow_invocable_r_v<
                return_t,
                const callable_t &,
                args_t...>,
            "Const delegate callable must match the signature and be noexcept." );

        delegate_t<return_t( args_t... )> delegate{};
        if ( pCallable == nullptr ) {
            return delegate;
        }
        delegate.pObject = const_cast<callable_t *>( pCallable );
        delegate.pfnInvoke = []( void *pObject, args_t... args ) noexcept
            -> return_t {
            return ( *static_cast<const callable_t *>( pObject ) )(
                static_cast<args_t &&>( args )... );
        };
        return delegate;
    }
};

template <typename signature_t, typename callable_t>
delegate_t<signature_t> Delegate_BindCallable(
    callable_t *pCallable ) noexcept
{
    return delegate_callable_binder_t<signature_t, callable_t>::Bind(
        pCallable );
}

template <typename signature_t, typename callable_t>
delegate_t<signature_t> Delegate_BindCallable(
    const callable_t *pCallable ) noexcept
{
    return delegate_callable_binder_t<signature_t, callable_t>::Bind(
        pCallable );
}

template <typename return_t, typename... args_t>
return_t Delegate_Invoke(
    const delegate_t<return_t( args_t... )> &delegate,
    std::type_identity_t<args_t>... args ) noexcept
{
    const bool_t bBound = Delegate_IsBound( delegate );
    CY_ASSERT_MSG( bBound, "Delegate_Invoke requires a bound delegate." );
    // Invocation is intentionally unchecked in release after the contract
    // assertion; calling an unbound delegate is a programmer error.
    return delegate.pfnInvoke(
        delegate.pObject,
        static_cast<args_t &&>( args )... );
}

template <typename return_t, typename... args_t>
void Delegate_Reset(
    delegate_t<return_t( args_t... )> *pDelegate ) noexcept
{
    const bool_t bValidDestination = pDelegate != nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "Delegate_Reset requires a delegate object." );
    if ( bValidDestination ) {
        pDelegate->pObject = nullptr;
        pDelegate->pfnInvoke = nullptr;
    }
}

template <typename return_t, typename... args_t>
bool_t Delegate_Equals(
    const delegate_t<return_t( args_t... )> &left,
    const delegate_t<return_t( args_t... )> &right ) noexcept
{
    return left.pObject == right.pObject &&
           left.pfnInvoke == right.pfnInvoke;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DELEGATE_INL
