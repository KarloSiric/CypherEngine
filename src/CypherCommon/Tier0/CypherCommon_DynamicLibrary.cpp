//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_DynamicLibrary.cpp
//  Purpose: Implements CypherCommon Tier0 dynamic library helpers.
//  Details: Dynamic library loading is the foundation for future game modules,
//           tools, editor plugins, and hot-reload experiments.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_DynamicLibrary.h"

#include "CypherCommon_Platform.h"

#include <cstdio>
#include <cstdlib>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_POSIX
    #include <dlfcn.h>
#endif

namespace cypher::common
{

namespace
{

void DynamicLibrary_SetError(
    dynamic_library_t *pLibrary,
    const char *pszError ) noexcept
{
    if ( pLibrary == nullptr ) {
        return;
    }

    const char *pszRead = pszError != nullptr ? pszError : "";
    usize i = 0u;
    while ( i + 1u < CY_DYNAMIC_LIBRARY_ERROR_MAX && pszRead[i] != '\0' ) {
        pLibrary->szLastError[i] = pszRead[i];
        ++i;
    }
    pLibrary->szLastError[i] = '\0';
}

#if CYPHER_PLATFORM_WINDOWS
void DynamicLibrary_SetWindowsError(
    dynamic_library_t *pLibrary,
    DWORD nError ) noexcept
{
    wchar_t wszMessage[CY_DYNAMIC_LIBRARY_ERROR_MAX] = {};
    const DWORD cchMessage = ::FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        nError,
        0u,
        wszMessage,
        static_cast<DWORD>( CY_DYNAMIC_LIBRARY_ERROR_MAX ),
        nullptr );
    if ( cchMessage != 0u ) {
        const int cchUtf8 = ::WideCharToMultiByte(
            CP_UTF8,
            0u,
            wszMessage,
            static_cast<int>( cchMessage ),
            pLibrary->szLastError,
            static_cast<int>( CY_DYNAMIC_LIBRARY_ERROR_MAX - 1u ),
            nullptr,
            nullptr );
        if ( cchUtf8 > 0 ) {
            pLibrary->szLastError[static_cast<usize>( cchUtf8 )] = '\0';
            return;
        }
    }

    std::snprintf(
        pLibrary->szLastError,
        CY_DYNAMIC_LIBRARY_ERROR_MAX,
        "Windows loader error %lu",
        static_cast<unsigned long>( nError ) );
    pLibrary->szLastError[CY_DYNAMIC_LIBRARY_ERROR_MAX - 1u] = '\0';
}

wchar_t *DynamicLibrary_Utf8PathToWide( const char *pszPath ) noexcept
{
    const int cchRequired = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        pszPath,
        -1,
        nullptr,
        0 );
    if ( cchRequired <= 0 ||
         static_cast<usize>( cchRequired ) > CY_USIZE_MAX / sizeof( wchar_t ) ) {
        return nullptr;
    }

    auto *pWidePath = static_cast<wchar_t *>(
        std::malloc( static_cast<usize>( cchRequired ) * sizeof( wchar_t ) ) );
    if ( pWidePath == nullptr ) {
        return nullptr;
    }

    if ( ::MultiByteToWideChar(
             CP_UTF8,
             MB_ERR_INVALID_CHARS,
             pszPath,
             -1,
             pWidePath,
             cchRequired ) == 0 ) {
        std::free( pWidePath );
        return nullptr;
    }
    return pWidePath;
}
#endif

} // namespace

bool_t Cy_DynamicLibraryInit( dynamic_library_t *pLibrary ) noexcept
{
    if ( pLibrary == nullptr ) {
        return CY_FALSE;
    }
    if ( pLibrary->pHandle != nullptr ) {
        DynamicLibrary_SetError( pLibrary, "library is still loaded" );
        return CY_FALSE;
    }

    *pLibrary = {};
    return CY_TRUE;
}

bool_t Cy_DynamicLibraryLoad(
    dynamic_library_t *pLibrary,
    const char *pszPath ) noexcept
{
    return Cy_DynamicLibraryLoadEx(
        pLibrary,
        pszPath,
        CY_DYNAMIC_LIBRARY_NONE );
}

