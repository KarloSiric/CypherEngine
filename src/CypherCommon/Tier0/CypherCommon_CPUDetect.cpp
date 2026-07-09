//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_CPUDetect.cpp
//  Purpose: Implements CypherCommon Tier0 CPU detection support.
//  Details: CPUDetect owns processor identity, topology, cache line size, and
//           hardware/OS-usable feature flags. Higher layers use this file instead
//           of scattering CPUID, sysctl, or platform feature checks.
//
//  History:
//  - Created by Karlo Siric on 2026-07-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CPUDetect.h"

#include "CypherCommon_Platform.h"
#include "CypherCommon_Thread.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if CYPHER_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif CYPHER_PLATFORM_LINUX
    #include <unistd.h>
#elif CYPHER_PLATFORM_MACOS
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

#if CYPHER_COMPILER_MSVC && CYPHER_ARCH_X86_FAMILY
    #include <intrin.h>
#elif ( CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC ) && CYPHER_ARCH_X86_FAMILY
    #include <cpuid.h>
#endif

namespace cypher::common
{

namespace
{

cy_cpu_detect_info_t g_cpuInfo = {};
std::mutex g_cpuDetectMutex;
std::atomic_bool g_cpuDetectInitialized = false;

struct cpuid_regs_t {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

void CPUDetect_CopyString( char *pszDst, usize cchDst, const char *pszSrc )
{
    if ( pszDst == nullptr || cchDst == 0u ) {
        return;
    }

    const char *pszRead = pszSrc != nullptr ? pszSrc : "";
    usize i = 0u;
    for ( ; i + 1u < cchDst && pszRead[i] != '\0'; ++i ) {
        pszDst[i] = pszRead[i];
    }

    pszDst[i] = '\0';
}

bool_t CPUDetect_Cpuid( u32 leaf, u32 subleaf, cpuid_regs_t &out )
{
    out = {};

#if CYPHER_ARCH_X86_FAMILY && CYPHER_COMPILER_MSVC
    int regs[4] = {};
    __cpuidex( regs, static_cast<int>( leaf ), static_cast<int>( subleaf ) );
    out.eax = static_cast<u32>( regs[0] );
    out.ebx = static_cast<u32>( regs[1] );
    out.ecx = static_cast<u32>( regs[2] );
    out.edx = static_cast<u32>( regs[3] );
    return CY_TRUE;
#elif CYPHER_ARCH_X86_FAMILY && ( CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC )
    unsigned int eax = 0u;
    unsigned int ebx = 0u;
    unsigned int ecx = 0u;
    unsigned int edx = 0u;

    if ( __get_cpuid_count( leaf, subleaf, &eax, &ebx, &ecx, &edx ) == 0 ) {
        return CY_FALSE;
    }

    out.eax = static_cast<u32>( eax );
    out.ebx = static_cast<u32>( ebx );
    out.ecx = static_cast<u32>( ecx );
    out.edx = static_cast<u32>( edx );
    return CY_TRUE;
#else
    CYPHER_UNUSED( leaf );
    CYPHER_UNUSED( subleaf );
    return CY_FALSE;
#endif
}

u64 CPUDetect_XGetBV( u32 index )
{
#if CYPHER_ARCH_X86_FAMILY && CYPHER_COMPILER_MSVC
    return static_cast<u64>( _xgetbv( index ) );
#elif CYPHER_ARCH_X86_FAMILY && ( CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC )
    u32 eax = 0u;
    u32 edx = 0u;
    __asm__ volatile( "xgetbv" : "=a"( eax ), "=d"( edx ) : "c"( index ) );
    return ( static_cast<u64>( edx ) << 32u ) | static_cast<u64>( eax );
#else
    CYPHER_UNUSED( index );
    return 0u;
#endif
}

bool_t CPUDetect_HasAvxOsSupport()
{
#if CYPHER_ARCH_X86_FAMILY
    cpuid_regs_t regs = {};
    if ( !CPUDetect_Cpuid( 1u, 0u, regs ) ) {
        return CY_FALSE;
    }

    constexpr u32 CPU_FEATURE_XSAVE = CYPHER_BIT32( 26 );
    constexpr u32 CPU_FEATURE_OSXSAVE = CYPHER_BIT32( 27 );
    constexpr u64 XCR0_SSE = 0x2ull;
    constexpr u64 XCR0_AVX = 0x4ull;

    if ( ( regs.ecx & CPU_FEATURE_XSAVE ) == 0u || ( regs.ecx & CPU_FEATURE_OSXSAVE ) == 0u ) {
        return CY_FALSE;
    }

    const u64 xcr0 = CPUDetect_XGetBV( 0u );
    return ( xcr0 & ( XCR0_SSE | XCR0_AVX ) ) == ( XCR0_SSE | XCR0_AVX );
#else
    return CY_FALSE;
#endif
}

void CPUDetect_AddFeature( flags64_t &features, cy_cpu_feature_flags_t feature, bool_t enabled )
{
    if ( enabled ) {
        features |= static_cast<flags64_t>( feature );
    }
}

u32 CPUDetect_QueryMaxBasicLeaf()
{
    cpuid_regs_t regs = {};
    if ( CPUDetect_Cpuid( 0u, 0u, regs ) ) {
        return regs.eax;
    }

    return 0u;
}

void CPUDetect_FillVendor( cy_cpu_detect_info_t &info )
{
#if CYPHER_ARCH_X86_FAMILY
    cpuid_regs_t regs = {};
    if ( CPUDetect_Cpuid( 0u, 0u, regs ) ) {
        char szVendor[CY_CPU_VENDOR_MAX] = {};
        std::memcpy( szVendor + 0u, &regs.ebx, sizeof( regs.ebx ) );
        std::memcpy( szVendor + 4u, &regs.edx, sizeof( regs.edx ) );
        std::memcpy( szVendor + 8u, &regs.ecx, sizeof( regs.ecx ) );

        CPUDetect_CopyString( info.szVendor, CY_CPU_VENDOR_MAX, szVendor );

        if ( std::strcmp( szVendor, "GenuineIntel" ) == 0 ) {
            info.vendor = CY_CPU_VENDOR_INTEL;
            return;
        }
        if ( std::strcmp( szVendor, "AuthenticAMD" ) == 0 ) {
            info.vendor = CY_CPU_VENDOR_AMD;
            return;
        }
    }
#endif

#if CYPHER_PLATFORM_MACOS && CYPHER_ARCH_ARM_FAMILY
    info.vendor = CY_CPU_VENDOR_APPLE;
    CPUDetect_CopyString( info.szVendor, CY_CPU_VENDOR_MAX, "Apple" );
#elif CYPHER_ARCH_ARM_FAMILY
    info.vendor = CY_CPU_VENDOR_ARM;
    CPUDetect_CopyString( info.szVendor, CY_CPU_VENDOR_MAX, "ARM" );
#else
    info.vendor = CY_CPU_VENDOR_UNKNOWN;
    CPUDetect_CopyString( info.szVendor, CY_CPU_VENDOR_MAX, "Unknown" );
#endif
}

void CPUDetect_FillBrand( cy_cpu_detect_info_t &info )
{
    CPUDetect_CopyString( info.szBrand, CY_CPU_BRAND_MAX, "Unknown" );

#if CYPHER_ARCH_X86_FAMILY
    cpuid_regs_t maxRegs = {};
    if ( CPUDetect_Cpuid( 0x80000000u, 0u, maxRegs ) && maxRegs.eax >= 0x80000004u ) {
        char szBrand[CY_CPU_BRAND_MAX] = {};
        cpuid_regs_t regs = {};

        for ( u32 i = 0u; i < 3u; ++i ) {
            if ( CPUDetect_Cpuid( 0x80000002u + i, 0u, regs ) ) {
                std::memcpy( szBrand + ( i * 16u ) + 0u, &regs.eax, sizeof( regs.eax ) );
                std::memcpy( szBrand + ( i * 16u ) + 4u, &regs.ebx, sizeof( regs.ebx ) );
                std::memcpy( szBrand + ( i * 16u ) + 8u, &regs.ecx, sizeof( regs.ecx ) );
                std::memcpy( szBrand + ( i * 16u ) + 12u, &regs.edx, sizeof( regs.edx ) );
            }
        }

        CPUDetect_CopyString( info.szBrand, CY_CPU_BRAND_MAX, szBrand );
        return;
    }
#endif

#if CYPHER_PLATFORM_MACOS
    size_t cchBrand = CY_CPU_BRAND_MAX;
    if ( ::sysctlbyname( "machdep.cpu.brand_string", info.szBrand, &cchBrand, nullptr, 0 ) == 0 ) {
        info.szBrand[CY_CPU_BRAND_MAX - 1u] = '\0';
        return;
    }
#elif CYPHER_PLATFORM_LINUX
    FILE *pFile = std::fopen( "/proc/cpuinfo", "r" );
    if ( pFile == nullptr ) {
        return;
    }

    char szLine[256] = {};
    while ( std::fgets( szLine, sizeof( szLine ), pFile ) != nullptr ) {
        if ( std::strncmp( szLine, "model name", 10u ) == 0 || std::strncmp( szLine, "Hardware", 8u ) == 0 || std::strncmp( szLine, "Processor", 9u ) == 0 ) {
            const char *pColon = std::strchr( szLine, ':' );
            if ( pColon != nullptr ) {
                const char *pBrand = pColon + 1;
                while ( *pBrand == ' ' || *pBrand == '\t' ) {
                    ++pBrand;
                }

                CPUDetect_CopyString( info.szBrand, CY_CPU_BRAND_MAX, pBrand );

                char *pNewline = std::strchr( info.szBrand, '\n' );
                if ( pNewline != nullptr ) {
                    *pNewline = '\0';
                }
                break;
            }
        }
    }

    std::fclose( pFile );
#endif
}

void CPUDetect_FillFamilyModelStepping( cy_cpu_detect_info_t &info )
{
#if CYPHER_ARCH_X86_FAMILY
    cpuid_regs_t regs = {};
    if ( !CPUDetect_Cpuid( 1u, 0u, regs ) ) {
        return;
    }

    const u32 baseStepping = regs.eax & 0x0Fu;
    const u32 baseModel = ( regs.eax >> 4u ) & 0x0Fu;
    const u32 baseFamily = ( regs.eax >> 8u ) & 0x0Fu;
    const u32 extendedModel = ( regs.eax >> 16u ) & 0x0Fu;
    const u32 extendedFamily = ( regs.eax >> 20u ) & 0xFFu;

    info.stepping = baseStepping;
    info.family = baseFamily == 0x0Fu ? baseFamily + extendedFamily : baseFamily;
    info.model = ( baseFamily == 0x06u || baseFamily == 0x0Fu ) ? baseModel + ( extendedModel << 4u ) : baseModel;
#else
    info.family = 0u;
    info.model = 0u;
    info.stepping = 0u;
#endif
}

u32 CPUDetect_QueryPhysicalCoreCount()
{
#if CYPHER_PLATFORM_WINDOWS
    DWORD cbBuffer = 0u;
    if ( ::GetLogicalProcessorInformationEx( RelationProcessorCore, nullptr, &cbBuffer ) || cbBuffer == 0u ) {
        return Cy_ThreadGetLogicalCount();
    }

    void *pBuffer = std::malloc( cbBuffer );
    if ( pBuffer == nullptr ) {
        return Cy_ThreadGetLogicalCount();
    }

    u32 nCoreCount = 0u;
    if ( ::GetLogicalProcessorInformationEx( RelationProcessorCore, static_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( pBuffer ), &cbBuffer ) ) {
        DWORD cbOffset = 0u;
        while ( cbOffset < cbBuffer ) {
            const auto *pInfo = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>( static_cast<const u8 *>( pBuffer ) + cbOffset );
            if ( pInfo->Relationship == RelationProcessorCore ) {
                ++nCoreCount;
            }
            cbOffset += pInfo->Size;
        }
    }

    std::free( pBuffer );
    return nCoreCount != 0u ? nCoreCount : Cy_ThreadGetLogicalCount();
#elif CYPHER_PLATFORM_MACOS
    u32 nCoreCount = 0u;
    size_t cbCoreCount = sizeof( nCoreCount );

    if ( ::sysctlbyname( "hw.physicalcpu", &nCoreCount, &cbCoreCount, nullptr, 0 ) == 0 && nCoreCount != 0u ) {
        return nCoreCount;
    }

    return Cy_ThreadGetLogicalCount();
#elif CYPHER_PLATFORM_LINUX
    FILE *pFile = std::fopen( "/proc/cpuinfo", "r" );
    if ( pFile == nullptr ) {
        return Cy_ThreadGetLogicalCount();
    }

    constexpr u32 MAX_CPU_PAIRS = 1024u;
    u32 nPhysicalIds[MAX_CPU_PAIRS] = {};
    u32 nCoreIds[MAX_CPU_PAIRS] = {};
    u32 nPairCount = 0u;
    u32 nCurrentPhysicalId = CY_U32_MAX;
    u32 nCurrentCoreId = CY_U32_MAX;

    auto add_current_pair = [&]() {
        if ( nCurrentPhysicalId == CY_U32_MAX || nCurrentCoreId == CY_U32_MAX ) {
            return;
        }

        for ( u32 i = 0u; i < nPairCount; ++i ) {
            if ( nPhysicalIds[i] == nCurrentPhysicalId && nCoreIds[i] == nCurrentCoreId ) {
                return;
            }
        }

        if ( nPairCount < MAX_CPU_PAIRS ) {
            nPhysicalIds[nPairCount] = nCurrentPhysicalId;
            nCoreIds[nPairCount] = nCurrentCoreId;
            ++nPairCount;
        }
    };

    char szLine[256] = {};
    while ( std::fgets( szLine, sizeof( szLine ), pFile ) != nullptr ) {
        if ( szLine[0] == '\n' || szLine[0] == '\0' ) {
            add_current_pair();
            nCurrentPhysicalId = CY_U32_MAX;
            nCurrentCoreId = CY_U32_MAX;
            continue;
        }

        const char *pColon = std::strchr( szLine, ':' );
        if ( pColon == nullptr ) {
            continue;
        }

        const u32 nValue = static_cast<u32>( std::strtoul( pColon + 1, nullptr, 10 ) );
        if ( std::strncmp( szLine, "physical id", 11u ) == 0 ) {
            nCurrentPhysicalId = nValue;
        } else if ( std::strncmp( szLine, "core id", 7u ) == 0 ) {
            nCurrentCoreId = nValue;
        }
    }

    add_current_pair();
    std::fclose( pFile );

    return nPairCount != 0u ? nPairCount : Cy_ThreadGetLogicalCount();
#else
    return Cy_ThreadGetLogicalCount();
#endif
}

usize CPUDetect_QueryCacheLineSize()
{
#if CYPHER_PLATFORM_WINDOWS
    DWORD cbBuffer = 0u;
    if ( ::GetLogicalProcessorInformationEx( RelationCache, nullptr, &cbBuffer ) || cbBuffer == 0u ) {
        return CY_CACHE_LINE_SIZE;
    }

    void *pBuffer = std::malloc( cbBuffer );
    if ( pBuffer == nullptr ) {
        return CY_CACHE_LINE_SIZE;
    }

    usize nLineSize = 0u;
    if ( ::GetLogicalProcessorInformationEx( RelationCache, static_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>( pBuffer ), &cbBuffer ) ) {
        DWORD cbOffset = 0u;
        while ( cbOffset < cbBuffer ) {
            const auto *pInfo = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *>( static_cast<const u8 *>( pBuffer ) + cbOffset );
            if ( pInfo->Relationship == RelationCache && pInfo->Cache.Level == 1u ) {
                nLineSize = static_cast<usize>( pInfo->Cache.LineSize );
                break;
            }
            cbOffset += pInfo->Size;
        }
    }

    std::free( pBuffer );
    return nLineSize != 0u ? nLineSize : CY_CACHE_LINE_SIZE;
#elif CYPHER_PLATFORM_MACOS
    u64 nCacheLine = 0u;
    size_t cbCacheLine = sizeof( nCacheLine );

    if ( ::sysctlbyname( "hw.cachelinesize", &nCacheLine, &cbCacheLine, nullptr, 0 ) == 0 && nCacheLine != 0u ) {
        return static_cast<usize>( nCacheLine );
    }

    return CY_CACHE_LINE_SIZE;
#elif CYPHER_PLATFORM_LINUX && defined( _SC_LEVEL1_DCACHE_LINESIZE )
    const long nCacheLine = ::sysconf( _SC_LEVEL1_DCACHE_LINESIZE );
    if ( nCacheLine > 0 ) {
        return static_cast<usize>( nCacheLine );
    }

    return CY_CACHE_LINE_SIZE;
#else
    return CY_CACHE_LINE_SIZE;
#endif
}

flags64_t CPUDetect_QueryHardwareFeatures()
{
    flags64_t features = CY_CPU_FEATURE_NONE;

#if CYPHER_ARCH_ARM_FAMILY
    CPUDetect_AddFeature( features, CY_CPU_FEATURE_NEON, CY_TRUE );
    #if defined( __ARM_FEATURE_AES )
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_AES, CY_TRUE );
    #endif
#endif

#if CYPHER_ARCH_X86_FAMILY
    const u32 nMaxBasicLeaf = CPUDetect_QueryMaxBasicLeaf();

