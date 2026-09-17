//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_Runtime_Tests.cpp
//  Purpose: Verifies renderer configuration, dispatch, and frontend guards.
//  Details: These tests avoid opening a native graphics context so they remain
//           deterministic on local machines and headless CI workers.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Backend.h"
#include "CypherRender/CypherRender_Public.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;

namespace
{

bool ActivateTestHostContext( void * ) noexcept
{
    return true;
}

render::render_host_proc_t ResolveTestHostProcedure(
    const char *,
    void * ) noexcept
{
    return nullptr;
}

render::render_vertex_layout_t MakeInterleavedVertexLayout()
{
    render::render_vertex_layout_t layout{};
    layout.bindingCount = 1u;
    layout.bindings[0].binding = 0u;
    layout.bindings[0].stride = 16u;
    layout.bindings[0].inputRate = render::render_vertex_input_rate_t::PER_VERTEX;
    layout.bindings[0].instanceDivisor = 0u;

    layout.attributeCount = 2u;
    layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT,
        0u,
        0u,
        0u
    };
    layout.attributes[1] = {
        render::render_format_t::RGBA8_UNORM,
        12u,
        1u,
        0u
    };
    return layout;
}

} // namespace

TEST_CASE( "renderer errors expose stable names and descriptions" )
{
    const ::cypher::common::error_table_t *table = render::R_ErrorTable();

    REQUIRE( table != nullptr );
    CHECK( table->domain == ::cypher::common::error_domain_t::RENDER );
    CHECK( table->pErrors != nullptr );
    CHECK( table->errorCount > 50u );
    CHECK( std::strcmp( render::R_ErrorName( render::render_error_t::OK ), "OK" ) == 0 );
    CHECK( std::strcmp(
        render::R_ErrorName( render::render_error_t::ERR_DEVICE_LOST ),
        "ERR_DEVICE_LOST" ) == 0 );
    CHECK( render::R_ErrorDescription( render::render_error_t::ERR_PRESENT_FAILED )[0] != '\0' );
    CHECK( ::cypher::common::Cy_ErrorDomain(
        render::R_ErrorCode( render::render_error_t::ERR_FAILED ) ) ==
        ::cypher::common::error_domain_t::RENDER );
}

TEST_CASE( "default renderer configuration describes the OpenGL bootstrap path" )
{
    const render::render_config_t config = render::R_DefaultConfig();

    CHECK( config.backend == render::render_backend_t::AUTO );
    CHECK( config.mode == render::render_mode_t::RASTER );
    CHECK( config.presentMode == render::render_present_mode_t::FIFO );
    CHECK( config.validation == render::render_validation_t::ERRORS );
    CHECK( config.requiredCapabilities == render::R_CAPABILITY_RASTERIZATION );
    CHECK( config.sampleCount == 0u );
    CHECK( config.allowBackendFallback );
    CHECK( config.sRGBFramebuffer );
    CHECK( config.requireAcceleration );
    CHECK( render::R_ValidateConfig( config ) == render::render_error_t::OK );
}

TEST_CASE( "configuration validation rejects contradictory and unknown policy" )
{
    render::render_config_t config = render::R_DefaultConfig();

    config.backend = render::render_backend_t::COUNT;
    CHECK( render::R_ValidateConfig( config ) == render::render_error_t::ERR_INVALID_ARGUMENT );

    config = render::R_DefaultConfig();
    config.requiredCapabilities = CYPHER_BIT64( 63 );
    CHECK( render::R_ValidateConfig( config ) == render::render_error_t::ERR_INVALID_ARGUMENT );

    config = render::R_DefaultConfig();
    config.optionalCapabilities |= render::R_CAPABILITY_RASTERIZATION;
    CHECK( render::R_ValidateConfig( config ) == render::render_error_t::ERR_INVALID_ARGUMENT );

    for ( const ::cypher::common::u8 invalidSamples : { 1u, 3u, 6u, 65u } ) {
        config = render::R_DefaultConfig();
        config.sampleCount = invalidSamples;
        CHECK( render::R_ValidateConfig( config ) == render::render_error_t::ERR_INVALID_ARGUMENT );
    }

    for ( const ::cypher::common::u8 validSamples : { 0u, 2u, 4u, 8u, 16u, 32u, 64u } ) {
        config = render::R_DefaultConfig();
        config.sampleCount = validSamples;
        CHECK( render::R_ValidateConfig( config ) == render::render_error_t::OK );
    }

    config = render::R_DefaultConfig();
    config.mode = render::render_mode_t::PATH_TRACED;
    CHECK( render::R_ValidateConfig( config ) == render::render_error_t::ERR_UNSUPPORTED );
}

