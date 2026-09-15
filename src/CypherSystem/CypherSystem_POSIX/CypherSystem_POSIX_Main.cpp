//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_POSIX/CypherSystem_POSIX_Main.cpp
//  Purpose: Implements operating-system services shared by POSIX targets.
//  Details: This unit is the root POSIX adapter, not a process entry point.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Platform.h"

#if CYPHER_PLATFORM_POSIX

#include "CypherSystem_Local.h"

#include <cerrno> // errno and EINTR used while resuming interrupted sleeps.
#include <ctime>  // nanosleep and localtime_r.

namespace cypher::engine::sys
{

void Sys_PlatformSleepMilliseconds( const common::u64 milliseconds ) noexcept
{
    timespec request{};
    request.tv_sec = static_cast<time_t>( milliseconds / 1000u );
    request.tv_nsec = static_cast<long>( ( milliseconds % 1000u ) * 1000000u );

    while ( nanosleep( &request, &request ) == -1 && errno == EINTR ) {
        // Continue sleeping for the unslept interval after an interrupt signal.
    }
}

bool Sys_PlatformLocalTime( const std::time_t timeValue, std::tm &timeOut ) noexcept
{
    return localtime_r( &timeValue, &timeOut ) != nullptr;
}

} // namespace cypher::engine::sys

#endif // CYPHER_PLATFORM_POSIX