bool_t Cy_DynamicLibraryLoadEx(
    dynamic_library_t *pLibrary,
    const char *pszPath,
    flags32_t flags ) noexcept
{
    constexpr flags32_t VALID_FLAGS =
        CY_DYNAMIC_LIBRARY_RESOLVE_LAZY |
        CY_DYNAMIC_LIBRARY_GLOBAL_SYMBOLS;
    if ( pLibrary == nullptr ) {
        return CY_FALSE;
    }
    if ( pszPath == nullptr || pszPath[0] == '\0' ) {
        DynamicLibrary_SetError( pLibrary, "invalid dynamic-library path" );
        return CY_FALSE;
    }
    if ( pLibrary->pHandle != nullptr ) {
        DynamicLibrary_SetError( pLibrary, "library is already loaded" );
        return CY_FALSE;
    }
    if ( ( flags & ~VALID_FLAGS ) != 0u ) {
        DynamicLibrary_SetError( pLibrary, "invalid dynamic-library flags" );
        return CY_FALSE;
    }
    DynamicLibrary_SetError( pLibrary, "" );

#if CYPHER_PLATFORM_WINDOWS
    wchar_t *pWidePath = DynamicLibrary_Utf8PathToWide( pszPath );
    if ( pWidePath == nullptr ) {
        DynamicLibrary_SetError( pLibrary, "failed to convert dynamic-library path to UTF-16" );
        return CY_FALSE;
    }

    HMODULE hModule = ::LoadLibraryExW(
        pWidePath,
        nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS );
    std::free( pWidePath );
    if ( hModule == nullptr ) {
        DynamicLibrary_SetWindowsError( pLibrary, ::GetLastError() );
        return CY_FALSE;
    }
    pLibrary->pHandle = reinterpret_cast<void *>( hModule );
#elif CYPHER_PLATFORM_POSIX
    const int nResolveMode =
        ( flags & CY_DYNAMIC_LIBRARY_RESOLVE_LAZY ) != 0u ? RTLD_LAZY : RTLD_NOW;
    const int nVisibility =
        ( flags & CY_DYNAMIC_LIBRARY_GLOBAL_SYMBOLS ) != 0u ? RTLD_GLOBAL : RTLD_LOCAL;
    ::dlerror();
    pLibrary->pHandle = ::dlopen( pszPath, nResolveMode | nVisibility );
    if ( pLibrary->pHandle == nullptr ) {
        DynamicLibrary_SetError( pLibrary, ::dlerror() );
        return CY_FALSE;
    }
#else
    CYPHER_UNUSED( flags );
    DynamicLibrary_SetError( pLibrary, "dynamic libraries are unsupported" );
    return CY_FALSE;
#endif

    return CY_TRUE;
}

bool_t Cy_DynamicLibraryUnload( dynamic_library_t *pLibrary ) noexcept
{
    if ( pLibrary == nullptr ) {
        return CY_FALSE;
    }
    if ( pLibrary->pHandle == nullptr ) {
        DynamicLibrary_SetError( pLibrary, "" );
        return CY_TRUE;
    }

#if CYPHER_PLATFORM_WINDOWS
    if ( ::FreeLibrary( reinterpret_cast<HMODULE>( pLibrary->pHandle ) ) == FALSE ) {
        DynamicLibrary_SetWindowsError( pLibrary, ::GetLastError() );
        return CY_FALSE;
    }
#elif CYPHER_PLATFORM_POSIX
    if ( ::dlclose( pLibrary->pHandle ) != 0 ) {
        DynamicLibrary_SetError( pLibrary, ::dlerror() );
        return CY_FALSE;
    }
#endif

    pLibrary->pHandle = nullptr;
    DynamicLibrary_SetError( pLibrary, "" );
    return CY_TRUE;
}

bool_t Cy_DynamicLibraryIsLoaded(
    const dynamic_library_t *pLibrary ) noexcept
{
    return pLibrary != nullptr && pLibrary->pHandle != nullptr;
}

void *Cy_DynamicLibraryGetSymbol(
    dynamic_library_t *pLibrary,
    const char *pszSymbolName ) noexcept
{
    if ( pLibrary == nullptr ||
         pLibrary->pHandle == nullptr ||
         pszSymbolName == nullptr ||
         pszSymbolName[0] == '\0' ) {
        if ( pLibrary != nullptr ) {
            DynamicLibrary_SetError( pLibrary, "invalid symbol lookup" );
        }
        return nullptr;
    }
    DynamicLibrary_SetError( pLibrary, "" );

#if CYPHER_PLATFORM_WINDOWS
    FARPROC pSymbol = ::GetProcAddress(
        reinterpret_cast<HMODULE>( pLibrary->pHandle ),
        pszSymbolName );
    if ( pSymbol == nullptr ) {
        DynamicLibrary_SetWindowsError( pLibrary, ::GetLastError() );
        return nullptr;
    }
    return reinterpret_cast<void *>( pSymbol );
#elif CYPHER_PLATFORM_POSIX
    ::dlerror();
    void *pSymbol = ::dlsym( pLibrary->pHandle, pszSymbolName );
    const char *pszError = ::dlerror();
    if ( pszError != nullptr ) {
        DynamicLibrary_SetError( pLibrary, pszError );
        return nullptr;
    }
    return pSymbol;
#else
    DynamicLibrary_SetError( pLibrary, "dynamic libraries are unsupported" );
    return nullptr;
#endif
}

const char *Cy_DynamicLibraryGetLastError(
    const dynamic_library_t *pLibrary ) noexcept
{
    return pLibrary != nullptr ? pLibrary->szLastError : "invalid library handle";
}

} // namespace cypher::common
