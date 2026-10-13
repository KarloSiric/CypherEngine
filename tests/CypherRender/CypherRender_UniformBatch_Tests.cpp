//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests bounded uniform staging, draw records, and failure ownership.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_UniformBatch.h"
#include "CypherRender/CypherRender_Local.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

namespace common = ::cypher::common;
namespace render = ::cypher::engine::render;

namespace
{

struct uniform_batch_fixture_t {
    render::renderer_state_t savedRenderer{ render::tr };
    render::backend_api_t backend{};
    render::render_uniform_batch_t batch{};
    common::allocator_t allocator{};
    const common::allocator_t *systemAllocator{ common::Allocator_GetSystem() };
    common::byte uploaded[1024]{};
    common::u64 uploadedOffset{ 0u };
    common::u64 uploadedSize{ 0u };
    common::u64 nextToken{ 1u };
    common::u32 bufferCreates{ 0u };
    common::u32 bufferDestroyCalls{ 0u };
    common::u32 bufferDestroys{ 0u };
    common::u32 updateCalls{ 0u };
    common::u32 allocationAttempts{ 0u };
    common::u32 allocations{ 0u };
    common::u32 frees{ 0u };
    render::render_buffer_desc_t createdBuffer{};
    bool failAllocation{ false };
    bool failCreate{ false };
    bool returnTokenOnCreateFailure{ false };
    bool failNextUpdate{ false };
    bool failNextDestroy{ false };

    void Init()
    {
        allocator.pfnAllocate = []( void *state, common::usize size,
            common::usize alignment ) noexcept -> void * {
            auto &fixture = *static_cast<uniform_batch_fixture_t *>( state );
            ++fixture.allocationAttempts;
            if ( fixture.failAllocation ) return nullptr;
            void *result = fixture.systemAllocator->pfnAllocate(
                fixture.systemAllocator->pUserData, size, alignment );
            if ( result != nullptr ) ++fixture.allocations;
            return result;
        };
        allocator.pfnFree = []( void *state, void *memory,
            common::usize size, common::usize alignment ) noexcept {
            auto &fixture = *static_cast<uniform_batch_fixture_t *>( state );
            ++fixture.frees;
            fixture.systemAllocator->pfnFree(
                fixture.systemAllocator->pUserData, memory, size, alignment );
        };
        allocator.pUserData = this;

        backend.CreateBuffer = []( const render::render_buffer_desc_t &description,
            const render::render_buffer_data_t *initialData,
            render::backend_buffer_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<uniform_batch_fixture_t *>( state );
            ++fixture.bufferCreates;
            fixture.createdBuffer = description;
            out = {};
            if ( initialData != nullptr ) return render::render_error_t::ERR_INVALID_ARGUMENT;
            if ( fixture.failCreate ) {
                if ( fixture.returnTokenOnCreateFailure ) {
                    out.value = fixture.nextToken++;
                }
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.UpdateBuffer = []( render::backend_buffer_t,
            common::u64 destinationOffset,
            const render::render_buffer_data_t &sourceData,
            void *state ) noexcept {
            auto &fixture = *static_cast<uniform_batch_fixture_t *>( state );
            ++fixture.updateCalls;
            if ( fixture.failNextUpdate ) {
                fixture.failNextUpdate = false;
                return render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED;
            }
            if ( sourceData.bytes == nullptr ||
                 sourceData.byteSize > sizeof( fixture.uploaded ) ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            fixture.uploadedOffset = destinationOffset;
            fixture.uploadedSize = sourceData.byteSize;
            std::memcpy( fixture.uploaded, sourceData.bytes,
                static_cast<common::usize>( sourceData.byteSize ) );
            return render::render_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void *state ) noexcept {
            auto &fixture = *static_cast<uniform_batch_fixture_t *>( state );
            ++fixture.bufferDestroyCalls;
            if ( fixture.failNextDestroy ) {
                fixture.failNextDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            ++fixture.bufferDestroys;
            return render::render_error_t::OK;
        };

        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.info.limits.maxUniformBlockBytes = 256u;
        render::tr.info.limits.maxUniformBufferBindings = 16u;
        render::tr.info.limits.uniformBufferOffsetAlignment = 256u;
        REQUIRE( render::R_BufferSystemInit() == render::render_error_t::OK );
    }

    ~uniform_batch_fixture_t()
    {
        (void)render::R_DestroyUniformBatch( &batch );
        (void)render::R_BufferSystemShutdown();
        render::tr = savedRenderer;
    }
};

} // namespace

TEST_CASE( "uniform batch copies borrowed records and uploads one aligned span",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateUniformBatch( 512u, &fixture.allocator,
                 &fixture.batch ) == render::render_error_t::OK );
    CHECK( fixture.bufferCreates == 1u );
    CHECK( fixture.createdBuffer.byteSize == 512u );
    CHECK( fixture.createdBuffer.usage == render::R_BUFFER_USAGE_UNIFORM );
    CHECK( fixture.createdBuffer.memory == render::render_buffer_memory_t::UPLOAD );
    CHECK( fixture.createdBuffer.updatePolicy != render::render_buffer_update_t::IMMUTABLE );

    common::byte first[64]{};
    common::byte second[32]{};
    for ( common::u32 i = 0u; i < sizeof( first ); ++i ) first[i] = static_cast<common::byte>( i + 1u );
    for ( common::u32 i = 0u; i < sizeof( second ); ++i ) second[i] = static_cast<common::byte>( 0x80u + i );
    common::byte firstExpected[sizeof( first )]{};
    common::byte secondExpected[sizeof( second )]{};
    std::memcpy( firstExpected, first, sizeof( first ) );
    std::memcpy( secondExpected, second, sizeof( second ) );

    render::render_uniform_record_t firstRecord{};
    render::render_uniform_record_t secondRecord{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, first,
                 sizeof( first ), &firstRecord ) == render::render_error_t::OK );
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, second,
                 sizeof( second ), &secondRecord ) == render::render_error_t::OK );
    CHECK( firstRecord.offset == 0u );
    CHECK( firstRecord.byteSize == sizeof( first ) );
    CHECK( secondRecord.offset == 256u );
    CHECK( secondRecord.byteSize == sizeof( second ) );
    CHECK( firstRecord.buffer.value == secondRecord.buffer.value );
    CHECK( firstRecord.generation == secondRecord.generation );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline.value = 77u;
    draw.instanceCount = 3u;
    CHECK( render::R_ApplyUniformRecordToDraw( fixture.batch,
               firstRecord, 0u, &draw ) == render::render_error_t::ERR_INVALID_STATE );
    CHECK( draw.uniformBuffers[0].value == 0u );

