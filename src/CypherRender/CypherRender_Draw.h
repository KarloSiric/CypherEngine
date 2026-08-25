//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Draw.h
//  Purpose: Declares the CypherRender Render Draw module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Draw Contract

Draw commands validate borrowed geometry and material references before submission. Backend
execution owns no caller memory and must not retain transient command pointers past their
documented lifetime.
================
*/

#ifndef CYPHER_ENGINE_RENDER_DRAW_H
#define CYPHER_ENGINE_RENDER_DRAW_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Matrix4.h"
#include "CypherRender_Camera.h"
#include "CypherRender_Mesh.h"
#include "CypherRender_Shader.h"

namespace cypher::engine::render
{

constexpr common::u32 CYPHER_RENDER_DRAW_ITEMS_LIST_MAX = 16384u;

struct draw_item_t {
    ::cypher::math::mat4_t modelMatrix{ ::cypher::math::CY_MAT4_IDENTITY }; // Object-to-world transform.

    mesh_t    *mesh{};                                      // Borrowed geometry; must outlive list execution.
    shader_t  *shader{};                                    // Borrowed program; must outlive list execution.
};

struct draw_list_t {
    draw_item_t *items{ nullptr };                          // Caller-owned contiguous command storage.
    common::u32 nItemCount{ 0u };                           // Submitted commands in the active prefix.
    common::u32 nItemCapacity{ 0u };                        // Maximum commands addressable through items.
};

render_error_t R_DrawItem( const draw_item_t &item, const camera_t &camera );

void R_DrawListInit( draw_list_t &drawList, draw_item_t *items, common::u32 nItemCapacity );

void R_DrawListClear( draw_list_t &drawList );

render_error_t R_DrawListSubmit( draw_list_t &drawList, const draw_item_t &item );

render_error_t R_DrawListDraw( const draw_list_t &drawList, const camera_t &camera );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_DRAW_H
