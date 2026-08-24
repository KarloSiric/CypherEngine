<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/picasso_ui_workflow_reference.md
//  Purpose: Records the researched interaction and visual contract for Picasso.
//  Details: Separates durable authoring-workflow decisions from individual Qt
//           widget implementations and screenshot-specific decoration.
//
//  History:
//  - Created by Karlo Siric on 2026-08-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Picasso UI And Workflow Reference

## Scope

Picasso is a texture and material authoring tool. It adopts Hammer's compact
desktop-tool discipline, Photoshop's direct image-editing interactions, and
Substance's layer-driven material workflow. It must not reproduce Hammer's map
editing layout blindly or grow into a general-purpose Photoshop replacement.

This document is the UI contract used to review the Qt implementation. The
supplied Hammer screenshots are the primary visual reference. Product behavior
comes from the documented workflows below.

## Reference Findings

### Hammer 5.x

Hammer presents tools as a dense workstation rather than a decorative desktop
application:

- Tool buttons occupy compact, dark, beveled cells with one-pixel separators.
- Glyphs are filled or strongly weighted pictograms, not bright wireframe icons.
- Idle glyphs are dark steel with sparse blue, cyan, amber, or green details.
- Hover raises contrast slightly; it does not flood the button with color.
- The selected mode receives a thin warm-orange frame and a darker amber face.
- The top strip carries global modes and operations. The permanent left rail
  carries the active editing tools.
- Docks meet directly with narrow splitters. Headers, rows, and inputs share a
  consistent vertical rhythm.
- Tool properties are contextual, while history, selection, assets, and object
  properties remain persistent workspace surfaces.
- The bottom status bar reports active context instead of showing decoration.

