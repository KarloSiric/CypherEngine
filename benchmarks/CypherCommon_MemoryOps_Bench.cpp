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

struct memoryops_bench_buffers_t {
    std::array<u8, kLargeBytes> nSrc{};
    std::array<u8, kLargeBytes> nDst{};
    std::array<u8, kLargeBytes> nEqual{};
    std::array<u8, kLargeBytes> nZero{};
    std::array<u8, kLargeBytes> nNonZeroFirst{};
    std::array<u8, kLargeBytes> nNonZeroLast{};
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
BENCHMARK( BM_MemMove_Overlap4KB );
BENCHMARK( BM_MemZero_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_StdMemsetZero_Bytes )->Arg( kSmallBytes )->Arg( kMediumBytes )->Arg( kLargeBytes );
BENCHMARK( BM_MemCompare_Equal4KB );
BENCHMARK( BM_MemIsZero_Zero4KB );
BENCHMARK( BM_MemIsZero_NonZeroFirst4KB );
BENCHMARK( BM_MemIsZero_NonZeroLast4KB );
BENCHMARK( BM_MemRangesOverlap );
BENCHMARK( BM_MemPointerInRange );
BENCHMARK( BM_CopyArray_U32_1024 );
