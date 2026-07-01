#include "CypherCommon_Align.h"

#include <benchmark/benchmark.h>

#include <array>

using namespace cypher::common;

namespace
{

constexpr usize kValueCount = 1024u;

struct align_bench_values_t {
    std::array<usize, kValueCount> nValues{};
    alignas( 64 ) std::array<u8, 4096u> nBytes{};
};

align_bench_values_t BuildValues()
{
    align_bench_values_t values{};
    for ( usize i = 0u; i < kValueCount; ++i ) {
        values.nValues[i] = ( i * 37u ) + 13u;
    }
    return values;
}

align_bench_values_t &GetValues()
{
    static align_bench_values_t values = BuildValues();
    return values;
}

void BM_AlignUp_1024Values( benchmark::State &state )
{
    align_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( usize nValue : values.nValues ) {
            nAccum += AlignUp( nValue, 64u );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_AlignDown_1024Values( benchmark::State &state )
{
    align_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( usize nValue : values.nValues ) {
            nAccum += AlignDown( nValue, 64u );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_AlignPadding_1024Values( benchmark::State &state )
{
    align_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( usize nValue : values.nValues ) {
            nAccum += AlignPadding( nValue, 64u );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_AlignUpChecked_1024Values( benchmark::State &state )
{
    align_bench_values_t &values = GetValues();

    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( usize nValue : values.nValues ) {
            usize nAligned = 0u;
            nAccum += AlignUpChecked( nValue, 64u, nAligned ) ? nAligned : 0u;
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

void BM_AlignPointerPadding_1024Offsets( benchmark::State &state )
{
    align_bench_values_t &values = GetValues();
    const u8 *pBase = values.nBytes.data();

    for ( auto _ : state ) {
        usize nAccum = 0u;
        for ( usize i = 0u; i < kValueCount; ++i ) {
            nAccum += AlignPointerPadding( pBase + ( i % values.nBytes.size() ), 64u );
        }
        benchmark::DoNotOptimize( nAccum );
    }
}

} // namespace

BENCHMARK( BM_AlignUp_1024Values );
BENCHMARK( BM_AlignDown_1024Values );
BENCHMARK( BM_AlignPadding_1024Values );
BENCHMARK( BM_AlignUpChecked_1024Values );
BENCHMARK( BM_AlignPointerPadding_1024Offsets );
