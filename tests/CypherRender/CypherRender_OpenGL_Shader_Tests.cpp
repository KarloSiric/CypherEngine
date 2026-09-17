//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherRender/CypherRender_OpenGL_Shader_Tests.cpp
//  Purpose: Verifies native shader-stage ownership and failure reporting.
//  Details: Scoped GLAD substitutes exercise driver failure paths without a
//           window or graphics context. Every process-wide hook is restored.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender/OpenGL/CypherRender_OpenGL_Local.h"
#include "CypherRender/OpenGL/CypherRender_OpenGL_Shader.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"
#include "CypherCommon/Tier0/CypherCommon_LogToggle.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace render = ::cypher::engine::render;
namespace common = ::cypher::common;

namespace
{

constexpr char g_source[] = "#version 410 core\nvoid main() {}\n";

enum class failure_point_t { NONE, CREATE, SOURCE, COMPILE, DIAGNOSTIC, PROGRAM_CREATE, ATTACH, LINK, PROGRAM_DELETE };

struct captured_log_t {
    common::log_level_t level{};
    common::error_code_t errorCode{};
    char text[512]{};
};

struct shader_driver_t {
    failure_point_t failure{ failure_point_t::NONE };
    GLenum injectedError{ GL_OUT_OF_MEMORY };
    GLenum pendingError{ GL_NO_ERROR };
    GLuint createdShader{ 37u };
    GLint compileStatus{ GL_TRUE };
    unsigned compileFailureOnCall{ 0u };
    unsigned createCalls{ 0u };
    GLuint program{ 91u };
    GLint linkStatus{ GL_TRUE };
    unsigned programCreateCalls{ 0u };
    unsigned attachCalls{ 0u };
    unsigned detachCalls{ 0u };
    unsigned linkCalls{ 0u };
    unsigned programDeleteCalls{ 0u };
    unsigned useProgramCalls{ 0u };
    const char *diagnostic{ "mock driver: invalid shader expression" };
    unsigned nativeCalls{ 0u };
    unsigned sourceCalls{ 0u };
    unsigned compileCalls{ 0u };
    unsigned deleteCalls{ 0u };
    unsigned diagnosticCalls{ 0u };
    GLenum createdStage{ 0u };
    GLuint sourcedShader{ 0u };
    GLuint compiledShader{ 0u };
    GLuint deletedShader{ 0u };
    const GLchar *source{ nullptr };
    GLsizei sourceCount{ 0 };
    GLint sourceLength{ -1 };
    bool live{ false };
    bool diagnosticReadWhileLive{ false };
    captured_log_t logs[8]{};
    unsigned logCount{ 0u };
};

shader_driver_t *g_driver = nullptr;

void InjectError( const failure_point_t point )
{
    if ( g_driver->failure == point ) g_driver->pendingError = g_driver->injectedError;
}

GLenum GLAD_API_PTR MockGetError()
{
    ++g_driver->nativeCalls;
    const GLenum error = g_driver->pendingError;
    g_driver->pendingError = GL_NO_ERROR;
    return error;
}

GLuint GLAD_API_PTR MockCreateShader( const GLenum stage )
{
    ++g_driver->nativeCalls;
    ++g_driver->createCalls;
    g_driver->createdStage = stage;
    g_driver->live = g_driver->createdShader != 0u;
    InjectError( failure_point_t::CREATE );
    return g_driver->createdShader != 0u
        ? g_driver->createdShader + g_driver->createCalls - 1u : 0u;
}

void GLAD_API_PTR MockShaderSource(
    const GLuint shader, const GLsizei count,
    const GLchar *const *strings, const GLint *lengths )
{
    ++g_driver->nativeCalls;
    ++g_driver->sourceCalls;
    g_driver->sourcedShader = shader;
    g_driver->sourceCount = count;
    g_driver->source = strings != nullptr && count > 0 ? strings[0] : nullptr;
    g_driver->sourceLength = lengths != nullptr && count > 0 ? lengths[0] : -1;
    InjectError( failure_point_t::SOURCE );
}

void GLAD_API_PTR MockCompileShader( const GLuint shader )
{
    ++g_driver->nativeCalls;
    ++g_driver->compileCalls;
    g_driver->compiledShader = shader;
    InjectError( failure_point_t::COMPILE );
}

void GLAD_API_PTR MockGetShaderiv( GLuint, const GLenum name, GLint *valueOut )
{
    ++g_driver->nativeCalls;
    if ( name == GL_COMPILE_STATUS ) {
        *valueOut = g_driver->compileFailureOnCall == g_driver->compileCalls
            ? GL_FALSE : g_driver->compileStatus;
    } else if ( name == GL_INFO_LOG_LENGTH ) {
        *valueOut = static_cast<GLint>( std::strlen( g_driver->diagnostic ) + 1u );
    } else {
        g_driver->pendingError = GL_INVALID_ENUM;
    }
}

void GLAD_API_PTR MockGetShaderInfoLog(
    GLuint, const GLsizei capacity, GLsizei *lengthOut, GLchar *textOut )
{
    ++g_driver->nativeCalls;
    ++g_driver->diagnosticCalls;
    g_driver->diagnosticReadWhileLive = g_driver->live;
    if ( capacity > 0 ) {
        const auto length = std::min(
            std::strlen( g_driver->diagnostic ), static_cast<std::size_t>( capacity - 1 ) );
        std::memcpy( textOut, g_driver->diagnostic, length );
        textOut[length] = '\0';
        if ( lengthOut != nullptr ) *lengthOut = static_cast<GLsizei>( length );
    }
    InjectError( failure_point_t::DIAGNOSTIC );
}

void GLAD_API_PTR MockDeleteShader( const GLuint shader )
{
    ++g_driver->nativeCalls;
    ++g_driver->deleteCalls;
    g_driver->deletedShader = shader;
    g_driver->live = false;
}

GLuint GLAD_API_PTR MockCreateProgram()
{
    ++g_driver->programCreateCalls;
    InjectError( failure_point_t::PROGRAM_CREATE );
    return g_driver->program;
}

void GLAD_API_PTR MockAttachShader( GLuint, GLuint )
{
    ++g_driver->attachCalls;
    InjectError( failure_point_t::ATTACH );
}

void GLAD_API_PTR MockDetachShader( GLuint, GLuint ) { ++g_driver->detachCalls; }

void GLAD_API_PTR MockLinkProgram( GLuint )
{
    ++g_driver->linkCalls;
    InjectError( failure_point_t::LINK );
}

void GLAD_API_PTR MockGetProgramiv( GLuint, const GLenum name, GLint *valueOut )
{
    if ( name == GL_LINK_STATUS ) *valueOut = g_driver->linkStatus;
    else if ( name == GL_INFO_LOG_LENGTH ) {
        *valueOut = static_cast<GLint>( std::strlen( g_driver->diagnostic ) + 1u );
    } else g_driver->pendingError = GL_INVALID_ENUM;
}

void GLAD_API_PTR MockDeleteProgram( GLuint )
{
    ++g_driver->programDeleteCalls;
    InjectError( failure_point_t::PROGRAM_DELETE );
}
void GLAD_API_PTR MockUseProgram( GLuint ) { ++g_driver->useProgramCalls; }

void CaptureLog( const common::log_record_t &record, void *userData ) noexcept
{
    auto &driver = *static_cast<shader_driver_t *>( userData );
    if ( driver.logCount >= 8u ) return;
    captured_log_t &log = driver.logs[driver.logCount++];
    log.level = record.level;
    log.errorCode = record.errorCode;
    std::snprintf( log.text, sizeof( log.text ), "%s", record.pMessage );
}

// Catch2 runs these cases serially. RAII also restores hooks after REQUIRE aborts.
struct shader_scope_t {
    shader_driver_t driver{};
    shader_driver_t *previousDriver{ g_driver };
    bool initialized{ render::glState.initialized };
    render::render_validation_t validation{ render::glState.config.validation };
    PFNGLGETERRORPROC getError{ glad_glGetError };
    PFNGLCREATESHADERPROC createShader{ glad_glCreateShader };
    PFNGLSHADERSOURCEPROC shaderSource{ glad_glShaderSource };
    PFNGLCOMPILESHADERPROC compileShader{ glad_glCompileShader };
    PFNGLGETSHADERIVPROC getShaderiv{ glad_glGetShaderiv };
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog{ glad_glGetShaderInfoLog };
    PFNGLDELETESHADERPROC deleteShader{ glad_glDeleteShader };
    PFNGLCREATEPROGRAMPROC createProgram{ glad_glCreateProgram };
    PFNGLATTACHSHADERPROC attachShader{ glad_glAttachShader };
    PFNGLDETACHSHADERPROC detachShader{ glad_glDetachShader };
    PFNGLLINKPROGRAMPROC linkProgram{ glad_glLinkProgram };
    PFNGLGETPROGRAMIVPROC getProgramiv{ glad_glGetProgramiv };
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog{ glad_glGetProgramInfoLog };
    PFNGLDELETEPROGRAMPROC deleteProgram{ glad_glDeleteProgram };
    PFNGLUSEPROGRAMPROC useProgram{ glad_glUseProgram };
    char languageVersion[render::GL_STRING_CAPACITY]{};
    common::log_callback_t logCallback{ nullptr };
    void *logUserData{ nullptr };
    common::log_category_mask_t logMask{ common::Cy_LogToggleGetMask() };