    cpuid_regs_t regs = {};
    if ( nMaxBasicLeaf >= 1u && CPUDetect_Cpuid( 1u, 0u, regs ) ) {
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_SSE2, ( regs.edx & CYPHER_BIT32( 26 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_SSE3, ( regs.ecx & CYPHER_BIT32( 0 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_SSSE3, ( regs.ecx & CYPHER_BIT32( 9 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_SSE41, ( regs.ecx & CYPHER_BIT32( 19 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_SSE42, ( regs.ecx & CYPHER_BIT32( 20 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_AVX, ( regs.ecx & CYPHER_BIT32( 28 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_AES, ( regs.ecx & CYPHER_BIT32( 25 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_FMA, ( regs.ecx & CYPHER_BIT32( 12 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_POPCNT, ( regs.ecx & CYPHER_BIT32( 23 ) ) != 0u );
    }

    cpuid_regs_t leaf7 = {};
    if ( nMaxBasicLeaf >= 7u && CPUDetect_Cpuid( 7u, 0u, leaf7 ) ) {
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_AVX2, ( leaf7.ebx & CYPHER_BIT32( 5 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_BMI1, ( leaf7.ebx & CYPHER_BIT32( 3 ) ) != 0u );
        CPUDetect_AddFeature( features, CY_CPU_FEATURE_BMI2, ( leaf7.ebx & CYPHER_BIT32( 8 ) ) != 0u );
    }
#endif

    return features;
}

flags64_t CPUDetect_FilterUsableFeatures( flags64_t hardwareFeatures )
{
    flags64_t usableFeatures = hardwareFeatures;

#if CYPHER_ARCH_X86_FAMILY
    if ( !CPUDetect_HasAvxOsSupport() ) {
        usableFeatures &= ~static_cast<flags64_t>( CY_CPU_FEATURE_AVX );
        usableFeatures &= ~static_cast<flags64_t>( CY_CPU_FEATURE_AVX2 );
        usableFeatures &= ~static_cast<flags64_t>( CY_CPU_FEATURE_FMA );
    }
#endif

    return usableFeatures;
}

} // namespace

bool_t Cy_CPUDetectInit()
{
    if ( g_cpuDetectInitialized.load( std::memory_order_acquire ) ) {
        return CY_TRUE;
    }

    std::lock_guard<std::mutex> lock( g_cpuDetectMutex );
    if ( g_cpuDetectInitialized.load( std::memory_order_relaxed ) ) {
        return CY_TRUE;
    }

    g_cpuInfo = {};
    CPUDetect_FillVendor( g_cpuInfo );
    CPUDetect_FillBrand( g_cpuInfo );
    CPUDetect_FillFamilyModelStepping( g_cpuInfo );
    g_cpuInfo.logicalThreadCount = Cy_ThreadGetLogicalCount();
    g_cpuInfo.physicalCoreCount = CPUDetect_QueryPhysicalCoreCount();
    g_cpuInfo.cacheLineSize = CPUDetect_QueryCacheLineSize();
    g_cpuInfo.hardwareFeatures = CPUDetect_QueryHardwareFeatures();
    g_cpuInfo.usableFeatures = CPUDetect_FilterUsableFeatures( g_cpuInfo.hardwareFeatures );

    g_cpuDetectInitialized.store( true, std::memory_order_release );
    return CY_TRUE;
}

void Cy_CPUDetectShutdown()
{
    std::lock_guard<std::mutex> lock( g_cpuDetectMutex );
    g_cpuInfo = {};
    g_cpuDetectInitialized.store( false, std::memory_order_release );
}

const cy_cpu_detect_info_t *Cy_CPUDetectGetInfo()
{
    Cy_CPUDetectInit();
    return &g_cpuInfo;
}

bool_t Cy_CPUDetectHasFeature( flags64_t features, cy_cpu_feature_flags_t feature )
{
    return ( features & static_cast<flags64_t>( feature ) ) != 0u;
}

const char *Cy_CPUDetectFeatureName( cy_cpu_feature_flags_t feature )
{
    switch ( feature ) {
        case CY_CPU_FEATURE_NONE:
            return "none";
        case CY_CPU_FEATURE_SSE2:
            return "sse2";
        case CY_CPU_FEATURE_SSE3:
            return "sse3";
        case CY_CPU_FEATURE_SSSE3:
            return "ssse3";
        case CY_CPU_FEATURE_SSE41:
            return "sse4.1";
        case CY_CPU_FEATURE_SSE42:
            return "sse4.2";
        case CY_CPU_FEATURE_AVX:
            return "avx";
        case CY_CPU_FEATURE_AVX2:
            return "avx2";
        case CY_CPU_FEATURE_NEON:
            return "neon";
        case CY_CPU_FEATURE_AES:
            return "aes";
        case CY_CPU_FEATURE_FMA:
            return "fma";
        case CY_CPU_FEATURE_BMI1:
            return "bmi1";
        case CY_CPU_FEATURE_BMI2:
            return "bmi2";
        case CY_CPU_FEATURE_POPCNT:
            return "popcnt";
        default:
            return "unknown";
    }
}

const char *Cy_CPUDetectVendorName( cy_cpu_vendor_t vendor )
{
    switch ( vendor ) {
        case CY_CPU_VENDOR_INTEL:
            return "Intel";
        case CY_CPU_VENDOR_AMD:
            return "AMD";
        case CY_CPU_VENDOR_APPLE:
            return "Apple";
        case CY_CPU_VENDOR_ARM:
            return "ARM";
        case CY_CPU_VENDOR_QUALCOMM:
            return "Qualcomm";
        case CY_CPU_VENDOR_UNKNOWN:
        default:
            return "Unknown";
    }
}

} // namespace cypher::common
