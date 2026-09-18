//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_Shader_Tests.cpp
//  Purpose: Verifies public shader ownership without a native graphics context.
//  Details: A small backend substitute tests validation, metadata snapshots,
//           stale handles, retained pipeline references, and native failures.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Local.h"

#include <catch2/catch_test_macros.hpp>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;

namespace
{

constexpr char g_source[] = "#version 410 core\nvoid main() {}\n";

struct shader_backend_t {
    render::render_error_t createResult{ render::render_error_t::OK };
    render::render_error_t destroyResult{ render::render_error_t::OK };
    common::u64 nextToken{ 1u };
    unsigned createCalls{ 0u };
    unsigned destroyCalls{ 0u };
};

render::render_error_t CreateShader(
    const render::render_shader_desc_t &, render::backend_shader_t &out, void *state ) noexcept
{
    auto &backend = *static_cast<shader_backend_t *>( state );
    ++backend.createCalls;
    out = {};
    if ( backend.createResult == render::render_error_t::OK ) out.value = backend.nextToken++;
    return backend.createResult;
}

render::render_error_t DestroyShader( render::backend_shader_t, void *state ) noexcept
{
    auto &backend = *static_cast<shader_backend_t *>( state );
    ++backend.destroyCalls;
    return backend.destroyResult;
}

struct shader_scope_t {
    render::renderer_state_t saved{ render::tr };
    shader_backend_t backend{};
    render::backend_api_t api{};

    shader_scope_t()
    {
        api.CreateShader = CreateShader;
        api.DestroyShader = DestroyShader;
        api.state = &backend;
        render::tr = {};
        render::tr.backend = &api;
        render::tr.info.backend = render::render_backend_t::OPENGL;
        render::tr.info.apiMajorVersion = 4u;
        render::tr.info.apiMinorVersion = 1u;
        render::tr.initialized = true;
    }

    ~shader_scope_t()
    {
        (void)render::R_ShaderSystemShutdown();
        render::tr = saved;
    }

    shader_scope_t( const shader_scope_t & ) = delete;
    shader_scope_t &operator=( const shader_scope_t & ) = delete;
};

common::cooked_shader_view_t MakeCooked()
{
    common::cooked_shader_view_t cooked{};
    cooked.nLanguageVersion = 410u;
    cooked.nStages = 2u;
    cooked.stages[0].stage = common::render_shader_stage_t::VERTEX;
    cooked.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    for ( auto &stage : cooked.stages ) {
        stage.code = { reinterpret_cast<const common::byte *>( g_source ), sizeof( g_source ) };
    }
    return cooked;
}

} // namespace

TEST_CASE( "shader frontend validates cooked data before backend creation", "[CypherRender][Shader]" )
{
    shader_scope_t scope;
    REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
    auto cooked = MakeCooked();
    render::render_shader_desc_t description{ &cooked, "validation" };
    auto expected = render::render_error_t::ERR_SHADER_DATA_INVALID;
    const char invalidUtf8[]{ static_cast<char>( 0xFFu ), '\0' };

    SECTION( "null view" ) {
        description.cookedShader = nullptr;
        expected = render::render_error_t::ERR_INVALID_ARGUMENT;
    }
    SECTION( "incompatible backend" ) { cooked.backend = static_cast<common::render_shader_backend_t>( 99u ); }
    SECTION( "invalid kind" ) { cooked.kind = static_cast<common::render_shader_program_kind_t>( 99u ); }
    SECTION( "unknown flags" ) { cooked.flags = 1u; }
    SECTION( "unsupported profile" ) { cooked.languageProfile = static_cast<common::render_shader_language_profile_t>( 99u ); }
    SECTION( "invalid language version" ) { cooked.nLanguageVersion = 123u; }
    SECTION( "newer language than active backend" ) {
        cooked.nLanguageVersion = 450u;
        expected = render::render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
    }
    SECTION( "stage count exceeds the view" ) { cooked.nStages = 3u; }
    SECTION( "duplicate stages" ) { cooked.stages[1].stage = common::render_shader_stage_t::VERTEX; }
    SECTION( "unknown stage" ) {
        cooked.stages[1].stage = static_cast<common::render_shader_stage_t>( 99u );
        expected = render::render_error_t::ERR_SHADER_STAGE_UNSUPPORTED;
    }
    SECTION( "bad stage bytes" ) { cooked.stages[1].code.pData = nullptr; }
    SECTION( "invalid UTF-8" ) {
        cooked.stages[0].code = { reinterpret_cast<const common::byte *>( invalidUtf8 ), sizeof( invalidUtf8 ) };
    }

    render::render_shader_handle_t shader{ 123u };
    CHECK( render::R_CreateShader( description, &shader ) == expected );
    CHECK( shader.value == 0u );
    CHECK( scope.backend.createCalls == 0u );
}

