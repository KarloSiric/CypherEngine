//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_Smoke.cpp
//  Purpose: Tests Tier0 Smoke behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Tier0.h"

using namespace cypher::common;

namespace
{

struct smoke_struct_t {
    u32 a;
    u32 b;
};

int Fail()
{
    return 1;
}

CYPHER_WARNING_PUSH()
CYPHER_WARNING_DISABLE_UNUSED_PARAMETER()
void WarningUnusedParameterProbe( int nUnusedParameter )
{
}
CYPHER_WARNING_POP()

} // namespace

int main()
{
    WarningUnusedParameterProbe( 1 );

    CYPHER_STATIC_ASSERT( sizeof( u32 ) == 4u, "u32 must be 4 bytes." );
    CYPHER_STATIC_ASSERT( is_trivially_copyable_v<smoke_struct_t>, "smoke_struct_t must be trivially copyable." );

    const compiler_info_t compiler = Compiler_GetInfo();
    if ( compiler.pName == nullptr || compiler.pName[0] == '\0' ) {
        return Fail();
    }
    if ( Compiler_GetName() == nullptr || Compiler_GetVersion() == 0u ) {
        return Fail();
    }
    if ( compiler.has_exceptions != ( CYPHER_CPP_EXCEPTIONS != 0 ) ) {
        return Fail();
    }
    if ( compiler.has_rtti != ( CYPHER_CPP_RTTI != 0 ) ) {
        return Fail();
    }

    const build_config_t build_config = BuildConfig_GetCurrent();
    if ( BuildConfig_GetName( build_config )[0] == '\0' ) {
        return Fail();
    }
    if ( BuildConfig_IsDebug() != ( CYPHER_BUILD_DEBUG != 0 ) ) {
        return Fail();
    }
    if ( BuildConfig_IsRelease() != ( CYPHER_BUILD_RELEASE != 0 ) ) {
        return Fail();
    }

    if ( !IsPowerOfTwo( 64u ) ) {
        return Fail();
    }
    if ( IsPowerOfTwo( 0u ) ) {
        return Fail();
    }
    if ( AlignUp( 13u, 8u ) != 16u ) {
        return Fail();
    }
    if ( AlignDown( 17u, 8u ) != 16u ) {
        return Fail();
    }
    if ( !IsAligned( 32u, 16u ) ) {
        return Fail();
    }

    if ( Bit32( 3u ) != 8u ) {
        return Fail();
    }
    if ( !HasAllFlags( 0x07u, 0x03u ) ) {
        return Fail();
    }
    if ( ClearFlags( 0x07u, 0x02u ) != 0x05u ) {
        return Fail();
    }
    if ( PopCount32( 0x0Fu ) != 4 ) {
        return Fail();
    }
    if ( CountTrailingZeros32( 0x10u ) != 4 ) {
        return Fail();
    }
    if ( RotateLeft32( 1u, 4 ) != 16u ) {
        return Fail();
    }

    if ( ByteSwap16( 0x1122u ) != 0x2211u ) {
        return Fail();
    }
    if ( ByteSwap32( 0x11223344u ) != 0x44332211u ) {
        return Fail();
    }
    if ( LittleToHost32( HostToLittle32( 0xCAFEBABEu ) ) != 0xCAFEBABEu ) {
        return Fail();
    }
    if ( MakeFourCC( 'C', 'Y', 'P', 'K' ) != 0x4B505943u ) {
        return Fail();
    }

    char src[] = { 'c', 'y', 'p', 'h', 'e', 'r', '\0' };
    char dst[sizeof( src )] = {};
    MemCopy( dst, src, sizeof( src ) );
    if ( !MemEqual( dst, src, sizeof( src ) ) ) {
        return Fail();
    }
    MemZero( dst, sizeof( dst ) );
    if ( dst[0] != '\0' ) {
        return Fail();
    }

    smoke_struct_t value = { 10u, 20u };
    ZeroStruct( value );
    if ( value.a != 0u || value.b != 0u ) {
        return Fail();
    }

    const timer_tick_t start_ticks = Timer_NowTicks();
    const timer_tick_t end_ticks = Timer_NowTicks();
    if ( end_ticks < start_ticks ) {
        return Fail();
    }

    if ( GetLogicalThreadCount() == 0u ) {
        return Fail();
    }
    const u64 thread_hash = GetCurrentThreadIdHash();
    CYPHER_UNUSED( thread_hash );

    stack_trace_t trace = {};
    if ( CaptureStackTrace( trace, CYPHER_STACK_TRACE_MAX_FRAMES, 0u ) != 0u ) {
        return Fail();
    }
    if ( trace.frame_count != 0u ) {
        return Fail();
    }

    const system_info_t info = GetSystemInfo();
    if ( info.logical_thread_count == 0u ) {
        return Fail();
    }
    if ( info.pointer_size != sizeof( void * ) ) {
        return Fail();
    }

    return 0;
}
