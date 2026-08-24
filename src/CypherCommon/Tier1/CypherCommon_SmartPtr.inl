//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SmartPtr.inl
//  Purpose: Implements narrow unique and intrusive ownership helpers.
//  Details: Ownership callbacks remain explicit and allocation policy stays outside
//           the pointer wrappers. Move operations transfer ownership destructively.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Smart Ptr Template Definitions

This dependency-light Tier1 utility keeps ownership, capacity, and failure behavior explicit so
higher engine systems can use it without hidden allocation or platform state. Template
definitions remain in this file so each concrete instantiation is compiled at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_SMARTPTR_INL
#define CYPHER_COMMON_TIER1_SMARTPTR_INL

#ifndef CYPHER_COMMON_TIER1_SMARTPTR_H
    #include "CypherCommon_SmartPtr.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t>
unique_ptr_t<type_t>::~unique_ptr_t() noexcept
{
    UniquePtr_Reset( this );
}

template <typename type_t>
unique_ptr_t<type_t>::unique_ptr_t( unique_ptr_t &&other ) noexcept
    : pObject( other.pObject ),
      pfnDestroy( other.pfnDestroy ),
      pUserData( other.pUserData )
{
    other.pObject = nullptr;
    other.pfnDestroy = nullptr;
    other.pUserData = nullptr;
}

template <typename type_t>
unique_ptr_t<type_t> &unique_ptr_t<type_t>::operator=(
    unique_ptr_t &&other ) noexcept
{
    if ( this == &other ) {
        return *this;
    }

    UniquePtr_Reset( this );
    pObject = other.pObject;
    pfnDestroy = other.pfnDestroy;
    pUserData = other.pUserData;
    other.pObject = nullptr;
    other.pfnDestroy = nullptr;
    other.pUserData = nullptr;
    return *this;
}

template <typename type_t>
unique_ptr_t<type_t> UniquePtr_Make(
    type_t *pObject,
    pointer_destroy_fn_t<type_t> pfnDestroy,
    void *pUserData ) noexcept
{
    const bool_t bValidOwner = pObject == nullptr || pfnDestroy != nullptr;
    CY_ASSERT_MSG(
        bValidOwner,
        "UniquePtr_Make requires a destroy callback for a live object." );
    if ( !bValidOwner || pObject == nullptr ) {
        return {};
    }

    unique_ptr_t<type_t> result{};
    result.pObject = pObject;
    result.pfnDestroy = pfnDestroy;
    result.pUserData = pUserData;
    return result;
}

template <typename type_t>
void UniquePtr_Reset( unique_ptr_t<type_t> *pPointer ) noexcept
{
    CY_ASSERT_MSG( pPointer != nullptr, "UniquePtr_Reset requires a pointer wrapper." );
    if ( pPointer == nullptr ) {
        return;
    }

    // Clear ownership before entering user code so a re-entrant destroy callback
    // cannot observe or release the same object twice through this wrapper.
    type_t *pObject = pPointer->pObject;
    pointer_destroy_fn_t<type_t> pfnDestroy = pPointer->pfnDestroy;
    void *pUserData = pPointer->pUserData;
    pPointer->pObject = nullptr;
    pPointer->pfnDestroy = nullptr;
    pPointer->pUserData = nullptr;
    if ( pObject != nullptr ) {
        CY_ASSERT_MSG(
            pfnDestroy != nullptr,
            "A live unique pointer must retain its destroy callback." );
        if ( pfnDestroy != nullptr ) {
            pfnDestroy( pObject, pUserData ); // The callback owns the concrete destruction policy.
        }
    }
}

template <typename type_t>
type_t *UniquePtr_Release( unique_ptr_t<type_t> *pPointer ) noexcept
{
    CY_ASSERT_MSG( pPointer != nullptr, "UniquePtr_Release requires a pointer wrapper." );
    if ( pPointer == nullptr ) {
        return nullptr;
    }

    type_t *pObject = pPointer->pObject; // Ownership transfers without invoking destruction.
    pPointer->pObject = nullptr;
    pPointer->pfnDestroy = nullptr;
    pPointer->pUserData = nullptr;
    return pObject;
}

template <typename type_t>
type_t *UniquePtr_Get( const unique_ptr_t<type_t> *pPointer ) noexcept
{
    return pPointer != nullptr ? pPointer->pObject : nullptr;
}

