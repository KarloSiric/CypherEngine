//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Error.cpp
//  Purpose: Implements allocation-free renderer error lookup.
//  Details: Symbolic names and descriptions remain usable during failed device
//           startup, low-memory handling, and orderly renderer teardown.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Error.h"

#include "CypherCommon/Tier0/CypherCommon_Defines.h"

namespace cypher::engine::render
{

namespace
{

using ::cypher::common::error_description_t;

#define R_ERROR_DESC( code, description ) \
    { static_cast<::cypher::common::u16>( render_error_t::code ), #code, description }

static const error_description_t g_renderErrorDescriptions[] = {
    R_ERROR_DESC( OK, "Renderer operation completed successfully." ),
    R_ERROR_DESC( ERR_FAILED, "Renderer operation failed." ),
    R_ERROR_DESC( ERR_INVALID_ARGUMENT, "The renderer received an invalid argument." ),
    R_ERROR_DESC( ERR_INVALID_STATE, "The renderer state does not permit this operation." ),
    R_ERROR_DESC( ERR_NOT_INITIALIZED, "The renderer has not been initialized." ),
    R_ERROR_DESC( ERR_ALREADY_INITIALIZED, "The renderer is already initialized." ),
    R_ERROR_DESC( ERR_OUT_OF_MEMORY, "Renderer memory allocation failed." ),
    R_ERROR_DESC( ERR_UNSUPPORTED, "The requested renderer operation is unsupported." ),
    R_ERROR_DESC( ERR_LIMIT_EXCEEDED, "The operation exceeds a renderer or device limit." ),
    R_ERROR_DESC( ERR_TIMEOUT, "A bounded renderer wait timed out." ),
    R_ERROR_DESC( ERR_INTERNAL_ERROR, "An internal renderer invariant failed." ),

    R_ERROR_DESC( ERR_BACKEND_NOT_BUILT, "The requested renderer backend is not part of this build." ),
    R_ERROR_DESC( ERR_BACKEND_UNAVAILABLE, "The requested renderer backend is unavailable on this machine." ),
    R_ERROR_DESC( ERR_API_VERSION_MISMATCH, "Renderer frontend and backend API versions do not match." ),
    R_ERROR_DESC( ERR_BACKEND_VERSION_UNSUPPORTED, "The required native graphics API version is unavailable." ),
    R_ERROR_DESC( ERR_CAPABILITY_MISSING, "The device is missing a required renderer capability." ),
    R_ERROR_DESC( ERR_WINDOW_INCOMPATIBLE, "The native window is incompatible with the selected renderer backend." ),
    R_ERROR_DESC( ERR_CONTEXT_CREATE_FAILED, "The graphics context or device could not be created." ),
    R_ERROR_DESC( ERR_CONTEXT_ACTIVATE_FAILED, "The graphics context could not be made current." ),
    R_ERROR_DESC( ERR_ENTRYPOINT_LOAD_FAILED, "Required graphics API entry points could not be loaded." ),
    R_ERROR_DESC( ERR_DEVICE_QUERY_FAILED, "Graphics device limits or capabilities could not be queried." ),
    R_ERROR_DESC( ERR_DEVICE_LOST, "The graphics device or context was lost." ),

    R_ERROR_DESC( ERR_FRAME_ALREADY_ACTIVE, "A renderer frame is already active." ),
    R_ERROR_DESC( ERR_FRAME_NOT_ACTIVE, "No renderer frame is active." ),
    R_ERROR_DESC( ERR_RENDER_TARGET_INVALID, "The active render target is invalid." ),
    R_ERROR_DESC( ERR_FRAMEBUFFER_INCOMPLETE, "The backend framebuffer is incomplete." ),
    R_ERROR_DESC( ERR_RESIZE_FAILED, "Renderer presentation storage could not be resized." ),
    R_ERROR_DESC( ERR_PRESENT_FAILED, "The completed frame could not be presented." ),
    R_ERROR_DESC( ERR_PRESENT_TARGET_LOST, "The native presentation target was lost." ),
    R_ERROR_DESC( ERR_PRESENT_TARGET_OUT_OF_DATE, "The presentation target must be recreated." ),

    R_ERROR_DESC( ERR_INVALID_HANDLE, "The renderer handle is invalid." ),
    R_ERROR_DESC( ERR_STALE_HANDLE, "The renderer handle refers to an expired generation." ),
    R_ERROR_DESC( ERR_RESOURCE_TYPE_MISMATCH, "The renderer handle has the wrong object type." ),
    R_ERROR_DESC( ERR_RESOURCE_NOT_READY, "The renderer resource is not ready for use." ),
    R_ERROR_DESC( ERR_RESOURCE_BUSY, "The renderer resource is still in use." ),
    R_ERROR_DESC( ERR_RESOURCE_CREATE_FAILED, "The native renderer resource could not be created." ),
    R_ERROR_DESC( ERR_RESOURCE_TRANSFER_FAILED, "A renderer resource transfer failed." ),
    R_ERROR_DESC( ERR_RESOURCE_FORMAT_UNSUPPORTED, "The requested renderer resource format is unsupported." ),
    R_ERROR_DESC( ERR_RESOURCE_SIZE_UNSUPPORTED, "The requested renderer resource size is unsupported." ),

    R_ERROR_DESC( ERR_SHADER_DATA_INVALID, "Cooked shader data is invalid or incompatible." ),
    R_ERROR_DESC( ERR_SHADER_STAGE_UNSUPPORTED, "The requested shader stage is unsupported." ),
    R_ERROR_DESC( ERR_SHADER_COMPILE_FAILED, "The native graphics driver rejected shader code." ),
    R_ERROR_DESC( ERR_SHADER_LINK_FAILED, "The shader stages could not be linked." ),
    R_ERROR_DESC( ERR_SHADER_INTERFACE_MISMATCH, "Shader stage interfaces do not agree." ),
    R_ERROR_DESC( ERR_PIPELINE_DESC_INVALID, "The renderer pipeline description is invalid." ),
    R_ERROR_DESC( ERR_PIPELINE_CREATE_FAILED, "The renderer pipeline could not be created." ),
    R_ERROR_DESC( ERR_BINDING_MISMATCH, "Bound resources do not match shader reflection." ),

    R_ERROR_DESC( ERR_VERTEX_LAYOUT_INVALID, "The vertex layout is invalid." ),
    R_ERROR_DESC( ERR_INDEX_DATA_INVALID, "The index data or range is invalid." ),
    R_ERROR_DESC( ERR_DRAW_DESC_INVALID, "The draw description is invalid." ),
    R_ERROR_DESC( ERR_COMMAND_STATE_INVALID, "Renderer commands were issued in an invalid order." ),
    R_ERROR_DESC( ERR_COMMAND_CAPACITY_EXCEEDED, "Renderer command storage is exhausted." ),
    R_ERROR_DESC( ERR_SUBMISSION_FAILED, "Recorded renderer work could not be submitted." ),
    R_ERROR_DESC( ERR_SYNCHRONIZATION_FAILED, "A renderer synchronization operation failed." ),

    R_ERROR_DESC( ERR_VIEW_INVALID, "The renderer view or viewport is invalid." ),
    R_ERROR_DESC( ERR_LIGHT_LIMIT_EXCEEDED, "The active light limit was exceeded." ),
    R_ERROR_DESC( ERR_SHADOW_STORAGE_EXHAUSTED, "Shadow storage is exhausted." ),
    R_ERROR_DESC( ERR_VISIBILITY_DATA_INVALID, "Renderer visibility data is invalid." ),
    R_ERROR_DESC( ERR_CAPTURE_UNSUPPORTED, "Frame capture is unsupported by this backend." ),
    R_ERROR_DESC( ERR_CAPTURE_FAILED, "Frame capture or readback failed." ),
    R_ERROR_DESC( INVALID, "The renderer returned an invalid error value." )
};

#undef R_ERROR_DESC

static const ::cypher::common::error_table_t g_renderErrorTable = {
    ::cypher::common::error_domain_t::RENDER,
    g_renderErrorDescriptions,
    CYPHER_ARRAY_COUNT( g_renderErrorDescriptions )
};

} // namespace

const char *R_ErrorName( const render_error_t error ) noexcept
{
    return ::cypher::common::Cy_ErrorFindName( g_renderErrorTable, R_ErrorCode( error ) );
}

const char *R_ErrorDescription( const render_error_t error ) noexcept
{
    return ::cypher::common::Cy_ErrorFindDescription(
        g_renderErrorTable,
        R_ErrorCode( error ) );
}

const ::cypher::common::error_table_t *R_ErrorTable() noexcept
{
    return &g_renderErrorTable;
}

} // namespace cypher::engine::render
