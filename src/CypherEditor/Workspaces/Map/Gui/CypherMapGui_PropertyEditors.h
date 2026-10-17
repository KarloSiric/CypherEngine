//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_PropertyEditors.h
//  Purpose: Declares the typed value editors Object Properties shows for an
//           entity class the game profile defines (CYGAME.md 5): a checkbox
//           for bool, a dropdown of choices, number fields with their limits
//           and unit, a colour swatch, three fields for vectors and angles,
//           asset and target pickers, and the class dropdown.
//  Details: An editor shows the entities' value; the class default when
//           none of them has one; or an empty "Multiple values" state when
//           they differ. It commits a whole new value through its callback
//           once the user has finished (Enter, focus leaving, a pick, a menu
//           closing), never per keystroke, and only when the value changed,
//           so one change is one undo step.
//
//           A commit rebuilds Object Properties, which deletes the editor,
//           so the callback must not apply the value synchronously: the
//           panel queues it to the next event-loop turn.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_PROPERTY_EDITORS_H
#define CYPHER_EDITOR_MAP_GUI_PROPERTY_EDITORS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"
#include "CypherMap_GameProfile.h"

#include <functional>
#include <memory>

#include <QString>

class QWidget;

namespace cypher::editor::map
{

// A value in its own document; the root is the value.
using map_property_value_t = std::shared_ptr<common::key_value_document_t>;
using map_property_commit_fn = std::function<void( const map_property_value_t &value )>;

struct map_property_editor_args_t {
    map_workspace_t *pWorkspace{ nullptr };
    const map_game_property_t *pProperty{ nullptr };
    const common::key_value_t *pValue{ nullptr }; // The entities' value; null when none has one (the default shows).
    bool bMixed{ false };                         // The selected entities disagree.
    map_property_commit_fn commit{};
};

// The editor for the property's type; null for the types edited only in the
// value dialog (objects, faces). Read-only properties get a disabled editor.
CYPHER_NODISCARD QWidget *MapPropertyEditor_Create( QWidget *pParent, const map_property_editor_args_t &args );

// The class field: the profile's placeable classes of the entity's kind,
// picked from the list or typed (a class the profile does not know is kept).
CYPHER_NODISCARD QWidget *MapClassEditor_Create( QWidget *pParent, map_workspace_t *pWorkspace, const QString &className, bool bMixed,
    std::function<void( const QString &className )> commit );

// Builds values the way the editors do (tests use them to compare).
CYPHER_NODISCARD map_property_value_t MapPropertyValue_Bool( bool value );
CYPHER_NODISCARD map_property_value_t MapPropertyValue_Integer( common::i64 value );
CYPHER_NODISCARD map_property_value_t MapPropertyValue_Real( common::f64 value );
CYPHER_NODISCARD map_property_value_t MapPropertyValue_String( const QString &value );

// The number a value holds, whatever its numeric type.
CYPHER_NODISCARD bool MapPropertyValue_Number( const common::key_value_t *pValue, common::f64 *pOut ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_PROPERTY_EDITORS_H
