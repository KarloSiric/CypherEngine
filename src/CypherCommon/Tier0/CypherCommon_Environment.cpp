//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Environment.cpp
//  Purpose: Implements CypherCommon Tier0 environment variable helpers.
//  Details: Tools and runtime diagnostics use this layer instead of scattering
//           platform-specific environment access.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Environment.h"

#include "CypherCommon_Platform.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace cypher::common
{

// POSIX environment access is process-global and not consistently thread-safe
// across implementations. Serialize all reads and writes behind one lock.
namespace
{

std::mutex g_environmentMutex;

bool_t Environment_IsValidName( const char *pszName ) noexcept
{
    if ( pszName == nullptr || pszName[0] == '\0' ) {
        return CY_FALSE;
    }

    // The native environment representation is NAME=VALUE. An '=' in NAME would
    // make the entry ambiguous and is rejected on every platform.
    for ( const char *pszRead = pszName; *pszRead != '\0'; ++pszRead ) {
        if ( *pszRead == '=' ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

#if CYPHER_PLATFORM_WINDOWS
wchar_t *Environment_Utf8ToWide( const char *pszValue ) noexcept
{
    if ( pszValue == nullptr ) {
        return nullptr;
    }

    // Query exact UTF-16 storage including NUL before allocating. Invalid UTF-8
    // is rejected rather than silently replaced by the Windows conversion API.
    const int cchRequired = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        pszValue,
        -1,
        nullptr,
        0 );
    if ( cchRequired <= 0 ||
         static_cast<usize>( cchRequired ) > CY_USIZE_MAX / sizeof( wchar_t ) ) {
        return nullptr;
    }

    auto *pWide = static_cast<wchar_t *>(
        std::malloc( static_cast<usize>( cchRequired ) * sizeof( wchar_t ) ) );
    if ( pWide == nullptr ) {
        return nullptr;
    }

    if ( ::MultiByteToWideChar(
             CP_UTF8,
             MB_ERR_INVALID_CHARS,
             pszValue,
             -1,
             pWide,
             cchRequired ) == 0 ) {
        std::free( pWide );
        return nullptr;
    }
    return pWide;
}
#endif

} // namespace

cy_environment_get_result_t Cy_EnvironmentGet(
    const char *pszName,
    char *pszDst,
    usize cchDst ) noexcept
{
    // A failed or truncated read always leaves caller storage as an empty string.
    cy_environment_get_result_t result = {};
    if ( pszDst != nullptr && cchDst > 0u ) {
        pszDst[0] = '\0';
        pszDst[cchDst - 1u] = '\0';
    }
    if ( !Environment_IsValidName( pszName ) ) {
        return result;
    }

    try {
        std::lock_guard<std::mutex> lock( g_environmentMutex );

#if CYPHER_PLATFORM_WINDOWS
        wchar_t *pWideName = Environment_Utf8ToWide( pszName );
        if ( pWideName == nullptr ) {
            return result;
        }

        // GetEnvironmentVariableW returns zero for both missing and present-empty
        // values. GetLastError distinguishes those two public API states.
        wchar_t wszProbe[1] = {};
        ::SetLastError( ERROR_SUCCESS );
        const DWORD cchWideRequired =
            ::GetEnvironmentVariableW( pWideName, wszProbe, 1u );
        if ( cchWideRequired == 0u ) {
            result.exists = ::GetLastError() == ERROR_SUCCESS;
            std::free( pWideName );
            return result;
        }

        auto *pWideValue = static_cast<wchar_t *>(
            std::malloc(
                static_cast<usize>( cchWideRequired ) * sizeof( wchar_t ) ) );
        if ( pWideValue == nullptr ) {
            std::free( pWideName );
            return result;
        }

        const DWORD cchWideWritten = ::GetEnvironmentVariableW(
            pWideName,
            pWideValue,
            cchWideRequired );
        std::free( pWideName );
        if ( cchWideWritten == 0u || cchWideWritten >= cchWideRequired ) {
            std::free( pWideValue );
            return result;
        }

        const int cchUtf8Required = ::WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            pWideValue,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr );
        if ( cchUtf8Required <= 0 ) {
            std::free( pWideValue );
            return result;
        }

        result.exists = CY_TRUE;
        // WideCharToMultiByte's required count includes NUL; the public result does not.
        result.cchRequired = static_cast<usize>( cchUtf8Required - 1 );
        if ( pszDst != nullptr && cchDst > 0u ) {
            if ( cchDst >= static_cast<usize>( cchUtf8Required ) ) {
                const int cchWritten = ::WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    pWideValue,
                    -1,
                    pszDst,
                    cchUtf8Required,
                    nullptr,
                    nullptr );
                if ( cchWritten <= 0 ) {
                    pszDst[0] = '\0';
                    result.exists = CY_FALSE;
                    result.cchRequired = 0u;
                }
            } else {
                result.isTruncated = CY_TRUE;
            }
        }
        std::free( pWideValue );
#else
        // getenv returns storage owned by the C runtime. Copy it while holding the
        // environment mutex because another thread may replace the variable.
        const char *pszValue = std::getenv( pszName );
        if ( pszValue == nullptr ) {
            return result;
        }

        result.exists = CY_TRUE;
        result.cchRequired = std::strlen( pszValue );
        if ( pszDst != nullptr && cchDst > 0u ) {
            if ( result.cchRequired < cchDst ) {
                std::memmove( pszDst, pszValue, result.cchRequired + 1u );
            } else {
                result.isTruncated = CY_TRUE;
            }
        }
#endif
    } catch ( ... ) {
        if ( pszDst != nullptr && cchDst > 0u ) {
            pszDst[0] = '\0';
        }
        return {};
    }

    return result;
}

bool_t Cy_EnvironmentSet(
    const char *pszName,
    const char *pszValue ) noexcept
{
    if ( !Environment_IsValidName( pszName ) || pszValue == nullptr ) {
        return CY_FALSE;
    }

    try {
        std::lock_guard<std::mutex> lock( g_environmentMutex );
#if CYPHER_PLATFORM_WINDOWS
        wchar_t *pWideName = Environment_Utf8ToWide( pszName );
        wchar_t *pWideValue = Environment_Utf8ToWide( pszValue );
        if ( pWideName == nullptr || pWideValue == nullptr ) {
            std::free( pWideName );
            std::free( pWideValue );
            return CY_FALSE;
        }

        const bool_t didSet =
            ::SetEnvironmentVariableW( pWideName, pWideValue ) != FALSE;
        std::free( pWideName );
        std::free( pWideValue );
        return didSet;
#else
        return ::setenv( pszName, pszValue, 1 ) == 0;
#endif
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_EnvironmentUnset( const char *pszName ) noexcept
{
    if ( !Environment_IsValidName( pszName ) ) {
        return CY_FALSE;
    }

    try {
        std::lock_guard<std::mutex> lock( g_environmentMutex );
#if CYPHER_PLATFORM_WINDOWS
        wchar_t *pWideName = Environment_Utf8ToWide( pszName );
        if ( pWideName == nullptr ) {
            return CY_FALSE;
        }

        ::SetLastError( ERROR_SUCCESS );
        // Removing an already-missing value is idempotent and therefore succeeds.
        const bool_t didUnset =
            ::SetEnvironmentVariableW( pWideName, nullptr ) != FALSE ||
            ::GetLastError() == ERROR_ENVVAR_NOT_FOUND;
        std::free( pWideName );
        return didUnset;
#else
        return ::unsetenv( pszName ) == 0;
#endif
    } catch ( ... ) {
        return CY_FALSE;
    }
}

bool_t Cy_EnvironmentHas( const char *pszName ) noexcept
{
    return Cy_EnvironmentGet( pszName, nullptr, 0u ).exists;
}

} // namespace cypher::common
