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

bool_t DynamicLibrary_Load( dynamic_library_t *pLibrary, const char *pPath )
{
    if ( pLibrary == nullptr || pPath == nullptr || pPath[0] == '\0' ) {
        return CY_FALSE;
    }

    DynamicLibrary_Unload( pLibrary );

#if CYPHER_PLATFORM_WINDOWS
    pLibrary->pHandle = reinterpret_cast<void *>( ::LoadLibraryA( pPath ) );
#elif CYPHER_PLATFORM_POSIX
    pLibrary->pHandle = ::dlopen( pPath, RTLD_NOW | RTLD_LOCAL );
#else
    pLibrary->pHandle = nullptr;
#endif

    return pLibrary->pHandle != nullptr;
}

void DynamicLibrary_Unload( dynamic_library_t *pLibrary )
{
    if ( pLibrary == nullptr || pLibrary->pHandle == nullptr ) {
        return;
    }

#if CYPHER_PLATFORM_WINDOWS
    ::FreeLibrary( reinterpret_cast<HMODULE>( pLibrary->pHandle ) );
#elif CYPHER_PLATFORM_POSIX
    ::dlclose( pLibrary->pHandle );
#endif

    pLibrary->pHandle = nullptr;
}

void *DynamicLibrary_GetSymbol( dynamic_library_t *pLibrary, const char *pSymbolName )
{
    if ( pLibrary == nullptr || pLibrary->pHandle == nullptr || pSymbolName == nullptr || pSymbolName[0] == '\0' ) {
        return nullptr;
    }

#if CYPHER_PLATFORM_WINDOWS
    return reinterpret_cast<void *>( ::GetProcAddress( reinterpret_cast<HMODULE>( pLibrary->pHandle ), pSymbolName ) );
#elif CYPHER_PLATFORM_POSIX
    return ::dlsym( pLibrary->pHandle, pSymbolName );
#else
    return nullptr;
#endif
}

} // namespace cypher::common