    shader_scope_t()
    {
        g_driver = &driver;
        render::glState.initialized = true;
        render::glState.config.validation = render::render_validation_t::DISABLED;
        glad_glGetError = MockGetError;
        glad_glCreateShader = MockCreateShader;
        glad_glShaderSource = MockShaderSource;
        glad_glCompileShader = MockCompileShader;
        glad_glGetShaderiv = MockGetShaderiv;
        glad_glGetShaderInfoLog = MockGetShaderInfoLog;
        glad_glDeleteShader = MockDeleteShader;
        glad_glCreateProgram = MockCreateProgram;
        glad_glAttachShader = MockAttachShader;
        glad_glDetachShader = MockDetachShader;
        glad_glLinkProgram = MockLinkProgram;
        glad_glGetProgramiv = MockGetProgramiv;
        glad_glGetProgramInfoLog = MockGetShaderInfoLog;
        glad_glDeleteProgram = MockDeleteProgram;
        glad_glUseProgram = MockUseProgram;
        std::memcpy( languageVersion, render::glState.shadingLanguageVersion, sizeof( languageVersion ) );
        std::snprintf( render::glState.shadingLanguageVersion, sizeof( languageVersion ), "4.10 mock" );
        common::Cy_LogGetCallback( &logCallback, &logUserData );
        common::Cy_LogSetCallback( CaptureLog, &driver );
        common::Cy_LogToggleEnable( common::Cy_LogChannelMask( common::log_channel_t::Render ) );
    }

