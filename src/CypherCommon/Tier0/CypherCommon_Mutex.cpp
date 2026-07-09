//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Mutex.cpp
//  Purpose: Implements CypherCommon Tier0 mutex primitives.
//  Details: This file provides small, portable mutual exclusion wrappers used by
//           diagnostics, resource tables, async systems, and future containers.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Mutex.h"

namespace cypher::common
{

bool_t Cy_MutexInit( cy_mutex_t *pMutex )
{
    if ( pMutex == nullptr ) {
        return CY_FALSE;
    }

    pMutex->bInitialized = CY_TRUE;
    return CY_TRUE;
}

void Cy_MutexShutdown( cy_mutex_t *pMutex )
{
    if ( pMutex == nullptr ) {
        return;
    }

    pMutex->bInitialized = CY_FALSE;
}

bool_t Cy_MutexIsInitialized( const cy_mutex_t *pMutex )
{
    return pMutex != nullptr && pMutex->bInitialized;
}

void Cy_MutexLock( cy_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return;
    }

    pMutex->native.lock();
}

bool_t Cy_MutexTryLock( cy_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return CY_FALSE;
    }

    return pMutex->native.try_lock();
}

void Cy_MutexUnlock( cy_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return;
    }

    pMutex->native.unlock();
}

bool_t Cy_RecursiveMutexInit( cy_recursive_mutex_t *pMutex )
{
    if ( pMutex == nullptr ) {
        return CY_FALSE;
    }

    pMutex->bInitialized = CY_TRUE;
    return CY_TRUE;
}

void Cy_RecursiveMutexShutdown( cy_recursive_mutex_t *pMutex )
{
    if ( pMutex == nullptr ) {
        return;
    }

    pMutex->bInitialized = CY_FALSE;
}

bool_t Cy_RecursiveMutexIsInitialized( const cy_recursive_mutex_t *pMutex )
{
    return pMutex != nullptr && pMutex->bInitialized;
}

void Cy_RecursiveMutexLock( cy_recursive_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return;
    }

    pMutex->native.lock();
}

bool_t Cy_RecursiveMutexTryLock( cy_recursive_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return CY_FALSE;
    }

    return pMutex->native.try_lock();
}

void Cy_RecursiveMutexUnlock( cy_recursive_mutex_t *pMutex )
{
    if ( pMutex == nullptr || !pMutex->bInitialized ) {
        return;
    }

    pMutex->native.unlock();
}

} // namespace cypher::common