TEST_CASE( "host surface validation rejects incomplete or ambiguous ownership" )
{
    render::render_host_surface_desc_t surface{};
    CHECK( render::R_ValidateHostSurface( surface ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    surface.backend = render::render_backend_t::OPENGL;
    surface.drawableExtent = { 640u, 360u };
    surface.apiMajorVersion = 4u;
    surface.apiMinorVersion = 1u;
    surface.accelerated = true;
    surface.ActivateContext = ActivateTestHostContext;
    surface.ResolveProcAddress = ResolveTestHostProcedure;
    CHECK( render::R_ValidateHostSurface( surface ) ==
        render::render_error_t::OK );

    render::render_host_surface_desc_t malformed = surface;
    malformed.drawableExtent.width = 0u;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.backend = render::render_backend_t::AUTO;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.apiMajorVersion = 0u;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.presentMode = render::render_present_mode_t::COUNT;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.ActivateContext = nullptr;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.ResolveProcAddress = nullptr;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformed = surface;
    malformed.sampleCount = 3u;
    CHECK( render::R_ValidateHostSurface( malformed ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    render::render_config_t incompatible = render::R_DefaultConfig();
    incompatible.backend = render::render_backend_t::SOFTWARE;
    CHECK( render::R_InitHostSurface( surface, incompatible ) ==
        render::render_error_t::ERR_WINDOW_INCOMPATIBLE );
    CHECK_FALSE( render::R_IsInitialized() );
}

TEST_CASE( "backend selection returns one complete concrete OpenGL table" )
{
    render::render_config_t config = render::R_DefaultConfig();
    const render::backend_api_t *backend = nullptr;

    REQUIRE( render::R_SelectBackend( config, &backend ) == render::render_error_t::OK );
    REQUIRE( backend != nullptr );
    CHECK( backend->backend == render::render_backend_t::OPENGL );
    CHECK( render::R_IsBackendValid( backend ) );

    render::backend_api_t malformed = *backend;
    malformed.apiVersion += 1u;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.structSize -= 1u;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.InitHostSurface = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.BeginFrame = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.CreateBuffer = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.CreateVertexInput = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.CreateShader = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.DestroyShader = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.CreateGraphicsPipeline = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.DestroyGraphicsPipeline = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.DrawIndexed = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );
    malformed = *backend;
    malformed.state = nullptr;
    CHECK_FALSE( render::R_IsBackendValid( &malformed ) );

    backend = reinterpret_cast<const render::backend_api_t *>( 1u );
    CHECK( render::R_SelectBackend( config, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    config.backend = render::render_backend_t::SOFTWARE;
    CHECK( render::R_SelectBackend( config, &backend ) ==
        render::render_error_t::ERR_BACKEND_NOT_BUILT );
    CHECK( backend == nullptr );

    config.backend = render::render_backend_t::VULKAN;
    CHECK( render::R_SelectBackend( config, &backend ) ==
        render::render_error_t::ERR_BACKEND_NOT_BUILT );
    CHECK( backend == nullptr );
}

TEST_CASE( "window configuration is applied before native window creation" )
{
    render::render_config_t config = render::R_DefaultConfig();
    config.sampleCount = 4u;
    config.validation = render::render_validation_t::VERBOSE;
    sys::window_desc_t windowDescription{};

    REQUIRE( render::R_ConfigureWindow( config, windowDescription ) ==
        render::render_error_t::OK );
    CHECK( windowDescription.graphicsApi == sys::window_graphics_api_t::OPENGL );
    CHECK( windowDescription.openGL.majorVersion == 4u );
    CHECK( windowDescription.openGL.minorVersion == 1u );
    CHECK( windowDescription.openGL.profile == sys::gl_profile_t::CORE );
    CHECK( ( windowDescription.openGL.flags & sys::GLIMP_CONTEXT_FORWARD_COMPATIBLE ) != 0u );
    CHECK( ( windowDescription.openGL.flags & sys::GLIMP_CONTEXT_DEBUG ) != 0u );
    CHECK( windowDescription.openGL.sampleCount == 4u );
    CHECK( windowDescription.openGL.sRGBFramebuffer );
    CHECK( windowDescription.openGL.requireAcceleration );
    CHECK( sys::Sys_WindowDescIsValid( windowDescription ) );
}

TEST_CASE( "renderer frontend rejects lifecycle calls before initialization" )
{
    REQUIRE_FALSE( render::R_IsInitialized() );
    REQUIRE_FALSE( render::R_IsFrameActive() );
    CHECK( render::R_GetInfo() == nullptr );
    CHECK( render::R_Shutdown() == render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_BeginFrame( {} ) == render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_Resize( { 1280u, 720u } ) == render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_EndFrame() == render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_SetPresentMode( render::render_present_mode_t::FIFO ) ==
        render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_WaitIdle() == render::render_error_t::ERR_NOT_INITIALIZED );
}

TEST_CASE( "buffer descriptors reject malformed policy and accept deferred allocation contents" )
{
    render::render_buffer_desc_t description{};
    description.byteSize = 64u;
    description.usage = render::R_BUFFER_USAGE_VERTEX |
        render::R_BUFFER_USAGE_TRANSFER_DESTINATION;
    description.updatePolicy = render::render_buffer_update_t::IMMUTABLE;
    description.memory = render::render_buffer_memory_t::DEVICE_LOCAL;

    // Static device storage may receive data from a future GPU transfer rather
    // than requiring CPU bytes during object creation.
    CHECK( render::R_ValidateBufferDesc( description, nullptr ) ==
        render::render_error_t::OK );

    const ::cypher::common::u32 words[16]{};
    const render::render_buffer_data_t completeData{ words, sizeof( words ) };
    CHECK( render::R_ValidateBufferDesc( description, &completeData ) ==
        render::render_error_t::OK );

    render::render_buffer_desc_t invalid = description;
    invalid.byteSize = 0u;
    CHECK( render::R_ValidateBufferDesc( invalid, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    invalid = description;
    invalid.usage = render::R_BUFFER_USAGE_NONE;
    CHECK( render::R_ValidateBufferDesc( invalid, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    invalid = description;
    invalid.usage |= CYPHER_BIT32( 31 );
    CHECK( render::R_ValidateBufferDesc( invalid, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    invalid = description;
    invalid.memory = render::render_buffer_memory_t::COUNT;
    CHECK( render::R_ValidateBufferDesc( invalid, nullptr ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    render::render_buffer_data_t malformedData{ nullptr, description.byteSize };
    CHECK( render::R_ValidateBufferDesc( description, &malformedData ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );

    malformedData = { words, description.byteSize - 1u };
    CHECK( render::R_ValidateBufferDesc( description, &malformedData ) ==
        render::render_error_t::ERR_INVALID_ARGUMENT );
}

TEST_CASE( "buffer operations reject calls before renderer initialization" )
{
    render::render_buffer_desc_t description{};
    description.byteSize = 16u;
    description.usage = render::R_BUFFER_USAGE_VERTEX;
    description.updatePolicy = render::render_buffer_update_t::DYNAMIC;
    description.memory = render::render_buffer_memory_t::UPLOAD;

    render::render_buffer_handle_t buffer{ 0xFFFFFFFFFFFFFFFFull };
    CHECK( render::R_CreateBuffer( description, nullptr, &buffer ) ==
        render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( buffer.value == render::R_INVALID_BUFFER.value );
    CHECK_FALSE( render::R_IsBufferValid( buffer ) );
}

TEST_CASE( "vertex format metadata distinguishes converted and integer inputs" )
{
    render::render_vertex_format_info_t info{};

    REQUIRE( render::R_GetVertexFormatInfo(
        render::render_format_t::RGBA8_UNORM,
        &info ) );
    CHECK( info.componentCount == 4u );
    CHECK( info.byteSize == 4u );
    CHECK( info.alignment == 1u );
    CHECK( info.valueType == render::render_vertex_value_t::FLOAT );
    CHECK( info.normalized );
    CHECK_FALSE( info.packed );

    REQUIRE( render::R_GetVertexFormatInfo(
        render::render_format_t::RGB32_UINT,
        &info ) );
    CHECK( info.componentCount == 3u );
    CHECK( info.byteSize == 12u );
    CHECK( info.alignment == 4u );
    CHECK( info.valueType == render::render_vertex_value_t::UINT );
    CHECK_FALSE( info.normalized );

    CHECK_FALSE( render::R_GetVertexFormatInfo(
        render::render_format_t::RGBA8_SRGB,
        &info ) );
    CHECK_FALSE( render::R_GetVertexFormatInfo(
        render::render_format_t::BC7_UNORM,
        &info ) );
    CHECK_FALSE( render::R_GetVertexFormatInfo(
        render::render_format_t::R32_FLOAT,
        nullptr ) );
    CHECK( render::R_IndexTypeSize( render::render_index_type_t::UINT16 ) == 2u );
    CHECK( render::R_IndexTypeSize( render::render_index_type_t::UINT32 ) == 4u );
    CHECK( render::R_IndexTypeSize( render::render_index_type_t::COUNT ) == 0u );
}

TEST_CASE( "vertex layouts enforce portable stream and attribute invariants" )
{
    const render::render_vertex_layout_t valid = MakeInterleavedVertexLayout();
    CHECK( render::R_ValidateVertexLayout( valid ) == render::render_error_t::OK );

    render::render_vertex_layout_t malformed = valid;
    malformed.bindingCount = 0u;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.bindings[0].stride = 12u;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.bindings[0].instanceDivisor = 1u;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.bindings[0].inputRate = render::render_vertex_input_rate_t::PER_INSTANCE;
    malformed.bindings[0].instanceDivisor = 0u;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.attributes[1].location = malformed.attributes[0].location;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.attributes[1].binding = 1u;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );

    malformed = valid;
    malformed.attributes[1].format = render::render_format_t::RGBA8_SRGB;
    CHECK( render::R_ValidateVertexLayout( malformed ) ==
        render::render_error_t::ERR_VERTEX_LAYOUT_INVALID );
}

TEST_CASE( "vertex input operations reject calls before renderer initialization" )
{
    render::render_vertex_input_desc_t description{};
    description.layout = MakeInterleavedVertexLayout();
    description.vertexBufferCount = 1u;
    description.vertexBuffers[0].binding = 0u;

    render::render_vertex_input_handle_t vertexInput{ 0xFFFFFFFFFFFFFFFFull };
    CHECK( render::R_ValidateVertexInputDesc( description ) ==
        render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( render::R_CreateVertexInput( description, &vertexInput ) ==
        render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( vertexInput.value == render::R_INVALID_VERTEX_INPUT.value );
    CHECK_FALSE( render::R_IsVertexInputValid( vertexInput ) );
}