Valve's toolbar documentation also establishes that operation, view, selection,
and texture toolbars are dockable workspace elements, while the status bar has a
stable bottom role: [Hammer toolbar reference](https://developer.valvesoftware.com/wiki/Category:Hammer_Toolbars).

### Photoshop

Photoshop separates tool selection from tool configuration:

- One grouped tool panel provides the permanent editing vocabulary.
- The options bar changes with the selected tool.
- Related or uncommon tools may live in groups rather than consuming permanent
  top-toolbar space.
- Holding Space temporarily activates the Hand tool and releasing it restores
  the previous tool. This is a modifier, not a persistent tool change.
- Tooltips identify both the tool and shortcut.
- Tab hides panels to maximize canvas space, while workspace arrangements remain
  recoverable.

References:

- [Photoshop workspace overview](https://helpx.adobe.com/photoshop/desktop/get-started/learn-the-basics/workspace-overview.html)
- [Photoshop toolbar customization](https://helpx.adobe.com/photoshop/desktop/get-started/set-up-toolbars-panels/customize-the-toolbar.html)
- [Photoshop temporary Hand tool](https://helpx.adobe.com/photoshop/using/tool-techniques/hand-tool.html)
- [Photoshop panel visibility](https://helpx.adobe.com/photoshop/desktop/get-started/learn-the-basics/hide-show-panels.html)

### Substance Painter And Sampler

Texture and material work revolves around a coupled stack:

- The central 2D/3D viewport remains the largest region.
- The layer stack is authoritative for composition order, visibility, blending,
  opacity, masks, groups, and effects.
- The Properties panel changes with the selected layer, filter, generator, or
  active tool.
- Assets are searchable and can be dragged into the viewport or layer stack.
- Texture channels and texture sets are explicit; they are not hidden inside a
  generic image-properties panel.
- Closed panels remain accessible through sidebars, and the default workspace
  can be restored.

References:

- [Substance Painter interface overview](https://helpx.adobe.com/substance-3d-painter/using/interface-overview.html)
- [Substance Sampler interface overview](https://helpx.adobe.com/au/substance-3d-sampler/using/interface-overview.html)
- [Substance Sampler layers panel](https://experienceleague.adobe.com/en/docs/substance-3d-sampler/using/interface/panels/layers-panel)

### Qt Workspace Mechanics

`QMainWindow`, `QToolBar`, and `QDockWidget` are sufficient for the intended
desktop behavior. Every toolbar and dock needs a stable, unique object name so
`saveState()` and `restoreState()` can persist the workspace correctly. The
content widget supplies size hints; the dock wrapper must not be hard-sized.

References:

- [Qt QMainWindow](https://doc.qt.io/qt-6/qmainwindow.html)
- [Qt QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html)

## Picasso Workspace Contract

The default Texture workspace is:

```text
Application menu
Global commands: New Open Save Compile | Undo Redo | Texture Material | 2D 3D
Context options: active tool name and only that tool's editable parameters

Tool rail | Tool/asset panel | 2D canvas | Layers and Channels
          |                  |           | Contextual Properties
          |                  |           | Material Preview / History

Asset browser or Console tabs
Status bar
```

### Tool Rail

The left rail is the single persistent tool selector. Tools do not appear again
in the top options strip.

Groups, from top to bottom:

1. Select, rectangular selection, lasso, and move.
2. Brush, eraser, fill, gradient, eyedropper, and clone.
3. Crop, transform/seam, and mask.
4. Hand/pan, zoom, and pixel inspect.

The rail uses 34-pixel cells and 22-24-pixel filled pictograms. Group separators
are six pixels or less. Tooltips include the keyboard shortcut.

### Context Options

The context strip never repeats the entire tool set. It shows:

- active tool pictogram and name
- parameters applicable to that tool
- compact actions such as reset or preset selection

Brush exposes size, opacity, hardness, and blend mode. Pan exposes no fake brush
controls. Selection exposes selection mode and transform behavior. Controls that
the backend does not implement remain absent rather than visibly disabled.

### Input Contract

- Space held over the canvas temporarily enables pan.
- Releasing Space restores the cursor and leaves the selected tool unchanged.
- Middle-mouse drag always pans.
- Mouse wheel zooms around the pointer.
- `H` selects the persistent Hand tool; `Z` selects persistent Zoom.
- `Tab` will later hide and restore docks after the workspace state is stable.
- Escape cancels an in-progress tool operation once tool transactions exist.

### Dock Ownership

- **Layers/Channels:** composition, visibility, locks, masks, blend, opacity.
- **Properties:** active tool, selected layer, generator, filter, or material.
- **Assets:** generators, filters, presets, imported images, and project assets.
- **Preview:** texture channels or material geometry.
- **History:** document operations and the current undo cursor.
- **Console:** command execution, compilation output, and diagnostics.

These surfaces can be tabbed or moved, but their data ownership does not change.

## Visual Metrics

| Element | Contract |
| --- | --- |
| Menu height | 22-24 px |
| Global toolbar | 36-40 px |
| Context toolbar | 32-36 px |
| Tool rail | 40-42 px wide |
| Tool cell | 34 x 34 px |
| Tool pictogram | 22-24 px |
| Dock title | 22-24 px |
| Standard row | 24-28 px |
| Structural border | 1 px |
| Splitter | 2-3 px |
| Corner radius | 0-2 px |

## Color And Icon Contract

- Window and panels use neutral charcoal values between `#242424` and `#383838`.
- Idle button faces use `#2d2f30` with a dark lower/right edge.
- Idle glyphs use dark steel around `#707a7e`, with restrained family accents.
- Hover glyphs may rise to `#a4afb3`; hover borders use muted blue-gray.
- Active tool borders use warm orange around `#c57a32`.
- Active authored-object selection uses orange; keyboard focus uses blue-gray.
- Green is reserved for enabled/valid/success states.
- Cyan and bright blue are details, never the entire default tool rail.

Picasso must use its own licensed icon vocabulary. Valve artwork is a visual
reference and must not be copied from game binaries or screenshots.

## Review Order

1. Tool grouping, contextual behavior, and keyboard modifiers.
2. Icon silhouette, weight, state colors, and high-DPI rendering.
3. Dock ownership, default placement, tabbing, and alignment.
4. Layer and Properties interaction.
5. Asset workflow and drag/drop.
6. Preview, history, console, and status integration.

No later visual polish should bypass an earlier workflow defect.
