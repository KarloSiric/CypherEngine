//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherRender_HostSurface_OpenGL_Smoke_Tests.cpp
//  Purpose: Verifies rendering through a host-owned OpenGL context.
//  Details: The host creates and ultimately destroys the context. CypherRender
//           borrows it, prepares frames, and releases only renderer resources.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Target.h"
#include "CypherRender/CypherRender_Buffer.h"
#include "CypherRender/CypherRender_VertexInput.h"
#include "CypherSystem/Platform/CypherSystem_OpenGL.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <catch2/catch_test_macros.hpp>

#include <glad/gl.h>

#include <filesystem>
#include <string>

namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace common = ::cypher::common;

namespace
{

struct hosted_context_t {
    sys::window_t *pWindow{ nullptr };
    sys::gl_context_t *pContext{ nullptr };
    int nActivations{ 0 };
    int nFramePreparations{ 0 };
    GLuint presentationFramebuffer{ 0u };
    GLuint presentationReadFramebuffer{ 0u };
};

bool ActivateHostedContext( void *pUserData ) noexcept
{
    auto *pHost = static_cast<hosted_context_t *>( pUserData );
    if ( pHost == nullptr || pHost->pWindow == nullptr ||
         pHost->pContext == nullptr ) {
        return false;
    }
    ++pHost->nActivations;
    return sys::GLimp_MakeCurrent(
        *pHost->pWindow,
        *pHost->pContext ) == sys::sys_error_t::OK;
}

render::render_host_proc_t ResolveHostedProcedure(
    const char *pProcedureName,
    void * ) noexcept
{
    return reinterpret_cast<render::render_host_proc_t>(
        sys::GLimp_GetProcAddress( pProcedureName ) );
}

bool PrepareHostedFrame( void *pUserData ) noexcept
{
    auto *pHost = static_cast<hosted_context_t *>( pUserData );
    if ( pHost == nullptr ) return false;
    ++pHost->nFramePreparations;
    if ( pHost->presentationFramebuffer != 0u ) {
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pHost->presentationFramebuffer );
        glBindFramebuffer( GL_READ_FRAMEBUFFER, pHost->presentationReadFramebuffer );
    }
    return true;
}

GLuint CreateHostProgram()
{
    constexpr char vertexSource[] =
        "#version 410 core\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";
    constexpr char fragmentSource[] =
        "#version 410 core\nlayout(location=0) out vec4 color;\n"
        "void main() { color = vec4(1.0); }\n";
    const auto compile = []( const GLenum stage, const char *source ) {
        const GLuint shader = glCreateShader( stage );
        REQUIRE( shader != 0u );
        glShaderSource( shader, 1, &source, nullptr );
        glCompileShader( shader );
        GLint compiled = GL_FALSE;
        glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );
        REQUIRE( compiled == GL_TRUE );
        return shader;
    };
    const GLuint vertex = compile( GL_VERTEX_SHADER, vertexSource );
    const GLuint fragment = compile( GL_FRAGMENT_SHADER, fragmentSource );
    const GLuint program = glCreateProgram();
    REQUIRE( program != 0u );
    glAttachShader( program, vertex );
    glAttachShader( program, fragment );
    glLinkProgram( program );
    GLint linked = GL_FALSE;
    glGetProgramiv( program, GL_LINK_STATUS, &linked );
    REQUIRE( linked == GL_TRUE );
    glDeleteShader( vertex );
    glDeleteShader( fragment );
    return program;
}

} // namespace

