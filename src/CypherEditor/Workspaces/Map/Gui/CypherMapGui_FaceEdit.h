//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_FaceEdit.h
//  Purpose: Declares the texture editor of Faces mode and the Texture tool
//           (Hammer's Face Edit / Modify Texture): material, scale, shift,
//           rotation, justify, align and flip for the picked faces.
//  Details: Every control runs a registered texture command (one undo step
//           each), on the picked brush face or mesh faces, or on every face
//           of the selected objects outside Faces mode. A single brush face
//           shows its current scale, shift and rotation; editing a value
//           sets it.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_FACE_EDIT_H
#define CYPHER_EDITOR_MAP_GUI_FACE_EDIT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

class QWidget;

namespace cypher::editor::map
{

// The texture editor for the workspace. Its host panel calls Refresh when the
// selection or document changes (the panel owns the workspace listener).
CYPHER_NODISCARD QWidget *MapFaceEdit_Create( QWidget *pParent, map_workspace_t *pWorkspace ) noexcept;
void MapFaceEdit_Refresh( QWidget *pEditor ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_FACE_EDIT_H
