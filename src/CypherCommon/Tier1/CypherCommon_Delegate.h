//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Delegate.h
//  Purpose: Declares non-owning type-erased callbacks.
//  Details: A delegate stores one object pointer and one dispatch thunk. It allocates
//           nothing and never extends the lifetime of a bound object.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DELEGATE_H
#define CYPHER_COMMON_TIER1_DELEGATE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename signature_t>
struct delegate_t;

template <typename return_t, typename... args_t>
struct delegate_t<return_t( args_t... )> {
    using invoke_fn_t = return_t ( * )( void *pObject, args_t... args ) noexcept;

    void *pObject{ nullptr };
    invoke_fn_t pfnInvoke{ nullptr };
};

template <typename return_t, typename... args_t>
CYPHER_NODISCARD bool_t Delegate_IsBound(
    const delegate_t<return_t( args_t... )> &delegate ) noexcept;

template <typename signature_t, auto pfnFunction>
CYPHER_NODISCARD delegate_t<signature_t> Delegate_BindFunction() noexcept;

template <typename signature_t, auto pfnMethod, typename object_t>
CYPHER_NODISCARD delegate_t<signature_t> Delegate_BindMethod(
    object_t *pObject ) noexcept;

template <typename return_t, typename... args_t>
return_t Delegate_Invoke(
    const delegate_t<return_t( args_t... )> &delegate,
    args_t... args ) noexcept;

template <typename return_t, typename... args_t>
void Delegate_Reset( delegate_t<return_t( args_t... )> *pDelegate ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DELEGATE_H
