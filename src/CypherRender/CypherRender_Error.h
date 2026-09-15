//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Error.h
//  Purpose: Defines renderer-local failures and packed diagnostic conversion.
//  Details: Native driver and platform errors remain diagnostic context; the
//           public renderer boundary returns only stable Cypher error values.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_ERROR_H
#define CYPHER_ENGINE_RENDER_ERROR_H
#pragma once

#include "CypherCommon/Tier0/CypherCommon_Error.h"

namespace cypher::engine::render
{

/*
================
Renderer Error Codes

The high byte groups related failures. Native OpenGL, SDL, or Vulkan error
values remain diagnostic details and must never become the public result.
================
*/
enum class render_error_t : ::cypher::common::u16 {
    OK = 0x0000u,                         // Operation completed successfully.

    // General renderer state: 0x00xx.
    ERR_FAILED = 0x0001u,                 // Operation failed without a narrower result.
    ERR_INVALID_ARGUMENT,                 // Caller supplied invalid input.
    ERR_INVALID_STATE,                    // Current renderer state forbids the operation.
    ERR_NOT_INITIALIZED,                  // Renderer has not completed initialization.
    ERR_ALREADY_INITIALIZED,              // Initialization was requested twice.
    ERR_OUT_OF_MEMORY,                    // CPU or graphics memory allocation failed.
    ERR_UNSUPPORTED,                      // Operation is unsupported by this renderer.
    ERR_LIMIT_EXCEEDED,                   // A documented renderer limit was exceeded.
    ERR_TIMEOUT,                          // A bounded GPU or synchronization wait expired.
    ERR_INTERNAL_ERROR,                   // Internal renderer invariant failed.

    // Backend and graphics-device startup: 0x01xx.
    ERR_BACKEND_NOT_BUILT = 0x0100u,      // Requested backend was excluded from this build.
    ERR_BACKEND_UNAVAILABLE,              // Backend cannot run on the current machine.
    ERR_API_VERSION_MISMATCH,             // Host and renderer contracts are incompatible.
    ERR_BACKEND_VERSION_UNSUPPORTED,      // Required OpenGL/Vulkan version is unavailable.
    ERR_CAPABILITY_MISSING,               // Required hardware or driver feature is absent.
    ERR_WINDOW_INCOMPATIBLE,              // Native window attributes reject this backend.
    ERR_CONTEXT_CREATE_FAILED,            // Graphics context or device creation failed.
    ERR_CONTEXT_ACTIVATE_FAILED,          // Context could not be made current.
    ERR_ENTRYPOINT_LOAD_FAILED,           // Required graphics entry points are missing.
    ERR_DEVICE_QUERY_FAILED,              // Device limits or capabilities could not be read.
    ERR_DEVICE_LOST,                      // Graphics device/context became unusable.

    // Frame and presentation lifecycle: 0x02xx.
    ERR_FRAME_ALREADY_ACTIVE = 0x0200u,   // BeginFrame was called inside an active frame.
    ERR_FRAME_NOT_ACTIVE,                 // Frame-only operation occurred outside a frame.
    ERR_RENDER_TARGET_INVALID,            // Render-target attachment set is invalid.
    ERR_FRAMEBUFFER_INCOMPLETE,            // Backend rejected the framebuffer configuration.
    ERR_RESIZE_FAILED,                    // Presentation storage could not be resized.
    ERR_PRESENT_FAILED,                   // Completed image could not be presented.
    ERR_PRESENT_TARGET_LOST,              // Window surface or presentation target was lost.
    ERR_PRESENT_TARGET_OUT_OF_DATE,       // Presentation target must be recreated.

