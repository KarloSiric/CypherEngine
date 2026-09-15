//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_Contract_Tests.cpp
//  Purpose: Locks down the renderer header ABI before backend implementation.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Backend.h"
#include "CypherRender/CypherRender_Public.h"

#include <type_traits>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;

static_assert( sizeof( render::render_backend_t ) == 1u );
static_assert( sizeof( render::render_mode_t ) == 1u );
static_assert( sizeof( render::render_present_mode_t ) == 1u );
static_assert( sizeof( render::render_validation_t ) == 1u );
static_assert( sizeof( render::render_handle_t ) == 8u );
static_assert( sizeof( render::backend_buffer_t ) == 8u );
static_assert( sizeof( render::backend_vertex_input_t ) == 8u );
static_assert( sizeof( render::render_clear_flags_t ) == 4u );

// Capability positions are persistent contract values and may not be reordered.
static_assert( render::R_CAPABILITY_RASTERIZATION == CYPHER_BIT64( 0 ) );
static_assert( render::R_CAPABILITY_COMPUTE == CYPHER_BIT64( 4 ) );
static_assert( render::R_CAPABILITY_RAY_TRACING_PIPELINE == CYPHER_BIT64( 27 ) );
static_assert( render::R_CAPABILITY_KNOWN_MASK == CYPHER_BIT64( 28 ) - 1u );
static_assert( render::R_CLEAR_FLAG_MASK ==
    ( render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH | render::R_CLEAR_STENCIL ) );

static_assert( render::R_INVALID_HANDLE.value == 0u );
static_assert( render::R_INVALID_BACKEND_BUFFER.value == 0u );
static_assert( render::R_INVALID_BUFFER.value == 0u );
static_assert( render::R_INVALID_VERTEX_INPUT.value == 0u );
static_assert( render::R_INVALID_BACKEND_VERTEX_INPUT.value == 0u );
static_assert( std::is_trivially_copyable_v<render::render_buffer_desc_t> );
static_assert( std::is_trivially_copyable_v<render::render_buffer_info_t> );
static_assert( std::is_trivially_copyable_v<render::render_vertex_layout_t> );
static_assert( std::is_trivially_copyable_v<render::render_vertex_input_desc_t> );
static_assert( std::is_trivially_copyable_v<render::render_vertex_input_info_t> );
static_assert( std::is_trivially_copyable_v<render::render_config_t> );
static_assert( std::is_trivially_copyable_v<render::render_info_t> );
static_assert( std::is_trivially_copyable_v<render::render_frame_info_t> );
static_assert( std::is_standard_layout_v<render::backend_api_t> );
static_assert( render::R_BACKEND_API_SIZE == sizeof( render::backend_api_t ) );

using configure_window_fn_t = render::render_error_t (*)(
    const render::render_config_t &,
    sys::window_desc_t & ) noexcept;
using init_fn_t = render::render_error_t (*)(
    sys::window_t &,
    const render::render_config_t & ) noexcept;
using begin_frame_fn_t = render::render_error_t (*)(
    const render::render_frame_info_t & ) noexcept;
using set_present_mode_fn_t = render::render_error_t (*)(
    render::render_present_mode_t ) noexcept;
using wait_idle_fn_t = render::render_error_t (*)() noexcept;
using create_buffer_fn_t = render::render_error_t (*)(
    const render::render_buffer_desc_t &,
    const render::render_buffer_data_t *,
    render::render_buffer_handle_t * ) noexcept;
using create_vertex_input_fn_t = render::render_error_t (*)(
    const render::render_vertex_input_desc_t &,
    render::render_vertex_input_handle_t * ) noexcept;

static_assert( std::is_same_v<
    decltype( &render::R_ConfigureWindow ),
    configure_window_fn_t> );
static_assert( std::is_same_v<decltype( &render::R_Init ), init_fn_t> );
static_assert( std::is_same_v<decltype( &render::R_BeginFrame ), begin_frame_fn_t> );
static_assert( std::is_same_v<
    decltype( &render::R_SetPresentMode ),
    set_present_mode_fn_t> );
static_assert( std::is_same_v<decltype( &render::R_WaitIdle ), wait_idle_fn_t> );
static_assert( std::is_same_v<decltype( &render::R_CreateBuffer ), create_buffer_fn_t> );
static_assert( std::is_same_v<
    decltype( &render::R_CreateVertexInput ),
    create_vertex_input_fn_t> );
static_assert( render::R_BACKEND_API_VERSION == 3u );

int main()
{
    const render::render_config_t config{};

    if ( config.backend != render::render_backend_t::AUTO ) {
        return 1;
    }
    if ( config.mode != render::render_mode_t::RASTER ) {
        return 1;
    }
    if ( config.presentMode != render::render_present_mode_t::FIFO ) {
        return 1;
    }
    if ( ( config.requiredCapabilities & render::R_CAPABILITY_RASTERIZATION ) == 0u ) {
        return 1;
    }

    return 0;
}
