<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/README.md
//  Purpose: Documents the CypherCommon RenderSystem folder.
//  Details: RenderSystem contains public render interfaces, descriptors, flags, and
//           backend-neutral types shared by runtime, tools, and editor.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# RenderSystem

`RenderSystem` is for backend-neutral rendering contracts.

OpenGL, Vulkan, shader compilation, render graph, and GPU resource implementation
belong in renderer modules outside Common. Image, texture, and material contracts
are grouped below this folder because they share the rendering data boundary.

`CypherCommon_RenderPreview.h` defines a synchronous preview service shared by
Picasso, Mason, tests, and renderer hosts. It accepts a retained runtime resource
handle or a borrowed cooked in-memory resource and writes RGBA8 sRGB pixels into
caller-owned storage. The contract contains no Qt or graphics-backend types.
