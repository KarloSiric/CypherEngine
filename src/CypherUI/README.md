<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherUI/README.md
//  Purpose: Defines the ownership and first implementation gate of CypherUI.
//  Details: Separates shipping HUD/menu behavior from Qt authoring tools,
//           diagnostic overlays, font services, and native renderer execution.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherUI

`CypherUI` is the planned game-runtime interface system for HUDs, menus,
overlays, prompts, captions, and accessible input navigation.

The name is `CypherUI`. `CypherGUI` is not a second subsystem name.

## Ownership

CypherUI will own:

- document/widget identity and bounded lifetime;
- deterministic layout and clipping;
- style and theme resolution;
- pointer hit testing, capture, focus, and keyboard/controller navigation;
- UI state and time-based animation values;
- localization and data-binding inputs;
- DPI, safe-area, aspect-ratio, and accessibility policy;
- renderer-neutral rectangle, image, and text draw lists;
- UI diagnostics, inspection, and reload state.

CypherUI will not own:

- platform event collection (`CypherSystem`) or action mapping (`CypherInput`);
- font faces, shaping, fallback, or glyph caches (`CypherFont`);
- native textures, buffers, shaders, draw calls, or presentation
  (`CypherRender`);
- game rules (`CypherGame`);
- Qt widgets used by Picasso, TileEditor, or Mason;
- Dear ImGui debug tools.

## First Implementation Gate

This directory is a scaffold. It becomes an implemented `Cypher::UI` target
only when a complete runtime screen exists:

1. one HUD or focusable menu document;
2. bounded tree/storage and stable element IDs;
3. rectangle layout with minimum, preferred, maximum, padding, margin, and
   clipping behavior;
4. pointer, keyboard, and controller navigation;
5. style resolution and explicit state transitions;
6. text through `CypherFont`;
7. image resources through `CypherResource`;
8. immutable, bounded draw data consumed by `CypherRender`;
9. tests for layout, hit testing, focus, navigation, clipping, DPI, safe areas,
   malformed documents, capacity exhaustion, and hot reload.

The developer console will become one CypherUI surface over the independent
Command, CVar, and Log services. It does not justify a duplicate console
backend.

See [ADR 0006](../../docs/adr/0006-runtime-subsystem-structure.md) and the
[CryEngine 1 subsystem research](../../docs/cryengine1_subsystem_research.md).
