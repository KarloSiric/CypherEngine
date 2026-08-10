//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SmartPtr.h
//  Purpose: Declares narrow unique and intrusive pointer ownership helpers.
//  Details: Tier1 intentionally omits a general shared/weak control-block system.
//           Prefer explicit ownership; use intrusive references only at proven boundaries.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SMARTPTR_H
#define CYPHER_COMMON_TIER1_SMARTPTR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
using pointer_destroy_fn_t = void ( * )(
    type_t *pObject,
    void *pUserData ) noexcept;

template <typename type_t>
struct unique_ptr_t {
    unique_ptr_t() noexcept = default;
    ~unique_ptr_t() noexcept;
    unique_ptr_t( const unique_ptr_t & ) = delete;
    unique_ptr_t &operator=( const unique_ptr_t & ) = delete;
    unique_ptr_t( unique_ptr_t &&other ) noexcept;
    unique_ptr_t &operator=( unique_ptr_t &&other ) noexcept;

    type_t *pObject{ nullptr };
    pointer_destroy_fn_t<type_t> pfnDestroy{ nullptr };
    void *pUserData{ nullptr };
};

template <typename type_t>
CYPHER_NODISCARD unique_ptr_t<type_t> UniquePtr_Make(
    type_t *pObject,
    pointer_destroy_fn_t<type_t> pfnDestroy,
    void *pUserData = nullptr ) noexcept;

template <typename type_t>
void UniquePtr_Reset( unique_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *UniquePtr_Release(
    unique_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *UniquePtr_Get(
    const unique_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t UniquePtr_IsValid(
    const unique_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
using intrusive_add_ref_fn_t = void ( * )( type_t *pObject ) noexcept;

template <typename type_t>
using intrusive_release_fn_t = void ( * )( type_t *pObject ) noexcept;

template <typename type_t>
struct intrusive_ptr_t {
    intrusive_ptr_t() noexcept = default;
    ~intrusive_ptr_t() noexcept;
    intrusive_ptr_t( const intrusive_ptr_t & ) = delete;
    intrusive_ptr_t &operator=( const intrusive_ptr_t & ) = delete;
    intrusive_ptr_t( intrusive_ptr_t &&other ) noexcept;
    intrusive_ptr_t &operator=( intrusive_ptr_t &&other ) noexcept;

    type_t *pObject{ nullptr };
    intrusive_add_ref_fn_t<type_t> pfnAddRef{ nullptr };
    intrusive_release_fn_t<type_t> pfnRelease{ nullptr };
};

template <typename type_t>
CYPHER_NODISCARD intrusive_ptr_t<type_t> IntrusivePtr_Acquire(
    type_t *pObject,
    intrusive_add_ref_fn_t<type_t> pfnAddRef,
    intrusive_release_fn_t<type_t> pfnRelease ) noexcept;

template <typename type_t>
void IntrusivePtr_Reset( intrusive_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD intrusive_ptr_t<type_t> IntrusivePtr_Copy(
    const intrusive_ptr_t<type_t> &pointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *IntrusivePtr_Get(
    const intrusive_ptr_t<type_t> *pPointer ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t IntrusivePtr_IsValid(
    const intrusive_ptr_t<type_t> *pPointer ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_SMARTPTR_INL
    #include "CypherCommon_SmartPtr.inl"
#endif

#endif // CYPHER_COMMON_TIER1_SMARTPTR_H
