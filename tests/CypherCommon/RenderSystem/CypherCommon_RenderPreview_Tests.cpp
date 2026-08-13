//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/RenderSystem/CypherCommon_RenderPreview_Tests.cpp
//  Purpose: Tests the backend-neutral render preview service facade.
//  Details: Coverage protects request validation, target capabilities, bounded
//           output, metadata transactions, and backend contract enforcement.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RenderPreview.h"

#include "CypherCommon_StringView.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;

namespace
{

struct preview_backend_t {
    u32 cCalls{ 0u };
    usize cbObserved{ 0u };
    render_preview_status_t status{ render_preview_status_t::OK };
};

render_preview_status_t TestRender(
    void *pUserData,
    const render_preview_request_t &request,
    byte_span_t output ) noexcept
{
    auto &backend = *static_cast<preview_backend_t *>( pUserData );
    ++backend.cCalls;
    backend.cbObserved = output.nCount;
    if ( backend.status != render_preview_status_t::OK ) {
        return backend.status;
    }

    for ( usize iByte = 0u; iByte < output.nCount; ++iByte ) {
        output.pData[iByte] = static_cast<byte>(
            ( request.nRequestId + iByte ) & 0xFFu );
    }
    return render_preview_status_t::OK;
}

resource_handle_t TestHandle() noexcept
{
    return ResourceHandle_Make( 3u, 7u, 2u );
}

render_preview_service_t TestService( preview_backend_t &backend ) noexcept
{
    return {
        TestRender,
        &backend,
        RENDER_PREVIEW_CAPABILITY_TEXTURE |
            RENDER_PREVIEW_CAPABILITY_MATERIAL,
        { 512u, 512u }
    };
}

render_preview_request_t TextureRequest() noexcept
{
    render_preview_request_t request{};
    request.nRequestId = 19u;
    request.nDocumentRevision = 5u;
    request.resource = TestHandle();
    request.nWidth = 4u;
    request.nHeight = 2u;
    return request;
}

} // namespace

TEST_CASE( "Render preview dispatch writes caller-owned RGBA output",
           "[CypherCommon][RenderPreview]" )
{
    preview_backend_t backend{};
    const render_preview_service_t service = TestService( backend );
    const render_preview_request_t request = TextureRequest();
    byte pixels[32]{};

    const render_preview_result_t result = RenderPreview_Render(
        &service,
        request,
        { pixels, sizeof( pixels ) } );
    REQUIRE( result.status == render_preview_status_t::OK );
    REQUIRE( result.nRequestId == request.nRequestId );
    REQUIRE( result.nDocumentRevision == request.nDocumentRevision );
    REQUIRE( result.nWidth == 4u );
    REQUIRE( result.nHeight == 2u );
    REQUIRE( result.cbRowPitch == 16u );
    REQUIRE( result.cbWritten == sizeof( pixels ) );
    REQUIRE( backend.cCalls == 1u );
    REQUIRE( backend.cbObserved == sizeof( pixels ) );
    REQUIRE( pixels[0] == 19u );
}

TEST_CASE( "Render preview validates requests before dispatch",
           "[CypherCommon][RenderPreview][Validation]" )
{
    preview_backend_t backend{};
    render_preview_service_t service = TestService( backend );
    render_preview_request_t request = TextureRequest();

    REQUIRE( RenderPreview_IsServiceValid( &service ) );
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::OK );

    request.nRequestId = 0u;
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request = TextureRequest();
    request.resource = {};
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request = TextureRequest();
    request.nWidth = 513u;
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request = TextureRequest();
    request.flags = CYPHER_BIT32( 31u );
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request = TextureRequest();
    request.texture.exposure = std::numeric_limits<f32>::infinity();
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request = TextureRequest();
    request.background.r = std::numeric_limits<f32>::quiet_NaN();
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );

    REQUIRE( backend.cCalls == 0u );
}

TEST_CASE( "Render preview target capabilities are explicit",
           "[CypherCommon][RenderPreview][Capabilities]" )
{
    preview_backend_t backend{};
    render_preview_service_t service = TestService( backend );
    service.capabilities = RENDER_PREVIEW_CAPABILITY_TEXTURE;
    render_preview_request_t request = TextureRequest();
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::OK );

    request.target = render_preview_target_t::MATERIAL;
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::UNSUPPORTED_TARGET );
}

TEST_CASE( "Render preview accepts one transient cooked document source",
           "[CypherCommon][RenderPreview][Source]" )
{
    preview_backend_t backend{};
    const render_preview_service_t service = TestService( backend );
    render_preview_request_t request = TextureRequest();
    const byte cooked[4]{ 1u, 2u, 3u, 4u };
    request.source = render_preview_source_t::COOKED_RESOURCE;
    request.resource = {};
    request.cookedResource = { cooked, sizeof( cooked ) };
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::OK );

    request.resource = TestHandle();
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
    request.resource = {};
    request.cookedResource = {};
    REQUIRE( RenderPreview_ValidateRequest( &service, request ) ==
             render_preview_status_t::INVALID_REQUEST );
}

TEST_CASE( "Render preview output and result metadata are transactional",
           "[CypherCommon][RenderPreview][Failure]" )
{
    preview_backend_t backend{};
    const render_preview_service_t service = TestService( backend );
    const render_preview_request_t request = TextureRequest();
    byte pixels[32]{};

    render_preview_result_t result = RenderPreview_Render(
        &service,
        request,
        { pixels, sizeof( pixels ) - 1u } );
    REQUIRE( result.status == render_preview_status_t::OUTPUT_TOO_SMALL );
    REQUIRE( result.nRequestId == 0u );
    REQUIRE( result.cbWritten == 0u );
    REQUIRE( backend.cCalls == 0u );

    backend.status = render_preview_status_t::RESOURCE_UNAVAILABLE;
    result = RenderPreview_Render(
        &service,
        request,
        { pixels, sizeof( pixels ) } );
    REQUIRE( result.status == render_preview_status_t::RESOURCE_UNAVAILABLE );
    REQUIRE( result.nRequestId == 0u );
    REQUIRE( result.cbWritten == 0u );

    backend.status = render_preview_status_t::INVALID_REQUEST;
    result = RenderPreview_Render(
        &service,
        request,
        { pixels, sizeof( pixels ) } );
    REQUIRE( result.status == render_preview_status_t::CONTRACT_VIOLATION );
}

TEST_CASE( "Render preview size arithmetic and status names are stable",
           "[CypherCommon][RenderPreview][Helpers]" )
{
    REQUIRE( RenderPreview_RequiredOutputSize(
                 4u,
                 2u,
                 render_preview_output_format_t::RGBA8_SRGB ) == 32u );
    REQUIRE( RenderPreview_RequiredOutputSize(
                 0u,
                 2u,
                 render_preview_output_format_t::RGBA8_SRGB ) == 0u );
    REQUIRE( RenderPreview_RequiredOutputSize(
                 2u,
                 2u,
                 static_cast<render_preview_output_format_t>( 99u ) ) == 0u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( RenderPreview_StatusName(
            render_preview_status_t::BACKEND_ERROR ) ),
        StringView_FromCString( "BACKEND_ERROR" ) ) );
}
