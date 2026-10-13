//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Tests static mesh uploads, validation, and renderer ownership.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/Runtime/CypherRender_MeshBridge.h"
#include "CypherRender/CypherRender_Local.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace common = ::cypher::common;
namespace render = ::cypher::engine::render;

namespace
{

struct mesh_fixture_t {
    render::renderer_state_t savedRenderer{ render::tr };
    render::backend_api_t backend{};
    render::render_static_mesh_bundle_t bundle{};
    common::f32 vertices[9]{
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    common::u16 indices16[3]{ 0u, 1u, 2u };
    common::u32 indices32[3]{ 0u, 1u, 2u };
    common::byte uploaded[2][64]{};
    common::u64 uploadSizes[2]{};
    render::render_buffer_usage_flags_t uploadUsages[2]{};
    common::u64 nextToken{ 1u };
    common::u32 bufferCreates{ 0u };
    common::u32 bufferDestroyCalls{ 0u };
    common::u32 bufferDestroys{ 0u };
    common::u32 inputCreates{ 0u };
    common::u32 inputDestroyCalls{ 0u };
    common::u32 inputDestroys{ 0u };
    bool failSecondBuffer{ false };
    bool failInputCreate{ false };
    bool returnTokenOnFailure{ false };
    bool failNextBufferDestroy{ false };
    bool failNextInputDestroy{ false };
    bool inputReceivedBothBuffers{ false };

    void Init()
    {
        backend.CreateBuffer = []( const render::render_buffer_desc_t &description,
            const render::render_buffer_data_t *data,
            render::backend_buffer_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<mesh_fixture_t *>( state );
            const common::u32 index = fixture.bufferCreates++;
            out = {};
            if ( data == nullptr || data->bytes == nullptr ||
                 data->byteSize != description.byteSize ||
                 data->byteSize > sizeof( fixture.uploaded[0] ) ) {
                return render::render_error_t::ERR_INVALID_ARGUMENT;
            }
            if ( index < 2u ) {
                fixture.uploadSizes[index] = data->byteSize;
                fixture.uploadUsages[index] = description.usage;
                std::memcpy( fixture.uploaded[index], data->bytes,
                    static_cast<common::usize>( data->byteSize ) );
            }
            if ( fixture.failSecondBuffer && index == 1u ) {
                if ( fixture.returnTokenOnFailure ) out.value = fixture.nextToken++;
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyBuffer = []( render::backend_buffer_t, void *state ) noexcept {
            auto &fixture = *static_cast<mesh_fixture_t *>( state );
            ++fixture.bufferDestroyCalls;
            if ( fixture.failNextBufferDestroy ) {
                fixture.failNextBufferDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            ++fixture.bufferDestroys;
            return render::render_error_t::OK;
        };
        backend.CreateVertexInput = []( const render::backend_vertex_input_desc_t &description,
            render::backend_vertex_input_t &out, void *state ) noexcept {
            auto &fixture = *static_cast<mesh_fixture_t *>( state );
            ++fixture.inputCreates;
            fixture.inputReceivedBothBuffers = description.vertexBufferCount == 1u &&
                description.vertexBuffers[0].buffer.value != 0u &&
                description.indexBuffer.buffer.value != 0u;
            out = {};
            if ( fixture.failInputCreate ) {
                if ( fixture.returnTokenOnFailure ) out.value = fixture.nextToken++;
                return render::render_error_t::ERR_OUT_OF_MEMORY;
            }
            out.value = fixture.nextToken++;
            return render::render_error_t::OK;
        };
        backend.DestroyVertexInput = []( render::backend_vertex_input_t, void *state ) noexcept {
            auto &fixture = *static_cast<mesh_fixture_t *>( state );
            ++fixture.inputDestroyCalls;
            if ( fixture.failNextInputDestroy ) {
                fixture.failNextInputDestroy = false;
                return render::render_error_t::ERR_DEVICE_LOST;
            }
            ++fixture.inputDestroys;
            return render::render_error_t::OK;
        };

        render::tr = {};
        render::tr.backend = { &backend, this };
        render::tr.initialized = true;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        REQUIRE( render::R_BufferSystemInit() == render::render_error_t::OK );
        REQUIRE( render::R_VertexInputSystemInit() == render::render_error_t::OK );
    }

    render::render_static_mesh_source_t Source(
        render::render_index_type_t indexType = render::render_index_type_t::UINT16 ) const
    {
        render::render_static_mesh_source_t source{};
        source.vertexLayout.bindingCount = 1u;
        source.vertexLayout.bindings[0].stride = 3u * sizeof( common::f32 );
        source.vertexLayout.attributeCount = 1u;
        source.vertexLayout.attributes[0] = {
            render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u
        };
        source.vertexBytes = vertices;
        source.vertexByteSize = sizeof( vertices );
        source.vertexCount = 3u;
        source.indexBytes = indexType == render::render_index_type_t::UINT16
            ? static_cast<const void *>( indices16 )
            : static_cast<const void *>( indices32 );
        source.indexByteSize = indexType == render::render_index_type_t::UINT16
            ? sizeof( indices16 ) : sizeof( indices32 );
        source.indexCount = 3u;
        source.indexType = indexType;
        return source;
    }

    ~mesh_fixture_t()
    {
        (void)render::R_DestroyStaticMeshBundle( &bundle );
        (void)render::R_VertexInputSystemShutdown();
        (void)render::R_BufferSystemShutdown();
        render::tr = savedRenderer;
    }
};

} // namespace

TEST_CASE( "static mesh upload copies source bytes and applies only geometry to draws",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    const common::f32 expectedVertices[9]{
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    const common::u16 expectedIndices[3]{ 0u, 1u, 2u };
    const auto result = render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle );
    REQUIRE( result.status == render::render_static_mesh_bridge_status_t::OK );
    CHECK( fixture.bufferCreates == 2u );
    CHECK( fixture.inputCreates == 1u );
    CHECK( fixture.inputReceivedBothBuffers );
    CHECK( fixture.uploadSizes[0] == sizeof( expectedVertices ) );
    CHECK( fixture.uploadSizes[1] == sizeof( expectedIndices ) );
    CHECK( fixture.uploadUsages[0] == render::R_BUFFER_USAGE_VERTEX );
    CHECK( fixture.uploadUsages[1] == render::R_BUFFER_USAGE_INDEX );
    CHECK( render::R_IsBufferValid( fixture.bundle.vertexBuffer ) );
    CHECK( render::R_IsBufferValid( fixture.bundle.indexBuffer ) );
    CHECK( render::R_IsVertexInputValid( fixture.bundle.vertexInput ) );

    std::memset( fixture.vertices, 0, sizeof( fixture.vertices ) );
    std::memset( fixture.indices16, 0, sizeof( fixture.indices16 ) );
    CHECK( std::memcmp( fixture.uploaded[0], expectedVertices,
        sizeof( expectedVertices ) ) == 0 );
    CHECK( std::memcmp( fixture.uploaded[1], expectedIndices,
        sizeof( expectedIndices ) ) == 0 );

    render::render_draw_indexed_desc_t draw{};
    draw.pipeline.value = 77u;
    draw.uniformBuffers[0].value = 88u;
    draw.instanceCount = 4u;
    draw.firstIndex = 9u;
    REQUIRE( render::R_ApplyStaticMeshBundleToDraw(
        fixture.bundle, &draw ) == render::render_error_t::OK );
    CHECK( draw.pipeline.value == 77u );
    CHECK( draw.uniformBuffers[0].value == 88u );
    CHECK( draw.instanceCount == 4u );
    CHECK( draw.vertexInput.value == fixture.bundle.vertexInput.value );
    CHECK( draw.firstIndex == 0u );
    CHECK( draw.indexCount == 3u );
    CHECK( draw.vertexCount == 3u );

    REQUIRE( render::R_DestroyStaticMeshBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.inputDestroys == 1u );
    CHECK( fixture.bufferDestroys == 2u );
}

TEST_CASE( "static mesh bridge rejects out-of-range U16 and U32 indices before GPU allocation",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    fixture.indices16[2] = 3u;
    auto result = render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle );
    CHECK( result.status == render::render_static_mesh_bridge_status_t::INDEX_DATA_INVALID );
    CHECK( fixture.bufferCreates == 0u );
    CHECK( fixture.inputCreates == 0u );
    CHECK( fixture.bundle.vertexBuffer.value == 0u );

    fixture.indices32[1] = 0x1'0000u;
    result = render::R_CreateStaticMeshBundle(
        fixture.Source( render::render_index_type_t::UINT32 ), &fixture.bundle );
    CHECK( result.status == render::render_static_mesh_bridge_status_t::INDEX_DATA_INVALID );
    CHECK( fixture.bufferCreates == 0u );
    CHECK( fixture.inputCreates == 0u );
    CHECK( fixture.bundle.indexBuffer.value == 0u );
}

TEST_CASE( "static mesh upload accepts full-width U32 indices",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    const common::u32 expectedIndices[3]{ 0u, 1u, 2u };
    const auto result = render::R_CreateStaticMeshBundle(
        fixture.Source( render::render_index_type_t::UINT32 ), &fixture.bundle );
    REQUIRE( result.status == render::render_static_mesh_bridge_status_t::OK );
    CHECK( fixture.uploadSizes[1] == sizeof( expectedIndices ) );
    CHECK( std::memcmp( fixture.uploaded[1], expectedIndices,
        sizeof( expectedIndices ) ) == 0 );
    CHECK( fixture.bundle.indexType == render::render_index_type_t::UINT32 );
    render::render_vertex_input_info_t input{};
    REQUIRE( render::R_GetVertexInputInfo( fixture.bundle.vertexInput, &input ) ==
        render::render_error_t::OK );
    CHECK( input.indexBuffer.type == render::render_index_type_t::UINT32 );
}

TEST_CASE( "static mesh creation preserves a live output bundle",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle ).status ==
        render::render_static_mesh_bridge_status_t::OK );
    const auto original = fixture.bundle;
    const auto result = render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle );
    CHECK( result.status == render::render_static_mesh_bridge_status_t::INVALID_ARGUMENT );
    CHECK( fixture.bundle.vertexBuffer.value == original.vertexBuffer.value );
    CHECK( fixture.bundle.indexBuffer.value == original.indexBuffer.value );
    CHECK( fixture.bundle.vertexInput.value == original.vertexInput.value );
    CHECK( fixture.bufferCreates == 2u );
    CHECK( fixture.inputCreates == 1u );
}

