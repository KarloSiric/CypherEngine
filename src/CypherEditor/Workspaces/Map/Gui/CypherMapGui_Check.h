//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Check.h
//  Purpose: Declares Check for Problems: what can be found wrong with the
//           open map without compiling it, listed with the object involved.
//  Details: Hammer's Check for Problems (Alt+P) and TrenchBroom's issue
//           browser. The checks read the session's wireframe and document:
//           load problems, outputs whose target does not exist or that
//           cannot be read, entities without a class, a missing player
//           start, stacked duplicate entities, brushes that could not be
//           rebuilt, objects far outside the world, and materials the asset
//           catalogue does not have. Choosing an issue selects and frames
//           its object. Game definitions (.cygame) will add class-specific
//           checks; until then only what holds for every game is checked.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_CHECK_H
#define CYPHER_EDITOR_MAP_GUI_CHECK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <QString>
#include <QVector>

class QDialog;
class QWidget;

namespace cypher::editor::map
{

inline constexpr common::f64 MAP_CHECK_WORLD_EXTENT = 131072.0; // Units from the origin before an object counts as lost.

enum class map_issue_severity_t : common::u8 { INFO = 0u, WARNING, ERROR };

struct map_issue_t {
    map_issue_severity_t severity{ map_issue_severity_t::WARNING };
    common::u64 id{ 0u };  // The object involved; 0 when the issue is map-wide.
    QString kind{};        // "missing_target", "no_player_start", ...: stable, for filters and tests.
    QString message{};
};

// Whether a virtual asset path exists (the asset browser's catalogue).
// Without one, material checks are skipped.
using map_asset_exists_fn = bool ( * )( void *pContext, const QString &path );

// Errors first, then warnings, then information; by object ID within each.
CYPHER_NODISCARD QVector<map_issue_t> MapCheck_Run( const map_workspace_t *pWorkspace, map_asset_exists_fn pfnAssetExists = nullptr,
                                                    void *pAssetContext = nullptr );
CYPHER_NODISCARD const char *MapCheck_SeverityName( map_issue_severity_t severity ) noexcept;

CYPHER_NODISCARD QDialog *MapCheckDialog_Create( QWidget *pParent, map_workspace_t *pWorkspace, map_asset_exists_fn pfnAssetExists = nullptr,
                                                 void *pAssetContext = nullptr );
// Runs the checks again (the map may have changed). Returns the issue count.
int MapCheckDialog_Refresh( QDialog *pDialog );
// Selects and frames the issue's object; false for map-wide issues.
bool MapCheckDialog_GoTo( QDialog *pDialog, int iIssue );

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_CHECK_H
