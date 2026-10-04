//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Typed authoring dialogs for the entity keys shown in Object Properties.
//////////////////////////////////////////////////////////////////////////
#ifndef CYPHER_MAP_GUI_ENTITY_PROPERTIES_H
#define CYPHER_MAP_GUI_ENTITY_PROPERTIES_H
#pragma once

#include "CypherMapGui_Workspace.h"

class QWidget;

namespace cypher::editor::map
{
enum class map_entity_property_action_t { ADD, EDIT, RENAME, IDENTITY };

// Selection is resolved to owning entities. A dialog opened for one selection
// cannot commit to another selection or a subsequently replaced document.
// Strings are plain text; structured values use bounded, duplicate-free CYKV.
bool MapEntityPropertyDialog_Show( QWidget *parent, map_workspace_t *workspace,
    map_entity_property_action_t action, common::string_view_t key = {}, QString *committedKey = nullptr );

// Bounded serialization for mixed-value display and structured-value editing.
// Binary and oversized values remain inspectable without being silently truncated.
QString MapEntityProperty_ValueText( const common::key_value_t *value, bool *editable = nullptr );
bool MapEntityProperty_CanEditValue( const common::key_value_t *value );
const char *MapEntityProperty_TypeName( common::key_value_type_t type ) noexcept;
} // namespace cypher::editor::map
#endif
