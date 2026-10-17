//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_UvEditor.h
//  Purpose: Declares the UV Editor panel (TrenchBroom's face inspector UV
//           view, Hammer 5's Fast Texture UV widget): the selected faces
//           drawn over their material in texture space, edited by hand.
//  Details: Drag the faces to move their texture, the ring around the pivot
//           to turn it, a square handle to scale it along U or V (Shift:
//           both), or Alt and a square handle to shear it; drag the cross
//           to move the pivot. Moves snap to whole texels, turns to 15
//           degrees, scales to sixteenths; Ctrl drags freely. The wheel
//           zooms, the middle button pans. Each drag is one undo step for
//           every selected face; the faces follow the drag as a preview.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_UV_EDITOR_H
#define CYPHER_EDITOR_MAP_GUI_UV_EDITOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"
#include "CypherMap_TextureEdit.h"

#include <vector>

#include <QPointF>
#include <QString>

class QWidget;

namespace cypher::editor::map
{

inline constexpr const char *MAP_PANEL_UV_EDITOR = "map.uv_editor";

CYPHER_NODISCARD QWidget *MapUvEditor_Create( QWidget *pParent, map_workspace_t *pWorkspace );

// The faces the panel shows: the picked brush side or mesh faces in Faces
// mode (none otherwise).
CYPHER_NODISCARD std::vector<map_surface_ref_t> MapUvEditor_Surfaces( const map_workspace_t *pWorkspace );

// For tests and scripting: the panel's canvas, its view (texels at the
// centre, screen pixels per texel), and where a texture-space point is on
// screen.
CYPHER_NODISCARD QWidget *MapUvEditor_Canvas( QWidget *pPanel );
CYPHER_NODISCARD QPointF MapUvEditor_ToScreen( QWidget *pPanel, math::vec2d_t uv );
CYPHER_NODISCARD math::vec2d_t MapUvEditor_Pivot( QWidget *pPanel );
CYPHER_NODISCARD QString MapUvEditor_Status( QWidget *pPanel );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_UV_EDITOR_H