    ~shader_scope_t()
    {
        common::Cy_LogSetCallback( logCallback, logUserData );
        common::Cy_LogToggleSetMask( logMask );
        glad_glGetError = getError;
        glad_glCreateShader = createShader;
        glad_glShaderSource = shaderSource;
        glad_glCompileShader = compileShader;
        glad_glGetShaderiv = getShaderiv;
        glad_glGetShaderInfoLog = getShaderInfoLog;
        glad_glDeleteShader = deleteShader;
        glad_glCreateProgram = createProgram;
        glad_glAttachShader = attachShader;
        glad_glDetachShader = detachShader;
        glad_glLinkProgram = linkProgram;
        glad_glGetProgramiv = getProgramiv;
        glad_glGetProgramInfoLog = getProgramInfoLog;
        glad_glDeleteProgram = deleteProgram;
        glad_glUseProgram = useProgram;
        std::memcpy( render::glState.shadingLanguageVersion, languageVersion, sizeof( languageVersion ) );
        render::glState.initialized = initialized;
        render::glState.config.validation = validation;
        g_driver = previousDriver;
    }

    shader_scope_t( const shader_scope_t & ) = delete;
    shader_scope_t &operator=( const shader_scope_t & ) = delete;
};

common::cooked_shader_stage_view_t MakeStage()
{
    common::cooked_shader_stage_view_t stage{};
    stage.code = { reinterpret_cast<const common::byte *>( g_source ), sizeof( g_source ) };
    return stage;
}

common::cooked_shader_view_t MakeProgram()
{
    common::cooked_shader_view_t program{};
    program.nLanguageVersion = 410u;
    program.nStages = 2u;
    program.stages[0] = MakeStage();
    program.stages[1] = MakeStage();
    program.stages[1].stage = common::render_shader_stage_t::FRAGMENT;
    return program;
}

bool HasLog( const shader_driver_t &driver, const char *text,
             const common::log_level_t level, const common::error_code_t error )
{
    for ( unsigned index = 0u; index < driver.logCount; ++index ) {
        const captured_log_t &log = driver.logs[index];
        if ( log.level == level && log.errorCode == error && std::strstr( log.text, text ) ) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE( "shader stages reject invalid inputs before native work", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    auto stage = MakeStage();
    auto expected = render::render_error_t::ERR_SHADER_DATA_INVALID;
    const char embeddedNul[]{ 'x', '\0', 'y', '\0' };

    SECTION( "backend unavailable" ) {
        render::glState.initialized = false;
        expected = render::render_error_t::ERR_NOT_INITIALIZED;
    }
    SECTION( "unsupported stage" ) {
        stage.stage = static_cast<common::render_shader_stage_t>( 99u );
        expected = render::render_error_t::ERR_SHADER_STAGE_UNSUPPORTED;
    }
    SECTION( "unsupported code format" ) {
        stage.codeFormat = static_cast<common::render_shader_code_format_t>( 99u );
    }
    SECTION( "unknown stage flags" ) { stage.flags = 1u; }
    SECTION( "null data" ) { stage.code.pData = nullptr; }
    SECTION( "empty span" ) { stage.code.cbSize = 0u; }
    SECTION( "empty source" ) {
        stage.code = { reinterpret_cast<const common::byte *>( "" ), 1u };
    }
    SECTION( "oversized span" ) { stage.code.cbSize = common::CY_COOKED_SHADER_MAX_CODE_SIZE + 1u; }
    SECTION( "missing final NUL" ) { --stage.code.cbSize; }
    SECTION( "embedded NUL" ) {
        stage.code = { reinterpret_cast<const common::byte *>( embeddedNul ), sizeof( embeddedNul ) };
    }

    GLuint shader = 999u;
    CHECK( render::GL_CompileShaderStage( stage, "invalid", shader ) == expected );
    CHECK( shader == 0u );
    CHECK( scope.driver.nativeCalls == 0u );
}

TEST_CASE( "shader stages pass exact source length and transfer successful ownership", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    auto stage = MakeStage();
    GLenum expectedStage = GL_VERTEX_SHADER;
    SECTION( "vertex" ) {}
    SECTION( "fragment" ) {
        stage.stage = common::render_shader_stage_t::FRAGMENT;
        expectedStage = GL_FRAGMENT_SHADER;
    }

    GLuint shader = 999u;
    REQUIRE( render::GL_CompileShaderStage( stage, nullptr, shader ) == render::render_error_t::OK );
    CHECK( shader == scope.driver.createdShader );
    CHECK( scope.driver.createdStage == expectedStage );
    CHECK( scope.driver.sourceCount == 1 );
    CHECK( scope.driver.source == g_source );
    CHECK( scope.driver.sourceLength == static_cast<GLint>( sizeof( g_source ) - 1u ) );
    CHECK( scope.driver.sourcedShader == shader );
    CHECK( scope.driver.compiledShader == shader );
    CHECK( scope.driver.deleteCalls == 0u );
    CHECK( scope.driver.live );
    CHECK( scope.driver.logCount == 0u );
    glDeleteShader( shader ); // Successful compilation transfers cleanup to its caller.
    CHECK_FALSE( scope.driver.live );
}

TEST_CASE( "shader compilation failure reports diagnostics before deleting its stage", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    scope.driver.compileStatus = GL_FALSE;
    GLuint shader = 999u;
    CHECK( render::GL_CompileShaderStage( MakeStage(), "arena shader", shader ) ==
        render::render_error_t::ERR_SHADER_COMPILE_FAILED );
    CHECK( shader == 0u );
    CHECK( scope.driver.diagnosticReadWhileLive );
    CHECK( scope.driver.deleteCalls == 1u );
    CHECK( scope.driver.deletedShader == scope.driver.createdShader );
    CHECK_FALSE( scope.driver.live );
    const auto error = render::R_ErrorCode( render::render_error_t::ERR_SHADER_COMPILE_FAILED );
    CHECK( HasLog( scope.driver, "arena shader", common::log_level_t::Error, error ) );
    CHECK( HasLog( scope.driver, scope.driver.diagnostic, common::log_level_t::Error, error ) );
}

TEST_CASE( "shader allocation failures preserve the native error and leave no output", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    scope.driver.createdShader = 0u;
    auto expected = render::render_error_t::ERR_RESOURCE_CREATE_FAILED;
    SECTION( "zero without a native error" ) {}
    SECTION( "native out of memory" ) {
        scope.driver.failure = failure_point_t::CREATE;
        expected = render::render_error_t::ERR_OUT_OF_MEMORY;
    }

    GLuint shader = 999u;
    CHECK( render::GL_CompileShaderStage( MakeStage(), nullptr, shader ) == expected );
    CHECK( shader == 0u );
    CHECK( scope.driver.sourceCalls == 0u );
    CHECK( scope.driver.compileCalls == 0u );
    CHECK( scope.driver.deleteCalls == 0u );
}

TEST_CASE( "shader native operation errors clean up even with validation disabled", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    SECTION( "source upload" ) { scope.driver.failure = failure_point_t::SOURCE; }
    SECTION( "native compilation" ) { scope.driver.failure = failure_point_t::COMPILE; }
    SECTION( "allocation returned an object and an error" ) { scope.driver.failure = failure_point_t::CREATE; }

    GLuint shader = 999u;
    CHECK( render::GL_CompileShaderStage( MakeStage(), nullptr, shader ) ==
        render::render_error_t::ERR_OUT_OF_MEMORY );
    CHECK( shader == 0u );
    CHECK( scope.driver.deleteCalls == 1u );
    CHECK( scope.driver.deletedShader == scope.driver.createdShader );
    CHECK_FALSE( scope.driver.live );
    CHECK( scope.driver.diagnosticCalls == 0u );
    if ( scope.driver.failure != failure_point_t::COMPILE ) CHECK( scope.driver.compileCalls == 0u );
}

TEST_CASE( "shader diagnostic failures retain the original compilation error", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    scope.driver.compileStatus = GL_FALSE;
    scope.driver.failure = failure_point_t::DIAGNOSTIC;
    GLuint shader = 999u;
    CHECK( render::GL_CompileShaderStage( MakeStage(), nullptr, shader ) ==
        render::render_error_t::ERR_SHADER_COMPILE_FAILED );
    CHECK( shader == 0u );
    CHECK( scope.driver.diagnosticCalls == 1u );
    CHECK( scope.driver.deleteCalls == 1u );
    CHECK_FALSE( scope.driver.live );
    CHECK( HasLog( scope.driver, "Could not retrieve", common::log_level_t::Warning, common::CY_ERROR_OK ) );
}

TEST_CASE( "shader programs release temporary stages and do not change the current binding", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    const auto cooked = MakeProgram();
    render::backend_shader_t shader{ 999u };
    REQUIRE( render::GL_CreateShader( { &cooked, "program" }, shader, &render::glState ) == render::render_error_t::OK );
    CHECK( shader.value == scope.driver.program );
    CHECK( scope.driver.createCalls == 2u );
    CHECK( scope.driver.attachCalls == 2u );
    CHECK( scope.driver.detachCalls == 2u );
    CHECK( scope.driver.deleteCalls == 2u );
    CHECK( scope.driver.linkCalls == 1u );
    CHECK( scope.driver.programDeleteCalls == 0u );
    CHECK( scope.driver.useProgramCalls == 0u );
    CHECK( render::GL_DestroyShader( shader, &render::glState ) == render::render_error_t::OK );
    CHECK( scope.driver.programDeleteCalls == 1u );
}

TEST_CASE( "shader program creation rolls back stage and program failures", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    const auto cooked = MakeProgram();
    auto expected = render::render_error_t::ERR_OUT_OF_MEMORY;
    unsigned programDeletes = 1u;
    SECTION( "second stage compilation" ) {
        scope.driver.compileFailureOnCall = 2u;
        expected = render::render_error_t::ERR_SHADER_COMPILE_FAILED;
        programDeletes = 0u;
    }
    SECTION( "program allocation" ) { scope.driver.failure = failure_point_t::PROGRAM_CREATE; }
    SECTION( "attachment" ) { scope.driver.failure = failure_point_t::ATTACH; }
    SECTION( "native link operation" ) { scope.driver.failure = failure_point_t::LINK; }
    SECTION( "link interface rejection" ) {
        scope.driver.linkStatus = GL_FALSE;
        expected = render::render_error_t::ERR_SHADER_LINK_FAILED;
    }
    SECTION( "link diagnostics also fail" ) {
        scope.driver.linkStatus = GL_FALSE;
        scope.driver.failure = failure_point_t::DIAGNOSTIC;
        expected = render::render_error_t::ERR_SHADER_LINK_FAILED;
    }
    render::backend_shader_t shader{ 999u };
    CHECK( render::GL_CreateShader( { &cooked, "rollback" }, shader, &render::glState ) == expected );
    CHECK( shader.value == 0u );
    CHECK( scope.driver.deleteCalls == 2u );
    CHECK( scope.driver.programDeleteCalls == programDeletes );
    CHECK( scope.driver.useProgramCalls == 0u );
}

TEST_CASE( "shader program metadata is checked before native work", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    auto cooked = MakeProgram();
    auto expected = render::render_error_t::ERR_SHADER_DATA_INVALID;
    SECTION( "too many stages" ) { cooked.nStages = 3u; }
    SECTION( "missing fragment" ) { cooked.stages[1].stage = common::render_shader_stage_t::VERTEX; }
    SECTION( "future backend" ) { cooked.backend = static_cast<common::render_shader_backend_t>( 99u ); }
    SECTION( "unsupported GLSL" ) {
        cooked.nLanguageVersion = 450u;
        expected = render::render_error_t::ERR_BACKEND_VERSION_UNSUPPORTED;
    }
    render::backend_shader_t shader{ 999u };
    CHECK( render::GL_CreateShader( { &cooked, nullptr }, shader, &render::glState ) == expected );
    CHECK( shader.value == 0u );
    CHECK( scope.driver.nativeCalls == 0u );
    CHECK( scope.driver.programCreateCalls == 0u );
}

TEST_CASE( "shader program destruction validates ownership and preserves native failure", "[CypherRender][OpenGL][Shader]" )
{
    shader_scope_t scope;
    render::backend_shader_t shader{ scope.driver.program };
    void *state = &render::glState;
    auto expected = render::render_error_t::ERR_INVALID_HANDLE;
    unsigned deleteCalls = 0u;
    SECTION( "wrong backend state" ) {
        state = nullptr;
        expected = render::render_error_t::ERR_INVALID_ARGUMENT;
    }
    SECTION( "backend unavailable" ) {
        render::glState.initialized = false;
        expected = render::render_error_t::ERR_NOT_INITIALIZED;
    }
    SECTION( "invalid token" ) { shader = {}; }
    SECTION( "token wider than a native name" ) {
        shader.value = static_cast<common::u64>( std::numeric_limits<GLuint>::max() ) + 1u;
    }
    SECTION( "native deletion failure" ) {
        scope.driver.failure = failure_point_t::PROGRAM_DELETE;
        expected = render::render_error_t::ERR_OUT_OF_MEMORY;
        deleteCalls = 1u;
    }
    CHECK( render::GL_DestroyShader( shader, state ) == expected );
    CHECK( scope.driver.programDeleteCalls == deleteCalls );
    CHECK( scope.driver.useProgramCalls == 0u );
}
