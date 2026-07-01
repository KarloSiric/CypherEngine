#include "CypherCommon_MemoryOps.h"

#include <benchmark/benchmark.h>

#include <array>
#include <cstring>

using namespace cypher::common;

namespace
{

constexpr usize kSmallBytes = 64u;
constexpr usize kMediumBytes = 4u * 1024u;
constexpr usize kLargeBytes = 1024u * 1024u;
constexpr usize kAlignmentPadBytes = 128u;
constexpr usize kColdTouchBytes = 8u * 1024u * 1024u;

struct memoryops_bench_buffers_t {
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nSrc{};
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nDst{};
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nEqual{};
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nZero{};
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nNonZeroFirst{};
    alignas( 64 ) std::array<u8, kLargeBytes + kAlignmentPadBytes> nNonZeroLast{};
    alignas( 64 ) std::array<u8, kColdTouchBytes> nColdTouch{};
    std::array<u32, 1024u> nU32Src{};
    std::array<u32, 1024u> nU32Dst{};
};

memoryops_bench_buffers_t BuildBuffers()
{
    memoryops_bench_buffers_t buffers{};

    for ( usize i = 0u; i < kLargeBytes; ++i ) {
        buffers.nSrc[i] = static_cast<u8>( ( i * 37u ) & 0xFFu );
        buffers.nEqual[i] = buffers.nSrc[i];
    }

    for ( usize i = 0u; i < kColdTouchBytes; ++i ) {
        buffers.nColdTouch[i] = static_cast<u8>( ( i * 17u ) & 0xFFu );
    }

    buffers.nNonZeroFirst[0] = 1u;
    buffers.nNonZeroLast[kMediumBytes - 1u] = 1u;

    for ( usize i = 0u; i < buffers.nU32Src.size(); ++i ) {
        buffers.nU32Src[i] = static_cast<u32>( i * 13u );
    }

    return buffers;
}

memoryops_bench_buffers_t &GetBuffers()
{
    static memoryops_bench_buffers_t buffers = BuildBuffers();
    return buffers;
}

void TouchColdBuffer( memoryops_bench_buffers_t &buffers )
{
    u8 nAccum = 0u;
    for ( usize i = 0u; i < buffers.nColdTouch.size(); i += CY_CACHE_LINE_SIZE ) {
        nAccum ^= buffers.nColdTouch[i];
    }
    benchmark::DoNotOptimize( nAccum );
}

void BM_MemCopy_Bytes( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const usize nBytes = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        MemCopy( buffers.nDst.data(), buffers.nSrc.data(), nBytes );
        benchmark::DoNotOptimize( buffers.nDst.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( nBytes ) * state.iterations() );
}

void BM_MemCopy_Aligned4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data();
    const u8 *pSrc = buffers.nSrc.data();