TEST_CASE( "shader metadata is copied and destroyed handles remain stale", "[CypherRender][Shader]" )
{
    shader_scope_t scope;
    REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
    auto cooked = MakeCooked();
    render::render_shader_handle_t first{};
    REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &first ) == render::render_error_t::OK );
    CHECK( render::R_IsShaderValid( first ) );
    CHECK( common::Cy_Handle64Type( first ) == static_cast<common::u32>( render::render_object_type_t::SHADER ) );
    cooked = common::cooked_shader_view_t{}; // The caller may release or reuse its cooked view after creation.
    render::render_shader_info_t info{};
    REQUIRE( render::R_GetShaderInfo( first, &info ) == render::render_error_t::OK );
    CHECK( info.languageVersion == 410u );
    CHECK( info.stageCount == 2u );
    REQUIRE( render::R_DestroyShader( first ) == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsShaderValid( first ) );

    cooked = MakeCooked();
    render::render_shader_handle_t second{};
    REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &second ) == render::render_error_t::OK );
    CHECK( first.value != second.value );
    CHECK( render::R_GetShaderInfo( first, &info ) == render::render_error_t::ERR_STALE_HANDLE );
    CHECK( info.stageCount == 0u );
    CHECK( info.languageVersion == 0u );
    const auto buffer = common::Cy_Handle64Make( 0u, 1u,
        static_cast<common::u32>( render::render_object_type_t::BUFFER ) );
    CHECK( render::R_DestroyShader( buffer ) == render::render_error_t::ERR_RESOURCE_TYPE_MISMATCH );
    CHECK( render::R_IsShaderValid( second ) );
}

TEST_CASE( "shader references block destruction until every pipeline releases them", "[CypherRender][Shader]" )
{
    shader_scope_t scope;
    REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
    const auto cooked = MakeCooked();
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &shader ) == render::render_error_t::OK );
    render::backend_shader_t native{};
    REQUIRE( render::R_ShaderAcquireReference( shader, &native ) == render::render_error_t::OK );
    CHECK( native.value == 1u );
    REQUIRE( render::R_ShaderAcquireReference( shader, &native ) == render::render_error_t::OK );
    CHECK( render::R_DestroyShader( shader ) == render::render_error_t::ERR_RESOURCE_BUSY );
    CHECK( scope.backend.destroyCalls == 0u );
    REQUIRE( render::R_ShaderReleaseReference( shader ) == render::render_error_t::OK );
    CHECK( render::R_DestroyShader( shader ) == render::render_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( render::R_ShaderReleaseReference( shader ) == render::render_error_t::OK );
    CHECK( render::R_ShaderReleaseReference( shader ) == render::render_error_t::ERR_INVALID_STATE );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
    CHECK( scope.backend.destroyCalls == 1u );
    CHECK( render::R_ShaderAcquireReference( shader, &native ) == render::render_error_t::ERR_STALE_HANDLE );
    CHECK( native.value == 0u );
}

TEST_CASE( "shader native failures preserve ownership and deterministic outputs", "[CypherRender][Shader]" )
{
    shader_scope_t scope;
    REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
    const auto cooked = MakeCooked();
    render::render_shader_handle_t shader{ 123u };
    scope.backend.createResult = render::render_error_t::ERR_SHADER_LINK_FAILED;
    CHECK( render::R_CreateShader( { &cooked, nullptr }, &shader ) == render::render_error_t::ERR_SHADER_LINK_FAILED );
    CHECK( shader.value == 0u );
    scope.backend.createResult = render::render_error_t::OK;
    REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &shader ) == render::render_error_t::OK );
    scope.backend.destroyResult = render::render_error_t::ERR_DEVICE_LOST;
    CHECK( render::R_DestroyShader( shader ) == render::render_error_t::ERR_DEVICE_LOST );
    CHECK( render::R_IsShaderValid( shader ) );
    scope.backend.destroyResult = render::render_error_t::OK;
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsShaderValid( shader ) );
}

TEST_CASE( "shader shutdown releases all native programs before disabling its table", "[CypherRender][Shader]" )
{
    shader_scope_t scope;
    REQUIRE( render::R_ShaderSystemInit() == render::render_error_t::OK );
    CHECK( render::R_ShaderSystemInit() == render::render_error_t::ERR_ALREADY_INITIALIZED );
    const auto cooked = MakeCooked();
    render::render_shader_handle_t shaders[2]{};
    for ( auto &shader : shaders ) REQUIRE( render::R_CreateShader( { &cooked, nullptr }, &shader ) == render::render_error_t::OK );
    REQUIRE( render::R_ShaderSystemShutdown() == render::render_error_t::OK );
    CHECK( scope.backend.destroyCalls == 2u );
    for ( const auto shader : shaders ) CHECK_FALSE( render::R_IsShaderValid( shader ) );
    CHECK( render::R_CreateShader( { &cooked, nullptr }, &shaders[0] ) == render::render_error_t::ERR_NOT_INITIALIZED );
    CHECK( shaders[0].value == 0u );
    CHECK( render::R_GetShaderInfo( shaders[1], nullptr ) == render::render_error_t::ERR_INVALID_ARGUMENT );
    CHECK( render::R_ShaderSystemShutdown() == render::render_error_t::OK );
}