    std::memset( first, 0, sizeof( first ) );
    std::memset( second, 0, sizeof( second ) );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.updateCalls == 1u );
    CHECK( fixture.uploadedOffset == 0u );
    CHECK( fixture.uploadedSize == 288u );
    CHECK( std::memcmp( fixture.uploaded, firstExpected,
        sizeof( firstExpected ) ) == 0 );
    CHECK( std::memcmp( fixture.uploaded + 256u, secondExpected,
        sizeof( secondExpected ) ) == 0 );

    REQUIRE( render::R_ApplyUniformRecordToDraw( fixture.batch,
                 secondRecord, 1u, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == 77u );
    CHECK( draw.instanceCount == 3u );
    CHECK( draw.uniformBuffers[0].value == 0u );
    CHECK( draw.uniformBuffers[1].value == secondRecord.buffer.value );
    CHECK( draw.uniformRanges[1].offset == 256u );
    CHECK( draw.uniformRanges[1].byteSize == sizeof( secondExpected ) );
    render::render_uniform_record_t unused{};
    CHECK( render::R_AppendUniformRecord( &fixture.batch, secondExpected,
               sizeof( secondExpected ), &unused ) ==
        render::render_error_t::ERR_INVALID_STATE );
    CHECK( unused.buffer.value == 0u );
}

TEST_CASE( "uniform batch checks device-aligned capacity and record extent before copy",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateUniformBatch( 320u, &fixture.allocator,
                 &fixture.batch ) == render::render_error_t::OK );
    common::byte record[256]{};
    render::render_uniform_record_t first{};
    render::render_uniform_record_t second{};
    render::render_uniform_record_t rejected{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, record,
                 200u, &first ) == render::render_error_t::OK );
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, record,
                 64u, &second ) == render::render_error_t::OK );
    CHECK( first.offset == 0u );
    CHECK( second.offset == 256u );
    CHECK( render::R_AppendUniformRecord( &fixture.batch, record,
               1u, &rejected ) ==
        render::render_error_t::ERR_COMMAND_CAPACITY_EXCEEDED );
    CHECK( rejected.buffer.value == 0u );
    CHECK( render::R_AppendUniformRecord( &fixture.batch, record,
               257u, &rejected ) == render::render_error_t::ERR_LIMIT_EXCEEDED );
    CHECK( render::R_AppendUniformRecord( &fixture.batch, record,
               std::numeric_limits<common::u64>::max(), &rejected ) ==
        render::render_error_t::ERR_LIMIT_EXCEEDED );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.uploadedSize == 320u );
}

