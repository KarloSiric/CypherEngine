<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/UI/README.md
//  Purpose: Documents the shared CypherCommon UI contract folder.
//  Details: UI contains renderer-neutral public IDs and descriptors only when
//           multiple runtime or tool-facing consumers need the same contract.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# UI

`UI` is reserved for public, renderer-neutral runtime UI contracts shared by
more than one subsystem.

Examples include stable UI/resource IDs, style values, layout descriptors,
input-navigation records, accessibility inputs, and immutable draw-command
descriptors.

Widget trees, layout execution, focus state, animation, and interaction belong
to `src/CypherUI`. Font faces, shaping, glyph metrics, and caches belong to
`src/CypherFont`. GPU execution belongs to `src/CypherRender`. Qt widgets remain
inside their authoring-tool product.

No contract is added here until an implemented consumer pair proves the shared
shape.