TEST_CASE( "static mesh bridge rolls back buffers and a failed vertex input",
    "[CypherRender][MeshBridge]" )
{
    SECTION( "second buffer fails after returning a native token" ) {
        mesh_fixture_t fixture{};
        fixture.Init();
        fixture.failSecondBuffer = true;
        fixture.returnTokenOnFailure = true;
        const auto result = render::R_CreateStaticMeshBundle(
            fixture.Source(), &fixture.bundle );
        CHECK( result.status == render::render_static_mesh_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.bufferCreates == 2u );
        CHECK( fixture.bufferDestroys == 2u );
        CHECK( fixture.inputCreates == 0u );
        CHECK( fixture.bundle.vertexBuffer.value == 0u );
        CHECK( fixture.bundle.indexBuffer.value == 0u );
    }
    SECTION( "vertex input fails after returning a native token" ) {
        mesh_fixture_t fixture{};
        fixture.Init();
        fixture.failInputCreate = true;
        fixture.returnTokenOnFailure = true;
        const auto result = render::R_CreateStaticMeshBundle(
            fixture.Source(), &fixture.bundle );
        CHECK( result.status == render::render_static_mesh_bridge_status_t::RENDER_ERROR );
        CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
        CHECK( fixture.inputCreates == 1u );
        CHECK( fixture.inputDestroys == 1u );
        CHECK( fixture.bufferDestroys == 2u );
        CHECK( fixture.bundle.vertexInput.value == 0u );
    }
}

