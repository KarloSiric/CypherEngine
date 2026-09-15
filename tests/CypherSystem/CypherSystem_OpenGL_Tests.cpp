//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherSystem/CypherSystem_OpenGL_Tests.cpp
//  Purpose: Verifies the public OpenGL context contract without requiring a display.
//  Details: Real driver/context creation belongs to the renderer smoke test. These
//           tests remain deterministic in headless CI and catch invalid policy,
//           stale handles, and calls made outside the required lifetime.
//
//  History:
//  - Created by Karlo Siric on 2026-08-31
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_OpenGL.h"
#include "CypherSystem_Window.h" // Complete window_t needed by lifecycle rejection tests.

#include <catch2/catch_test_macros.hpp>

#include <cstring> // std::memset used to verify failed queries clear outputs.

using namespace cypher::engine::sys;

namespace
{

gl_context_desc_t MakeDesktopContextDescription() noexcept
{
    gl_context_desc_t description{};
    description.majorVersion = 4u;
    description.minorVersion = 1u;
    description.profile = gl_profile_t::CORE;
    description.flags = GLIMP_CONTEXT_DEBUG | GLIMP_CONTEXT_FORWARD_COMPATIBLE;
    return description;
}

} // namespace

TEST_CASE( "System validates OpenGL context descriptions", "[CypherSystem][OpenGL][Config]" )
{
    gl_context_desc_t description{};
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    description = MakeDesktopContextDescription();
    REQUIRE( GLimp_ContextDescIsValid( description ) );

    description.majorVersion = 3u;
    description.minorVersion = 1u;
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    description.minorVersion = 2u;
    REQUIRE( GLimp_ContextDescIsValid( description ) );

    description = MakeDesktopContextDescription();
    description.flags |= CYPHER_BIT32( 31 );
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    description = MakeDesktopContextDescription();
    description.sampleCount = 1u;
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    description.sampleCount = 3u;
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    description.sampleCount = 4u;
    REQUIRE( GLimp_ContextDescIsValid( description ) );

    description = MakeDesktopContextDescription();
    description.redBits = 0u;
    REQUIRE_FALSE( GLimp_ContextDescIsValid( description ) );

    gl_context_desc_t esDescription{};
    esDescription.majorVersion = 2u;
    esDescription.profile = gl_profile_t::ES;
    REQUIRE( GLimp_ContextDescIsValid( esDescription ) );
}

TEST_CASE( "System OpenGL operations reject missing runtime state", "[CypherSystem][OpenGL][Lifecycle]" )
{
    window_t unavailableWindow{};
    gl_context_t unavailableContext{};

    REQUIRE_FALSE( GLimp_IsContextValid( unavailableContext ) );
    REQUIRE( GLimp_CreateContext( unavailableWindow, unavailableContext ) ==
        sys_error_t::ERR_NOT_INIT );
    REQUIRE( GLimp_MakeCurrent( unavailableWindow, unavailableContext ) ==
        sys_error_t::ERR_NOT_INIT );
    REQUIRE( GLimp_DestroyContext( unavailableContext ) ==
        sys_error_t::ERR_NOT_INIT );
    REQUIRE( GLimp_GetProcAddress( "glGetString" ) == nullptr );
    REQUIRE( GLimp_GetProcAddress( nullptr ) == nullptr );

    gl_context_info_t contextInfo{};
    std::memset( &contextInfo, 0xA5, sizeof( contextInfo ) );
    REQUIRE( GLimp_QueryContextInfo( contextInfo ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( contextInfo.majorVersion == 0u );
    REQUIRE( contextInfo.profile == gl_profile_t::NONE );

    gl_swap_interval_t interval = gl_swap_interval_t::SYNCHRONIZED;
    REQUIRE( GLimp_SetSwapInterval( gl_swap_interval_t::SYNCHRONIZED ) ==
        sys_error_t::ERR_NOT_INIT );
    REQUIRE( GLimp_GetSwapInterval( interval ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( interval == gl_swap_interval_t::IMMEDIATE );
    REQUIRE( GLimp_SwapWindow( unavailableWindow ) == sys_error_t::ERR_NOT_INIT );
}
