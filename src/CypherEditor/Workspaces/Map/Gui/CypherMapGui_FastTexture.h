//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FastTexture.h
//  Purpose: Declares Hammer 5's Fast Texture tool window: the selected
//           faces' material shown whole over a grid; drag a rectangle on it
//           (or click a grid cell) and the faces' texture coordinates fit
//           into that part of the texture.
//  Details: Modes follow Hammer: Use Existing keeps the faces' layout,
//           Unwrap Planar projects them flat first, Unwrap Square fits each
//           face on its own. Flips mirror the fit; the inset (in texture
//           pixels) keeps the fit off the rectangle's edges, for trims.
//           Every fit is one undo step.
//
//  History:
//  - Created by Karlo Siric on 2026-10-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_FAST_TEXTURE_H
#define CYPHER_EDITOR_MAP_GUI_FAST_TEXTURE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QRectF>
#include <QString>

class QDialog;
class QWidget;

namespace cypher::editor::map
{

// Shows the workspace's Fast Texture window (one per workspace), raised.
CYPHER_NODISCARD QDialog *MapFastTexture_Show( map_workspace_t *pWorkspace, QWidget *pParent ) noexcept;

// Fits the faces as choosing `uv` (0..1 across the texture, V down) on the
// texture would; false when nothing changed.
CYPHER_NODISCARD bool MapFastTexture_Choose( QDialog *pDialog, QRectF uv );
// The material the window shows (empty when no faces are selected), and
// how many hotspot regions it has.
CYPHER_NODISCARD QString MapFastTexture_Material( QDialog *pDialog );
CYPHER_NODISCARD common::usize MapFastTexture_HotspotCount( QDialog *pDialog );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_FAST_TEXTURE_H