TEST_CASE( "static mesh creation retains a buffer when rollback destruction fails",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    fixture.failSecondBuffer = true;
    fixture.failNextBufferDestroy = true;
    const auto result = render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle );
    CHECK( result.status == render::render_static_mesh_bridge_status_t::CLEANUP_FAILED );
    CHECK( result.renderError == render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK( result.cleanupRenderError == render::render_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsBufferValid( fixture.bundle.vertexBuffer ) );
    CHECK( fixture.bundle.indexBuffer.value == 0u );
    CHECK( fixture.bundle.vertexInput.value == 0u );
    CHECK( fixture.bufferDestroyCalls == 1u );

    REQUIRE( render::R_DestroyStaticMeshBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.bufferDestroyCalls == 2u );
    CHECK( fixture.bufferDestroys == 1u );
    CHECK( fixture.bundle.vertexBuffer.value == 0u );
}

TEST_CASE( "static mesh cleanup keeps dependent handles for a later retry",
    "[CypherRender][MeshBridge]" )
{
    mesh_fixture_t fixture{};
    fixture.Init();
    REQUIRE( render::R_CreateStaticMeshBundle(
        fixture.Source(), &fixture.bundle ).status ==
        render::render_static_mesh_bridge_status_t::OK );
    fixture.failNextInputDestroy = true;
    CHECK( render::R_DestroyStaticMeshBundle( &fixture.bundle ) ==
        render::render_error_t::ERR_DEVICE_LOST );
    CHECK( fixture.inputDestroyCalls == 1u );
    CHECK( fixture.inputDestroys == 0u );
    CHECK( fixture.bufferDestroyCalls == 0u );
    CHECK( render::R_IsVertexInputValid( fixture.bundle.vertexInput ) );
    CHECK( render::R_IsBufferValid( fixture.bundle.vertexBuffer ) );
    CHECK( render::R_IsBufferValid( fixture.bundle.indexBuffer ) );

    REQUIRE( render::R_DestroyStaticMeshBundle( &fixture.bundle ) ==
        render::render_error_t::OK );
    CHECK( fixture.inputDestroyCalls == 2u );
    CHECK( fixture.inputDestroys == 1u );
    CHECK( fixture.bufferDestroys == 2u );
    CHECK( fixture.bundle.vertexInput.value == 0u );
    CHECK( fixture.bundle.vertexBuffer.value == 0u );
    CHECK( fixture.bundle.indexBuffer.value == 0u );
}