    // GPU resource ownership and transfer: 0x03xx.
    ERR_INVALID_HANDLE = 0x0300u,         // Handle does not identify a renderer object.
    ERR_STALE_HANDLE,                     // Handle generation no longer matches its slot.
    ERR_RESOURCE_TYPE_MISMATCH,           // Handle refers to the wrong object type.
    ERR_RESOURCE_NOT_READY,               // Object exists but cannot yet be consumed.
    ERR_RESOURCE_BUSY,                    // In-flight GPU work prevents the operation.
    ERR_RESOURCE_CREATE_FAILED,           // Backend object creation failed.
    ERR_RESOURCE_TRANSFER_FAILED,         // Upload, download, mapping, or copy failed.
    ERR_RESOURCE_FORMAT_UNSUPPORTED,      // Requested pixel or buffer format is unsupported.
    ERR_RESOURCE_SIZE_UNSUPPORTED,        // Dimensions or allocation size exceed limits.

    // Shaders, bindings, and pipelines: 0x04xx.
    ERR_SHADER_DATA_INVALID = 0x0400u,    // Cooked shader data is corrupt or incompatible.
    ERR_SHADER_STAGE_UNSUPPORTED,         // Backend cannot execute the requested stage.
    ERR_SHADER_COMPILE_FAILED,            // Native driver rejected generated shader code.
    ERR_SHADER_LINK_FAILED,               // Shader stages could not form a complete program.
    ERR_SHADER_INTERFACE_MISMATCH,        // Stage inputs, outputs, or resources disagree.
    ERR_PIPELINE_DESC_INVALID,            // Pipeline state contains contradictory values.
    ERR_PIPELINE_CREATE_FAILED,           // Backend pipeline/program creation failed.
    ERR_BINDING_MISMATCH,                 // Bound resource disagrees with shader reflection.

    // Geometry, commands, and submission: 0x05xx.
    ERR_VERTEX_LAYOUT_INVALID = 0x0500u,  // Vertex layout disagrees with supplied data.
    ERR_INDEX_DATA_INVALID,               // Index type, range, or storage is invalid.
    ERR_DRAW_DESC_INVALID,                // Draw parameters cannot describe a legal draw.
    ERR_COMMAND_STATE_INVALID,            // Command order violates renderer state rules.
    ERR_COMMAND_CAPACITY_EXCEEDED,        // Fixed command storage cannot accept more work.
    ERR_SUBMISSION_FAILED,                // Recorded work could not be submitted.
    ERR_SYNCHRONIZATION_FAILED,           // Fence, barrier, or ownership operation failed.

    // Scene rendering and diagnostics: 0x06xx.
    ERR_VIEW_INVALID = 0x0600u,           // View, camera, or viewport state is invalid.
    ERR_LIGHT_LIMIT_EXCEEDED,             // Active-light capacity was exceeded.
    ERR_SHADOW_STORAGE_EXHAUSTED,         // Shadow atlas cannot accept another allocation.
    ERR_VISIBILITY_DATA_INVALID,          // Culling or visibility input is inconsistent.
    ERR_CAPTURE_UNSUPPORTED,              // Backend cannot perform the requested capture.
    ERR_CAPTURE_FAILED,                   // Screenshot, readback, or frame capture failed.

    INVALID = 0xFFFFu                     // Corrupt or unavailable renderer result.
};

CYPHER_NODISCARD constexpr bool R_ErrorSucceeded( render_error_t error ) noexcept
{
    return error == render_error_t::OK;
}

CYPHER_NODISCARD constexpr bool R_ErrorFailed( render_error_t error ) noexcept
{
    return !R_ErrorSucceeded( error );
}

CYPHER_NODISCARD constexpr ::cypher::common::error_code_t
R_ErrorCode( render_error_t error ) noexcept
{
    return ::cypher::common::Cy_ErrorMake(
        ::cypher::common::error_domain_t::RENDER,
        static_cast<::cypher::common::u16>( error ) );
}

CYPHER_NODISCARD CY_RETURNS_NONNULL const char *R_ErrorName(
    render_error_t error ) noexcept;

CYPHER_NODISCARD CY_RETURNS_NONNULL const char *R_ErrorDescription(
    render_error_t error ) noexcept;

CYPHER_NODISCARD CY_RETURNS_NONNULL
const ::cypher::common::error_table_t *R_ErrorTable() noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_ERROR_H