TEST_CASE( "renderer borrows and preserves a host-owned OpenGL context",
           "[CypherRender][HostSurface][OpenGL][Smoke]" )
{
    const std::filesystem::path userPath =
        std::filesystem::temp_directory_path() /
        ( "cypher_render_host_surface_" +
          std::to_string( sys::Sys_GetCurrentProcessId() ) );
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_render_host_surface_opengl_smoke_tests",
        "-basedir",
        ".",
        "-userpath",
        userPathString.c_str()
    };
    const sys::init_info_t initInfo{
        5,
        arguments,
        "CypherRenderHostSurfaceSmokeTests",
        "CypherTests"
    };

    if ( sys::Sys_Init( initInfo ) != sys::sys_error_t::OK ) {
        SKIP( "CypherSystem could not initialize a desktop graphics session" );
    }

    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = render::render_present_mode_t::IMMEDIATE;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    config.sRGBFramebuffer = false;
    config.requireAcceleration = false;

    sys::window_desc_t windowDescription{};
    windowDescription.title = "CypherRender Host Surface Smoke Test";
    windowDescription.width = 64u;
    windowDescription.height = 64u;
    windowDescription.flags = sys::SYS_WINDOW_HIDDEN;
    REQUIRE( render::R_ConfigureWindow( config, windowDescription ) ==
        render::render_error_t::OK );

    sys::window_t window{};
    if ( sys::Sys_CreateWindow( windowDescription, window ) !=
         sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host has no OpenGL 4.1-capable window system" );
    }

    sys::gl_context_t context{};
    if ( sys::GLimp_CreateContext( window, context ) != sys::sys_error_t::OK ) {
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host graphics driver cannot create an OpenGL 4.1 context" );
    }

    REQUIRE( sys::GLimp_MakeCurrent( window, context ) == sys::sys_error_t::OK );
    sys::gl_context_info_t contextInfo{};
    REQUIRE( sys::GLimp_QueryContextInfo( contextInfo ) == sys::sys_error_t::OK );

    hosted_context_t host{ &window, &context };
    render::render_host_surface_desc_t surface{};
    surface.backend = render::render_backend_t::OPENGL;
    surface.drawableExtent = { window.width, window.height };
    surface.presentMode = render::render_present_mode_t::IMMEDIATE;
    surface.apiMajorVersion = contextInfo.majorVersion;
    surface.apiMinorVersion = contextInfo.minorVersion;
    surface.sampleCount = contextInfo.sampleCount;
    surface.sRGBFramebuffer = contextInfo.sRGBFramebuffer;
    surface.accelerated = contextInfo.accelerated;
    surface.userData = &host;
    surface.ActivateContext = ActivateHostedContext;
    surface.ResolveProcAddress = ResolveHostedProcedure;
    surface.PrepareFrame = PrepareHostedFrame;
    // Present stays null: this exercises host-owned widget presentation.

    REQUIRE( render::R_ValidateHostSurface( surface ) ==
        render::render_error_t::OK );
    render::render_config_t hostedConfig = config;
    hostedConfig.backend = render::render_backend_t::AUTO;
    const render::render_error_t rendererResult =
        render::R_InitHostSurface( surface, hostedConfig );
    if ( rendererResult != render::render_error_t::OK ) {
        REQUIRE( sys::GLimp_DestroyContext( context ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
        REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );
        std::error_code cleanupError{};
        std::filesystem::remove_all( userPath, cleanupError );
        SKIP( "The host graphics driver cannot initialize the borrowed OpenGL 4.1 surface" );
    }
    REQUIRE( render::R_IsInitialized() );
    CHECK( host.nActivations > 0 );

    const render::render_info_t *pInfo = render::R_GetInfo();
    REQUIRE( pInfo != nullptr );
    CHECK( pInfo->backend == render::render_backend_t::OPENGL );
    CHECK( pInfo->apiMajorVersion == contextInfo.majorVersion );
    CHECK( pInfo->apiMinorVersion == contextInfo.minorVersion );
    CHECK( pInfo->sampleCount == contextInfo.sampleCount );

    const int nActivationsAfterInit = host.nActivations;
    const render::render_extent_t resizedExtent{ 48u, 32u };
    glViewport( 7, 8, 30, 20 );
    REQUIRE( render::R_Resize( resizedExtent ) == render::render_error_t::OK );
    REQUIRE( render::R_GetInfo() != nullptr );
    CHECK( render::R_GetInfo()->drawableExtent.width == resizedExtent.width );
    CHECK( render::R_GetInfo()->drawableExtent.height == resizedExtent.height );
    // Hosted resize records dimensions; it must not activate or mutate the
    // host context between paint calls.
    CHECK( host.nActivations == nActivationsAfterInit );
    GLint resizeViewport[4]{};
    glGetIntegerv( GL_VIEWPORT, resizeViewport );
    CHECK( resizeViewport[0] == 7 );
    CHECK( resizeViewport[1] == 8 );
    CHECK( resizeViewport[2] == 30 );
    CHECK( resizeViewport[3] == 20 );

    // A host widget may present into its own nonzero FBO. The renderer must
    // restore this exact binding and viewport after an offscreen pass.
    GLuint hostColor = 0u;
    glGenTextures( 1, &hostColor );
    glBindTexture( GL_TEXTURE_2D, hostColor );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 48, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glGenFramebuffers( 1, &host.presentationFramebuffer );
    glBindFramebuffer( GL_FRAMEBUFFER, host.presentationFramebuffer );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, hostColor, 0 );
    REQUIRE( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
    GLuint hostReadColor = 0u;
    glGenTextures( 1, &hostReadColor );
    glBindTexture( GL_TEXTURE_2D, hostReadColor );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 48, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glGenFramebuffers( 1, &host.presentationReadFramebuffer );
    glBindFramebuffer( GL_FRAMEBUFFER, host.presentationReadFramebuffer );
    glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, hostReadColor, 0 );
    REQUIRE( glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
    REQUIRE( glGetError() == GL_NO_ERROR );

    render::render_texture_handle_t targetColor{};
    const render::render_texture_desc_t targetColorDesc{
        16u, 16u, false, false, false, "Hosted offscreen color", true };
    REQUIRE( render::R_CreateTexture2D( targetColorDesc, {}, &targetColor ) ==
        render::render_error_t::OK );
    render::render_target_handle_t target{};
    REQUIRE( render::R_CreateRenderTarget( { targetColor }, &target ) ==
        render::render_error_t::OK );

    // Render the offscreen color back into the host FBO. This also exercises
    // the program, VAO, uniform binding, texture unit, and sampler mutations
    // performed by a real renderer draw before EndFrame restores host state.
    constexpr char vertexSource[] =
        "#version 410 core\nlayout(location=0) in vec3 position;\n"
        "void main() { gl_Position = vec4(position, 1.0); }\n";
    constexpr char fragmentSource[] =
        "#version 410 core\nuniform sampler2D base_color;\n"
        "layout(location=0) out vec4 color;\n"
        "void main() { color = texture(base_color, vec2(0.5)); }\n";
    common::cooked_shader_view_t source{};
    source.nLanguageVersion = 410u;
    source.nStages = 2u;
    source.stages[0].stage = common::render_shader_stage_t::VERTEX;
    source.stages[0].code = {
        reinterpret_cast<const common::byte *>( vertexSource ), sizeof( vertexSource ) };
    source.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    source.stages[1].code = {
        reinterpret_cast<const common::byte *>( fragmentSource ), sizeof( fragmentSource ) };
    render::render_shader_handle_t shader{};
    REQUIRE( render::R_CreateShader( { &source, "Hosted target composite" }, &shader ) ==
        render::render_error_t::OK );
    const float positions[]{ -0.9f, -0.9f, 0.0f,
                              0.9f, -0.9f, 0.0f,
                              0.0f,  0.9f, 0.0f };
    const common::u16 indices[]{ 0u, 1u, 2u };
    const auto createBuffer = []( const void *bytes, const common::u64 size,
                                  const render::render_buffer_usage_flags_t usage ) {
        render::render_buffer_desc_t desc{};
        desc.byteSize = size;
        desc.usage = usage;
        const render::render_buffer_data_t data{ bytes, size };
        render::render_buffer_handle_t buffer{};
        REQUIRE( render::R_CreateBuffer( desc, &data, &buffer ) ==
            render::render_error_t::OK );
        return buffer;
    };
    const auto vertexBuffer = createBuffer(
        positions, sizeof( positions ), render::R_BUFFER_USAGE_VERTEX );
    const auto indexBuffer = createBuffer(
        indices, sizeof( indices ), render::R_BUFFER_USAGE_INDEX );
    render::render_vertex_input_desc_t inputDesc{};
    inputDesc.layout.bindingCount = 1u;
    inputDesc.layout.bindings[0].stride = 3u * sizeof( float );
    inputDesc.layout.attributeCount = 1u;
    inputDesc.layout.attributes[0] = {
        render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    inputDesc.vertexBufferCount = 1u;
    inputDesc.vertexBuffers[0] = { vertexBuffer, 0u, 0u };
    inputDesc.indexBuffer = {
        indexBuffer, 0u, render::render_index_type_t::UINT16 };
    render::render_vertex_input_handle_t input{};
    REQUIRE( render::R_CreateVertexInput( inputDesc, &input ) ==
        render::render_error_t::OK );
    render::render_pipeline_desc_t pipelineDesc{};
    pipelineDesc.shader = shader;
    pipelineDesc.vertexLayout = inputDesc.layout;
    pipelineDesc.sampledTextureName = "base_color";
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.cullBackFaces = false;
    render::render_pipeline_handle_t pipeline{};
    REQUIRE( render::R_CreateGraphicsPipeline( pipelineDesc, &pipeline ) ==
        render::render_error_t::OK );

    // The host leaves a deliberate nondefault state at the paint boundary.
    // BeginFrame may override it, but EndFrame must return exactly to it.
    const GLuint hostProgram = CreateHostProgram();
    GLuint hostVertexArray = 0u;
    glGenVertexArrays( 1, &hostVertexArray );
    glBindVertexArray( hostVertexArray );
    GLuint hostUniform = 0u;
    glGenBuffers( 1, &hostUniform );
    glBindBuffer( GL_UNIFORM_BUFFER, hostUniform );
    glBufferData( GL_UNIFORM_BUFFER, 64, nullptr, GL_STATIC_DRAW );
    glBindBufferBase( GL_UNIFORM_BUFFER, 0u, hostUniform );
    GLuint hostSamplers[2]{};
    glGenSamplers( 2, hostSamplers );
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, hostColor );
    glBindSampler( 0u, hostSamplers[0] );
    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_2D, hostReadColor );
    glBindSampler( 2u, hostSamplers[1] );
    glUseProgram( hostProgram );
    glEnable( GL_SCISSOR_TEST );
    glScissor( 3, 4, 11, 13 );
    glColorMask( GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE );
    glDepthMask( GL_FALSE );
    glStencilMask( 0x5au );
    glClearColor( 0.125f, 0.25f, 0.375f, 0.5f );
    glClearDepth( 0.25 );
    glClearStencil( 7 );
    glViewport( 2, 3, 40, 25 );
    glEnable( GL_BLEND );
    glEnable( GL_DEPTH_TEST );
    glEnable( GL_CULL_FACE );
    glCullFace( GL_FRONT );
    glFrontFace( GL_CW );
    GLint maxViewports = 0;
    GLint maxDrawBuffers = 0;
    glGetIntegerv( GL_MAX_VIEWPORTS, &maxViewports );
    glGetIntegerv( GL_MAX_DRAW_BUFFERS, &maxDrawBuffers );
    REQUIRE( maxViewports >= 1 );
    REQUIRE( maxDrawBuffers >= 1 );
    if ( maxViewports > 1 ) {
        glViewportIndexedf( 1u, 7.0f, 8.0f, 23.0f, 19.0f );
        glScissorIndexed( 1u, 9, 10, 12, 14 );
        glEnablei( GL_SCISSOR_TEST, 1u );
    }
    if ( maxDrawBuffers > 1 ) {
        glColorMaski( 1u, GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE );
        glEnablei( GL_BLEND, 1u );
        glBlendFuncSeparatei( 1u, GL_DST_COLOR, GL_ONE_MINUS_SRC_COLOR,
            GL_ONE, GL_ZERO );
        glBlendEquationSeparatei( 1u, GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD );
    }
    REQUIRE( glGetError() == GL_NO_ERROR );

    render::render_frame_info_t frame{};
    frame.frameIndex = 1u;
    frame.deltaSeconds = 1.0f / 60.0f;
    frame.drawableExtent = resizedExtent;
    frame.clearFlags = render::R_CLEAR_COLOR;
    frame.clearColor = { 0.0f, 1.0f, 0.0f, 1.0f };

    // This renderer currently writes one fragment color. A host MRT FBO must
    // be rejected before BeginFrame clears either of its attachments.
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, host.presentationFramebuffer );
    glDisablei( GL_SCISSOR_TEST, 0u );
    glColorMaski( 0u, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    const GLfloat untouchedColor[4]{ 0.25f, 0.5f, 0.75f, 1.0f };
    glClearBufferfv( GL_COLOR, 0, untouchedColor );
    glEnablei( GL_SCISSOR_TEST, 0u );
    glColorMaski( 0u, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE );
    GLuint hostMrtColor = 0u;
    glGenTextures( 1, &hostMrtColor );
    glBindTexture( GL_TEXTURE_2D, hostMrtColor );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 48, 32, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glBindTexture( GL_TEXTURE_2D, hostReadColor );
    glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
        GL_TEXTURE_2D, hostMrtColor, 0 );
    const GLenum twoDrawBuffers[]{ GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers( 2, twoDrawBuffers );
    REQUIRE( glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE );
    REQUIRE( glGetError() == GL_NO_ERROR );
    REQUIRE( render::R_BeginFrame( frame ) ==
        render::render_error_t::ERR_RENDER_TARGET_INVALID );
    CHECK_FALSE( render::R_IsFrameActive() );
    CHECK( host.nFramePreparations == 1 );
    GLint rejectedViewport[4]{};
    glGetIntegerv( GL_VIEWPORT, rejectedViewport );
    CHECK( rejectedViewport[0] == 2 );
    CHECK( rejectedViewport[1] == 3 );
    CHECK( rejectedViewport[2] == 40 );
    CHECK( rejectedViewport[3] == 25 );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationFramebuffer );
    glReadBuffer( GL_COLOR_ATTACHMENT0 );
    unsigned char rejectedPixel[4]{};
    glReadPixels( 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rejectedPixel );
    CHECK( rejectedPixel[0] >= 55u );
    CHECK( rejectedPixel[1] >= 120u );
    CHECK( rejectedPixel[2] >= 180u );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationReadFramebuffer );
    const GLenum oneDrawBuffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers( 1, &oneDrawBuffer );
    REQUIRE( glGetError() == GL_NO_ERROR );

    REQUIRE( render::R_BeginFrame( frame ) == render::render_error_t::OK );
    GLint bound = 0;
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationFramebuffer );
    render::render_target_pass_desc_t pass{};
    pass.target = target;
    pass.clearColor = { 1.0f, 0.0f, 0.0f, 1.0f };
    REQUIRE( render::R_BeginTargetPass( pass ) == render::render_error_t::OK );
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) != host.presentationFramebuffer );
    REQUIRE( render::R_EndTargetPass() == render::render_error_t::OK );
    glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationFramebuffer );
    glGetIntegerv( GL_READ_FRAMEBUFFER_BINDING, &bound );
    CHECK( static_cast<GLuint>( bound ) == host.presentationReadFramebuffer );
    GLint viewport[4]{};
    glGetIntegerv( GL_VIEWPORT, viewport );
    CHECK( viewport[0] == 0 );
    CHECK( viewport[1] == 0 );
    CHECK( viewport[2] == 48 );
    CHECK( viewport[3] == 32 );
    render::render_draw_indexed_desc_t draw{};
    draw.pipeline = pipeline;
    draw.vertexInput = input;
    draw.sampledTexture = targetColor;
    draw.indexCount = draw.vertexCount = 3u;
    REQUIRE( render::R_DrawIndexed( draw ) == render::render_error_t::OK );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationFramebuffer );
    glReadBuffer( GL_COLOR_ATTACHMENT0 );
    unsigned char hostPixel[4]{};
    glReadPixels( 24, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hostPixel );
    CHECK( hostPixel[0] >= 240u );
    CHECK( hostPixel[1] <= 15u );
    glReadPixels( 1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hostPixel );
    CHECK( hostPixel[1] >= 240u );
    CHECK( hostPixel[0] <= 15u );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, host.presentationReadFramebuffer );
    REQUIRE( glGetError() == GL_NO_ERROR );
    REQUIRE( render::R_EndFrame() == render::render_error_t::OK );
    CHECK( host.nFramePreparations == 2 );

    // A widget can continue its own painting immediately after EndFrame. Its
    // draw/read targets and unrelated GL bindings must be unchanged by Cypher.
    const auto checkInteger = []( const GLenum parameter, const GLint expected ) {
        GLint actual = 0;
        glGetIntegerv( parameter, &actual );
        CHECK( actual == expected );
    };
    checkInteger( GL_DRAW_FRAMEBUFFER_BINDING,
        static_cast<GLint>( host.presentationFramebuffer ) );
    checkInteger( GL_READ_FRAMEBUFFER_BINDING,
        static_cast<GLint>( host.presentationReadFramebuffer ) );
    checkInteger( GL_CURRENT_PROGRAM, static_cast<GLint>( hostProgram ) );
    checkInteger( GL_VERTEX_ARRAY_BINDING, static_cast<GLint>( hostVertexArray ) );
    checkInteger( GL_UNIFORM_BUFFER_BINDING, static_cast<GLint>( hostUniform ) );
    GLint indexedUniform = 0;
    glGetIntegeri_v( GL_UNIFORM_BUFFER_BINDING, 0u, &indexedUniform );
    CHECK( static_cast<GLuint>( indexedUniform ) == hostUniform );
    checkInteger( GL_ACTIVE_TEXTURE, GL_TEXTURE2 );
    glActiveTexture( GL_TEXTURE0 );
    checkInteger( GL_TEXTURE_BINDING_2D, static_cast<GLint>( hostColor ) );
    checkInteger( GL_SAMPLER_BINDING, static_cast<GLint>( hostSamplers[0] ) );
    glActiveTexture( GL_TEXTURE2 );
    checkInteger( GL_TEXTURE_BINDING_2D, static_cast<GLint>( hostReadColor ) );
    checkInteger( GL_SAMPLER_BINDING, static_cast<GLint>( hostSamplers[1] ) );
    GLint restoredViewport[4]{};
    glGetIntegerv( GL_VIEWPORT, restoredViewport );
    CHECK( restoredViewport[0] == 2 );
    CHECK( restoredViewport[1] == 3 );
    CHECK( restoredViewport[2] == 40 );
    CHECK( restoredViewport[3] == 25 );
    CHECK( glIsEnabled( GL_SCISSOR_TEST ) == GL_TRUE );
    GLint scissorBox[4]{};
    glGetIntegerv( GL_SCISSOR_BOX, scissorBox );
    CHECK( scissorBox[0] == 3 );
    CHECK( scissorBox[1] == 4 );
    CHECK( scissorBox[2] == 11 );
    CHECK( scissorBox[3] == 13 );
    GLboolean colorMask[4]{};
    glGetBooleanv( GL_COLOR_WRITEMASK, colorMask );
    CHECK( colorMask[0] == GL_FALSE );
    CHECK( colorMask[1] == GL_TRUE );
    CHECK( colorMask[2] == GL_FALSE );
    CHECK( colorMask[3] == GL_TRUE );
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv( GL_DEPTH_WRITEMASK, &depthMask );
    CHECK( depthMask == GL_FALSE );
    checkInteger( GL_STENCIL_WRITEMASK, 0x5a );
    GLfloat clearColor[4]{};
    glGetFloatv( GL_COLOR_CLEAR_VALUE, clearColor );
    CHECK( clearColor[0] == 0.125f );
    CHECK( clearColor[1] == 0.25f );
    CHECK( clearColor[2] == 0.375f );
    CHECK( clearColor[3] == 0.5f );
    GLdouble clearDepth = 0.0;
    glGetDoublev( GL_DEPTH_CLEAR_VALUE, &clearDepth );
    CHECK( clearDepth == 0.25 );
    checkInteger( GL_STENCIL_CLEAR_VALUE, 7 );
    CHECK( glIsEnabled( GL_BLEND ) == GL_TRUE );
    CHECK( glIsEnabled( GL_DEPTH_TEST ) == GL_TRUE );
    CHECK( glIsEnabled( GL_CULL_FACE ) == GL_TRUE );
    checkInteger( GL_CULL_FACE_MODE, GL_FRONT );
    checkInteger( GL_FRONT_FACE, GL_CW );
    if ( maxViewports > 1 ) {
        GLfloat extraViewport[4]{};
        glGetFloati_v( GL_VIEWPORT, 1u, extraViewport );
        CHECK( extraViewport[0] == 7.0f );
        CHECK( extraViewport[1] == 8.0f );
        CHECK( extraViewport[2] == 23.0f );
        CHECK( extraViewport[3] == 19.0f );
        GLint extraScissor[4]{};
        glGetIntegeri_v( GL_SCISSOR_BOX, 1u, extraScissor );
        CHECK( extraScissor[0] == 9 );
        CHECK( extraScissor[1] == 10 );
        CHECK( extraScissor[2] == 12 );
        CHECK( extraScissor[3] == 14 );
        CHECK( glIsEnabledi( GL_SCISSOR_TEST, 1u ) == GL_TRUE );
    }
    if ( maxDrawBuffers > 1 ) {
        GLboolean extraMask[4]{};
        glGetBooleani_v( GL_COLOR_WRITEMASK, 1u, extraMask );
        CHECK( extraMask[0] == GL_TRUE );
        CHECK( extraMask[1] == GL_FALSE );
        CHECK( extraMask[2] == GL_TRUE );
        CHECK( extraMask[3] == GL_FALSE );
        CHECK( glIsEnabledi( GL_BLEND, 1u ) == GL_TRUE );
        const auto checkIndexedInteger = []( const GLenum parameter,
                                             const GLint expected ) {
            GLint actual = 0;
            glGetIntegeri_v( parameter, 1u, &actual );
            CHECK( actual == expected );
        };
        checkIndexedInteger( GL_BLEND_SRC_RGB, GL_DST_COLOR );
        checkIndexedInteger( GL_BLEND_DST_RGB, GL_ONE_MINUS_SRC_COLOR );
        checkIndexedInteger( GL_BLEND_SRC_ALPHA, GL_ONE );
        checkIndexedInteger( GL_BLEND_DST_ALPHA, GL_ZERO );
        checkIndexedInteger( GL_BLEND_EQUATION_RGB, GL_FUNC_REVERSE_SUBTRACT );
        checkIndexedInteger( GL_BLEND_EQUATION_ALPHA, GL_FUNC_ADD );
    }
    REQUIRE( glGetError() == GL_NO_ERROR );

    REQUIRE( render::R_DestroyGraphicsPipeline( pipeline ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyVertexInput( input ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( vertexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyBuffer( indexBuffer ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyShader( shader ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyRenderTarget( target ) == render::render_error_t::OK );
    REQUIRE( render::R_DestroyTexture( targetColor ) == render::render_error_t::OK );

    const int nActivationsBeforeShutdown = host.nActivations;
    REQUIRE( render::R_Shutdown() == render::render_error_t::OK );
    CHECK_FALSE( render::R_IsInitialized() );
    CHECK( host.nActivations > nActivationsBeforeShutdown );
    // CypherRender borrowed the context; ownership remains with this host.
    REQUIRE( sys::GLimp_IsContextValid( context ) );
    CHECK( glIsFramebuffer( host.presentationFramebuffer ) == GL_TRUE );
    CHECK( glIsFramebuffer( host.presentationReadFramebuffer ) == GL_TRUE );
    glUseProgram( 0u );
    glBindVertexArray( 0u );
    glBindBufferBase( GL_UNIFORM_BUFFER, 0u, 0u );
    glActiveTexture( GL_TEXTURE0 );
    glBindTexture( GL_TEXTURE_2D, 0u );
    glBindSampler( 0u, 0u );
    glActiveTexture( GL_TEXTURE2 );
    glBindTexture( GL_TEXTURE_2D, 0u );
    glBindSampler( 2u, 0u );
    glActiveTexture( GL_TEXTURE0 );
    glDeleteProgram( hostProgram );
    glDeleteVertexArrays( 1, &hostVertexArray );
    glDeleteBuffers( 1, &hostUniform );
    glDeleteSamplers( 2, hostSamplers );
    glDeleteFramebuffers( 1, &host.presentationFramebuffer );
    glDeleteFramebuffers( 1, &host.presentationReadFramebuffer );
    glDeleteTextures( 1, &hostColor );
    glDeleteTextures( 1, &hostReadColor );
    glDeleteTextures( 1, &hostMrtColor );
    REQUIRE( sys::GLimp_DestroyContext( context ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_DestroyWindow( window ) == sys::sys_error_t::OK );
    REQUIRE( sys::Sys_Shutdown() == sys::sys_error_t::OK );

    std::error_code cleanupError{};
    std::filesystem::remove_all( userPath, cleanupError );
}