TEST_CASE( "reset invalidates committed uniform record tokens even when offsets are reused",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateUniformBatch( 512u, &fixture.allocator,
                 &fixture.batch ) == render::render_error_t::OK );
    const common::byte source[64]{ 1u, 2u, 3u };
    render::render_uniform_record_t oldRecord{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, source,
                 sizeof( source ), &oldRecord ) == render::render_error_t::OK );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    render::tr.frameActive = true;
    CHECK( render::R_ResetUniformBatch( &fixture.batch ) ==
        render::render_error_t::ERR_FRAME_ALREADY_ACTIVE );
    render::tr.frameActive = false;
    REQUIRE( render::R_ResetUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );

    render::render_draw_indexed_desc_t draw{};
    CHECK( render::R_ApplyUniformRecordToDraw( fixture.batch,
               oldRecord, 0u, &draw ) == render::render_error_t::ERR_INVALID_STATE );
    render::render_uniform_record_t newRecord{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, source,
                 sizeof( source ), &newRecord ) == render::render_error_t::OK );
    CHECK( newRecord.offset == oldRecord.offset );
    CHECK( newRecord.buffer.value == oldRecord.buffer.value );
    CHECK( newRecord.generation != oldRecord.generation );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( render::R_ApplyUniformRecordToDraw( fixture.batch,
               oldRecord, 0u, &draw ) == render::render_error_t::ERR_STALE_HANDLE );
    REQUIRE( render::R_ApplyUniformRecordToDraw( fixture.batch,
                 newRecord, 0u, &draw ) == render::render_error_t::OK );
    CHECK( fixture.updateCalls == 2u );
}

TEST_CASE( "failed uniform upload stays uncommitted and can be retried",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateUniformBatch( 512u, &fixture.allocator,
                 &fixture.batch ) == render::render_error_t::OK );
    common::byte source[64]{ 7u, 9u };
    render::render_uniform_record_t record{};
    REQUIRE( render::R_AppendUniformRecord( &fixture.batch, source,
                 sizeof( source ), &record ) == render::render_error_t::OK );
    fixture.failNextUpdate = true;
    CHECK( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::ERR_RESOURCE_TRANSFER_FAILED );
    CHECK( fixture.updateCalls == 1u );
    render::render_draw_indexed_desc_t draw{};
    CHECK( render::R_ApplyUniformRecordToDraw( fixture.batch,
               record, 0u, &draw ) == render::render_error_t::ERR_INVALID_STATE );
    std::memset( source, 0, sizeof( source ) );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.updateCalls == 2u );
    CHECK( fixture.uploadedSize == 64u );
    CHECK( fixture.uploaded[0] == 7u );
    CHECK( fixture.uploaded[1] == 9u );
    REQUIRE( render::R_ApplyUniformRecordToDraw( fixture.batch,
                 record, 0u, &draw ) == render::render_error_t::OK );
    REQUIRE( render::R_CommitUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.updateCalls == 2u );
}

TEST_CASE( "uniform batch preserves ownership when GPU destroy fails and releases on retry",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateUniformBatch( 512u, &fixture.allocator,
                 &fixture.batch ) == render::render_error_t::OK );
    const common::u32 freesBeforeDestroy = fixture.frees;
    fixture.failNextDestroy = true;
    CHECK( render::R_DestroyUniformBatch( &fixture.batch ) ==
        render::render_error_t::ERR_DEVICE_LOST );
    CHECK( fixture.bufferDestroyCalls == 1u );
    CHECK( fixture.bufferDestroys == 0u );
    CHECK( fixture.frees == freesBeforeDestroy );
    REQUIRE( render::R_DestroyUniformBatch( &fixture.batch ) ==
        render::render_error_t::OK );
    CHECK( fixture.bufferDestroyCalls == 2u );
    CHECK( fixture.bufferDestroys == 1u );
    CHECK( fixture.frees == fixture.allocations );
}

TEST_CASE( "uniform batch creation failure releases staging and published native tokens",
    "[CypherRender][UniformBatch]" )
{
    uniform_batch_fixture_t fixture{};
    fixture.Init();
    fixture.failAllocation = true;
    CHECK( render::R_CreateUniformBatch( 512u, &fixture.allocator,
               &fixture.batch ) == render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK( fixture.allocationAttempts == 1u );
    CHECK( fixture.bufferCreates == 0u );
    CHECK( fixture.batch.staging == nullptr );
    fixture.failAllocation = false;
    fixture.failCreate = true;
    fixture.returnTokenOnCreateFailure = true;
    CHECK( render::R_CreateUniformBatch( 512u, &fixture.allocator,
               &fixture.batch ) == render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK( fixture.bufferCreates == 1u );
    CHECK( fixture.bufferDestroys == 1u );
    CHECK( fixture.frees == fixture.allocations );
    CHECK( fixture.batch.buffer.value == 0u );
}