template <typename type_t>
bool_t UniquePtr_IsValid( const unique_ptr_t<type_t> *pPointer ) noexcept
{
    return pPointer != nullptr &&
           ( pPointer->pObject == nullptr || pPointer->pfnDestroy != nullptr );
}

template <typename type_t>
intrusive_ptr_t<type_t>::~intrusive_ptr_t() noexcept
{
    IntrusivePtr_Reset( this );
}

template <typename type_t>
intrusive_ptr_t<type_t>::intrusive_ptr_t( intrusive_ptr_t &&other ) noexcept
    : pObject( other.pObject ),
      pfnAddRef( other.pfnAddRef ),
      pfnRelease( other.pfnRelease )
{
    other.pObject = nullptr;
    other.pfnAddRef = nullptr;
    other.pfnRelease = nullptr;
}

template <typename type_t>
intrusive_ptr_t<type_t> &intrusive_ptr_t<type_t>::operator=(
    intrusive_ptr_t &&other ) noexcept
{
    if ( this == &other ) {
        return *this;
    }

    IntrusivePtr_Reset( this );
    pObject = other.pObject;
    pfnAddRef = other.pfnAddRef;
    pfnRelease = other.pfnRelease;
    other.pObject = nullptr;
    other.pfnAddRef = nullptr;
    other.pfnRelease = nullptr;
    return *this;
}

template <typename type_t>
intrusive_ptr_t<type_t> IntrusivePtr_Acquire(
    type_t *pObject,
    intrusive_add_ref_fn_t<type_t> pfnAddRef,
    intrusive_release_fn_t<type_t> pfnRelease ) noexcept
{
    const bool_t bValidCallbacks =
        pObject == nullptr || ( pfnAddRef != nullptr && pfnRelease != nullptr );
    CY_ASSERT_MSG(
        bValidCallbacks,
        "IntrusivePtr_Acquire requires add-reference and release callbacks." );
    if ( !bValidCallbacks || pObject == nullptr ) {
        return {};
    }

    pfnAddRef( pObject );
    intrusive_ptr_t<type_t> result{};
    result.pObject = pObject;
    result.pfnAddRef = pfnAddRef;
    result.pfnRelease = pfnRelease;
    return result;
}

template <typename type_t>
void IntrusivePtr_Reset( intrusive_ptr_t<type_t> *pPointer ) noexcept
{
    CY_ASSERT_MSG(
        pPointer != nullptr,
        "IntrusivePtr_Reset requires a pointer wrapper." );
    if ( pPointer == nullptr ) {
        return;
    }

    // Detach first for the same re-entrancy reason as UniquePtr_Reset. The object's
    // release callback decides whether the reference count reaches final destruction.
    type_t *pObject = pPointer->pObject;
    intrusive_release_fn_t<type_t> pfnRelease = pPointer->pfnRelease;
    pPointer->pObject = nullptr;
    pPointer->pfnAddRef = nullptr;
    pPointer->pfnRelease = nullptr;
    if ( pObject != nullptr ) {
        CY_ASSERT_MSG(
            pfnRelease != nullptr,
            "A live intrusive pointer must retain its release callback." );
        if ( pfnRelease != nullptr ) {
            pfnRelease( pObject );
        }
    }
}

template <typename type_t>
intrusive_ptr_t<type_t> IntrusivePtr_Copy(
    const intrusive_ptr_t<type_t> &pointer ) noexcept
{
    const bool_t bValidPointer = IntrusivePtr_IsValid( &pointer );
    CY_ASSERT_MSG(
        bValidPointer,
        "IntrusivePtr_Copy requires a valid pointer wrapper." );
    if ( !bValidPointer || pointer.pObject == nullptr ) {
        return {};
    }

    pointer.pfnAddRef( pointer.pObject ); // Acquire the new reference before publishing the copy.
    intrusive_ptr_t<type_t> result{};
    result.pObject = pointer.pObject;
    result.pfnAddRef = pointer.pfnAddRef;
    result.pfnRelease = pointer.pfnRelease;
    return result;
}

template <typename type_t>
type_t *IntrusivePtr_Get( const intrusive_ptr_t<type_t> *pPointer ) noexcept
{
    return pPointer != nullptr ? pPointer->pObject : nullptr;
}

template <typename type_t>
bool_t IntrusivePtr_IsValid( const intrusive_ptr_t<type_t> *pPointer ) noexcept
{
    return pPointer != nullptr &&
           ( pPointer->pObject == nullptr ||
             ( pPointer->pfnAddRef != nullptr && pPointer->pfnRelease != nullptr ) );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SMARTPTR_INL
