// CypherEngine Source Code. Copyright (c) 2026 Karlo Siric. All rights reserved.
// Mason's local UI session. Map content and editor preferences are stored separately.
#pragma once

#include <QString>

class QMainWindow;
class QWidget;
namespace ads { class CDockManager; }

namespace cypher::mason
{
enum class workspace_state_result_t { OK, NOT_FOUND, INVALID, IO_ERROR };

// Atomic replacement on save. Restore preflights every state blob before live
// mutation, and rolls back if a component still cannot restore. Views apply
// last and validate before changing pane contents. Dock XML must contain a
// complete, unique set of the current dock IDs. Restore also accepts the retired
// assets ID, removed together with its saved splitter size. Other incompatible
// sessions use the current layout instead of silently hiding newly registered panels.
workspace_state_result_t MasonWorkspace_Save( QMainWindow *pWindow, ads::CDockManager *pDocks,
                                              QWidget *pViews, const QString &path );
workspace_state_result_t MasonWorkspace_Restore( QMainWindow *pWindow, ads::CDockManager *pDocks,
                                                 QWidget *pViews, const QString &path );

// Console belongs to Object Properties' tab group. Startup/reset collapse it;
// explicit session restore may preserve its saved open state. Other panels and
// their placement are retained. False means the required dock area is missing.
bool MasonWorkspace_ApplyPanelPolicy( ads::CDockManager *pDocks, bool bCollapseConsole );
}
