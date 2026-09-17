//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Shader.h
//  Purpose: Declares the backend-neutral runtime shader contract.
//  Details: The contract creates live renderer programs from validated
//           cooked shader views. Native OpenGL shader and program names remain
//           private to the OpenGL backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_SHADER_H
#define CYPHER_ENGINE_RENDER_SHADER_H
#pragma once

#include "CypherCommon/Formats/CypherCommon_CookedShader.h"
#include "CypherRender_Error.h"
#include "CypherRender_Types.h"

/*
===============================================================================

    Runtime shader programs

A runtime shader represents one complete program assembled from the stages in
a validated cooked shader resource. The frontend owns the public handle and
copied metadata; the backend owns the corresponding native program.

The first implementation accepts the existing validated CYSH graphics program
containing one vertex stage and one fragment stage. It does not accept authored
CYKV or raw files directly; resource loading and cooking stay outside Renderer.

All shader operations execute on the renderer thread. Validation, creation,
destruction, and information queries require an initialized renderer and shader
system. R_IsShaderValid returns false while the shader system is unavailable.

===============================================================================
*/

namespace cypher::engine::render
{

using render_shader_handle_t = render_handle_t; // Packed index, generation, and object type.

inline constexpr render_shader_handle_t R_INVALID_SHADER{};

/*
================
Shader descriptor

The caller keeps cookedShader, all stage bytes referenced by that view, and
the optional debugName alive and unchanged throughout validation and creation.

Creation is synchronous. The renderer and backend must not retain borrowed
pointers after the call returns. Persistent metadata is copied by value.
================
*/

struct render_shader_desc_t {
    const ::cypher::common::cooked_shader_view_t *cookedShader{
        nullptr }; // Validated, immutable view into a loaded CYSH resource.
    const char *debugName{
        nullptr }; // Optional borrowed, NUL-terminated UTF-8 diagnostic label.
};

/*
================
Shader information

This backend-neutral snapshot describes the cooked program represented by a
live renderer handle. Native GLuint, VkShaderModule, and driver pointers are
never exposed through this record.
================
*/

struct render_shader_info_t {
    ::cypher::common::render_shader_backend_t cookedBackend{
        ::cypher::common::render_shader_backend_t::OPENGL };
    ::cypher::common::render_shader_program_kind_t kind{
        ::cypher::common::render_shader_program_kind_t::GRAPHICS };
    ::cypher::common::render_shader_language_profile_t languageProfile{
        ::cypher::common::render_shader_language_profile_t::GLSL_CORE };

    ::cypher::common::u32 languageVersion{ 0u };
    ::cypher::common::flags32_t flags{ 0u };
    ::cypher::common::content_hash_t sourceHash{};
    ::cypher::common::u32 stageCount{ 0u };
};

// Validates cooked metadata, stage spans, and active-backend compatibility.
// Creates no native objects; successful validation does not guarantee compilation.
CYPHER_NODISCARD render_error_t R_ValidateShaderDesc(
    const render_shader_desc_t &description ) noexcept;

// Creates one frontend record and one complete native shader program.
// Rejects null shaderOut; every failure with a non-null output writes R_INVALID_SHADER.
// Frontend registration failure releases any program already created by the backend.
CYPHER_NODISCARD render_error_t R_CreateShader(
    const render_shader_desc_t &description,
    CY_OUT render_shader_handle_t *shaderOut ) noexcept;

// Rejects live pipeline references with ERR_RESOURCE_BUSY, leaving the shader live.
// On success, releases the native program and invalidates its public table entry.
CYPHER_NODISCARD render_error_t R_DestroyShader(
    render_shader_handle_t shader ) noexcept;

// Verifies object type, slot index, generation, and current live state.
CYPHER_NODISCARD bool R_IsShaderValid(
    render_shader_handle_t shader ) noexcept;

// Returns backend-neutral metadata copied during shader creation.
// Rejects null infoOut; other failures reset it to a default-initialized snapshot.
CYPHER_NODISCARD render_error_t R_GetShaderInfo(
    render_shader_handle_t shader,
    CY_OUT render_shader_info_t *infoOut ) noexcept;

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_SHADER_H
