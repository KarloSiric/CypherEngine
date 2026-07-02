//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tools/perf/CypherPerf_SystemReport.cpp
//  Purpose: Provides performance tooling for Perf SystemReport.
//  Details: This tool supports repeatable performance inspection and benchmark
//           reporting. It should stay scriptable so CI and local development can use
//           the same path.
//
//  History:
//  - Created by Karlo Siric on 2026-07-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Tier0.h"

#include <cstdio>

using namespace cypher::common;

namespace
{

const char *BoolName( bool_t value )
{
    return value ? "yes" : "no";
}

bool_t CompileHasSSE2()
{
#if defined( __SSE2__ ) || ( defined( _M_X64 ) && !defined( _M_ARM64 ) )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasSSE3()
{
#if defined( __SSE3__ )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasSSE41()
{
#if defined( __SSE4_1__ )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasSSE42()
{
#if defined( __SSE4_2__ )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasAVX()
{
#if defined( __AVX__ )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasAVX2()
{
#if defined( __AVX2__ )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

bool_t CompileHasNEON()
{
#if defined( __ARM_NEON ) || defined( __ARM_NEON__ ) || defined( _M_ARM64 )
    return CY_TRUE;
#else
    return CY_FALSE;
#endif
}

} // namespace

int main()
{
    const compiler_info_t compiler = Compiler_GetInfo();
    const system_info_t system = GetSystemInfo();

    std::printf( "Cypher perf system report\n" );
    std::printf( "platform: %s\n", CYPHER_PLATFORM_NAME );
    std::printf( "architecture: %s\n", CYPHER_ARCH_NAME );
    std::printf( "compiler: %s %u\n", compiler.pName, compiler.version );
    std::printf( "build: %s\n", BuildConfig_GetName( BuildConfig_GetCurrent() ) );
    std::printf( "pointer_size: %zu\n", system.pointer_size );
    std::printf( "logical_threads: %u\n", system.logical_thread_count );
    std::printf( "cache_line_size: %zu\n", system.cache_line_size );
    std::printf( "default_page_size: %zu\n", system.default_page_size );
    std::printf( "little_endian: %s\n", BoolName( system.is_little_endian ) );
    std::printf( "exceptions: %s\n", BoolName( compiler.has_exceptions ) );
    std::printf( "rtti: %s\n", BoolName( compiler.has_rtti ) );
    std::printf( "compile_sse2: %s\n", BoolName( CompileHasSSE2() ) );
    std::printf( "compile_sse3: %s\n", BoolName( CompileHasSSE3() ) );
    std::printf( "compile_sse41: %s\n", BoolName( CompileHasSSE41() ) );
    std::printf( "compile_sse42: %s\n", BoolName( CompileHasSSE42() ) );
    std::printf( "compile_avx: %s\n", BoolName( CompileHasAVX() ) );
    std::printf( "compile_avx2: %s\n", BoolName( CompileHasAVX2() ) );
    std::printf( "compile_neon: %s\n", BoolName( CompileHasNEON() ) );

    return 0;
}
