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

    CY_STATIC_ASSERT( sizeof( u32 ) == 4u, "u32 must be 4 bytes." );
    CY_STATIC_ASSERT( is_trivially_copyable_v<smoke_struct_t>, "smoke_struct_t must be trivially copyable." );

    const compiler_info_t compiler = Cy_CompilerGetInfo();
    if ( compiler.pName == nullptr || compiler.pName[0] == '\0' ) {
        return Fail();
    }
    if ( Cy_CompilerGetName() == nullptr || Cy_CompilerGetVersion() == 0u ) {
        return Fail();
    }
    if ( compiler.hasExceptions != ( CYPHER_CPP_EXCEPTIONS != 0 ) ) {
        return Fail();
    }
    if ( compiler.hasRtti != ( CYPHER_CPP_RTTI != 0 ) ) {
        return Fail();
    }

    const build_config_t build_config = Cy_BuildConfigGetCurrent();
    if ( Cy_BuildConfigGetName( build_config )[0] == '\0' ) {
        return Fail();
    }
    if ( Cy_BuildConfigIsDebug() != ( CYPHER_CONFIG_DEBUG != 0 ) ) {
        return Fail();
    }
    if ( Cy_BuildConfigIsRelease() != ( CYPHER_CONFIG_RELEASE != 0 ) ) {
        return Fail();
    }
    if ( Cy_BuildConfigIsDevelopment() != ( CYPHER_CONFIG_DEVELOPMENT != 0 ) ) {
        return Fail();
    }
    if ( Cy_BuildConfigIsShipping() != ( CYPHER_CONFIG_SHIPPING != 0 ) ) {
        return Fail();
    }

    if ( !Cy_AlignIsPowerOfTwo( 64u ) ) {
        return Fail();
    }
    if ( Cy_AlignIsPowerOfTwo( 0u ) ) {
        return Fail();
    }
    if ( Cy_AlignUp( 13u, 8u ) != 16u ) {
        return Fail();
    }
    if ( Cy_AlignDown( 17u, 8u ) != 16u ) {
        return Fail();
    }
    if ( !Cy_AlignIsAligned( 32u, 16u ) ) {
        return Fail();
    }

    if ( Cy_Bit32( 3u ) != 8u ) {
        return Fail();
    }
    if ( !Cy_HasAllFlags( 0x07u, 0x03u ) ) {
        return Fail();
    }
    if ( Cy_ClearFlags( 0x07u, 0x02u ) != 0x05u ) {
        return Fail();
    }
    if ( Cy_PopCount32( 0x0Fu ) != 4 ) {
        return Fail();
    }
    if ( Cy_CountTrailingZeros32( 0x10u ) != 4 ) {
        return Fail();
    }
    if ( Cy_RotateLeft32( 1u, 4 ) != 16u ) {
        return Fail();
    }

    if ( Cy_ByteSwap16( 0x1122u ) != 0x2211u ) {
        return Fail();
    }
    if ( Cy_ByteSwap32( 0x11223344u ) != 0x44332211u ) {
        return Fail();
    }
    if ( Cy_LittleToHost32( Cy_HostToLittle32( 0xCAFEBABEu ) ) != 0xCAFEBABEu ) {
        return Fail();
    }
    if ( Cy_MakeFourCC( 'C', 'Y', 'P', 'K' ) != 0x4B505943u ) {
        return Fail();
    }

    char src[] = { 'c', 'y', 'p', 'h', 'e', 'r', '\0' };
    char dst[sizeof( src )] = {};
    Cy_MemCopy( dst, src, sizeof( src ) );
    if ( !Cy_MemEqual( dst, src, sizeof( src ) ) ) {
        return Fail();
    }
    Cy_MemZero( dst, sizeof( dst ) );
    if ( dst[0] != '\0' ) {
        return Fail();
    }

    smoke_struct_t value = { 10u, 20u };
    Cy_ZeroStruct( value );
    if ( value.a != 0u || value.b != 0u ) {
        return Fail();
    }

    const timer_tick_t start_ticks = Cy_TimerNowTicks();
    const timer_tick_t end_ticks = Cy_TimerNowTicks();
    if ( end_ticks < start_ticks ) {
        return Fail();
    }

    if ( Cy_ThreadGetLogicalCount() == 0u ) {
        return Fail();
    }
    const u64 thread_hash = Cy_ThreadGetCurrentIdHash();
    CYPHER_UNUSED( thread_hash );

    const cy_cpu_detect_info_t *pCpu = Cy_CPUDetectGetInfo();
    if ( pCpu == nullptr ) {
        return Fail();
    }
    if ( pCpu->logicalThreadCount == 0u || pCpu->cacheLineSize == 0u ) {
        return Fail();
    }

    const cy_simd_caps_t *pSimd = Cy_SimdGetCaps();
    if ( pSimd == nullptr ) {
        return Fail();
    }
    if ( pSimd->vectorRegisterBits != pSimd->vectorRegisterBytes * 8u ) {
        return Fail();
    }
    if ( pSimd->bestLevel != CY_SIMD_LEVEL_SCALAR && pSimd->vectorRegisterBytes == 0u ) {
        return Fail();
    }

    stack_trace_t trace = {};
    const u32 cCapturedFrames = Cy_StackTraceCapture(
        &trace,
        CYPHER_STACK_TRACE_MAX_FRAMES,
        0u );
    if ( cCapturedFrames != trace.frame_count ) {
        return Fail();
    }
    if ( cCapturedFrames > CYPHER_STACK_TRACE_MAX_FRAMES ) {
        return Fail();
    }
#if CYPHER_PLATFORM_WINDOWS || CYPHER_PLATFORM_POSIX
    if ( cCapturedFrames == 0u ) {
        return Fail();
    }
#endif

    if ( !Cy_SystemInfoInit() ) {
        return Fail();
    }
    const cy_system_info_t *pInfo = Cy_SystemInfoGet();
    if ( pInfo == nullptr ) {
        return Fail();
    }
    if ( pInfo->cpu.logicalThreadCount == 0u ) {
        return Fail();
    }
    if ( pInfo->platform.pointerSize != sizeof( void * ) ) {
        return Fail();
    }

    return 0;
}