    for ( auto _ : state ) {
        MemCopy( pDst, pSrc, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemCopy_Unaligned4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data() + 1u;
    const u8 *pSrc = buffers.nSrc.data() + 3u;

    for ( auto _ : state ) {
        MemCopy( pDst, pSrc, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemCopy_Coldish4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data();
    const u8 *pSrc = buffers.nSrc.data();

    for ( auto _ : state ) {
        state.PauseTiming();
        TouchColdBuffer( buffers );
        state.ResumeTiming();

        MemCopy( pDst, pSrc, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_StdMemcpy_Bytes( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const usize nBytes = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        std::memcpy( buffers.nDst.data(), buffers.nSrc.data(), nBytes );
        benchmark::DoNotOptimize( buffers.nDst.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( nBytes ) * state.iterations() );
}

void BM_MemMove_Overlap4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();

    for ( auto _ : state ) {
        MemMove( buffers.nDst.data() + 16u, buffers.nDst.data(), kMediumBytes );
        benchmark::DoNotOptimize( buffers.nDst.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemZero_Bytes( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const usize nBytes = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        MemZero( buffers.nDst.data(), nBytes );
        benchmark::DoNotOptimize( buffers.nDst.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( nBytes ) * state.iterations() );
}

void BM_MemZero_Aligned4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data();

    for ( auto _ : state ) {
        MemZero( pDst, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemZero_Unaligned4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data() + 1u;

    for ( auto _ : state ) {
        MemZero( pDst, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemZero_Coldish4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u8 *pDst = buffers.nDst.data();

    for ( auto _ : state ) {
        state.PauseTiming();
        TouchColdBuffer( buffers );
        state.ResumeTiming();

        MemZero( pDst, kMediumBytes );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_StdMemsetZero_Bytes( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const usize nBytes = static_cast<usize>( state.range( 0 ) );

    for ( auto _ : state ) {
        std::memset( buffers.nDst.data(), 0, nBytes );
        benchmark::DoNotOptimize( buffers.nDst.data() );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( nBytes ) * state.iterations() );
}

void BM_MemCompare_Equal4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemCompare( buffers.nSrc.data(), buffers.nEqual.data(), kMediumBytes ) );
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemIsZero_Zero4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemIsZero( buffers.nZero.data(), kMediumBytes ) );
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemIsZero_ZeroColdish4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const u8 *pData = buffers.nZero.data();

    for ( auto _ : state ) {
        state.PauseTiming();
        TouchColdBuffer( buffers );
        state.ResumeTiming();

        benchmark::DoNotOptimize( MemIsZero( pData, kMediumBytes ) );
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemIsZero_NonZeroFirst4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemIsZero( buffers.nNonZeroFirst.data(), kMediumBytes ) );
    }
}

void BM_MemIsZero_NonZeroLast4KB( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemIsZero( buffers.nNonZeroLast.data(), kMediumBytes ) );
    }

    state.SetBytesProcessed( static_cast<int64_t>( kMediumBytes ) * state.iterations() );
}

void BM_MemRangesOverlap( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const u8 *pBase = buffers.nSrc.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemRangesOverlap( pBase, kMediumBytes, pBase + kSmallBytes, kMediumBytes ) );
    }
}

void BM_MemPointerInRange( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    const u8 *pBase = buffers.nSrc.data();

    for ( auto _ : state ) {
        benchmark::DoNotOptimize( MemPointerInRange( pBase + kSmallBytes, pBase, kMediumBytes ) );
    }
}

void BM_CopyArray_U32_1024( benchmark::State &state )
{
    memoryops_bench_buffers_t &buffers = GetBuffers();
    u32 *pDst = buffers.nU32Dst.data();
    const u32 *pSrc = buffers.nU32Src.data();

    for ( auto _ : state ) {
        CopyArray( pDst, pSrc, 1024u );
        benchmark::DoNotOptimize( pDst );
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed( static_cast<int64_t>( 1024u * sizeof( u32 ) ) * state.iterations() );
}

} // namespace

BENCHMARK( BM_MemCopy_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_StdMemcpy_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_MemCopy_Aligned4KB );
BENCHMARK( BM_MemCopy_Unaligned4KB );
BENCHMARK( BM_MemCopy_Coldish4KB );
BENCHMARK( BM_MemMove_Overlap4KB );
BENCHMARK( BM_MemZero_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_StdMemsetZero_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_MemZero_Aligned4KB );
BENCHMARK( BM_MemZero_Unaligned4KB );
BENCHMARK( BM_MemZero_Coldish4KB );
BENCHMARK( BM_MemCompare_Equal4KB );
BENCHMARK( BM_MemIsZero_Zero4KB );
BENCHMARK( BM_MemIsZero_ZeroColdish4KB );
BENCHMARK( BM_MemIsZero_NonZeroFirst4KB );
BENCHMARK( BM_MemIsZero_NonZeroLast4KB );
BENCHMARK( BM_MemRangesOverlap );
BENCHMARK( BM_MemPointerInRange );
BENCHMARK( BM_CopyArray_U32_1024 );
