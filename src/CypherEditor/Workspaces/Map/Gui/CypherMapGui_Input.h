//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Input.h
//  Purpose: Routes focused viewport keys through the editor keymap and
//           command registry, preserving tool and view context precedence.
//  Details: The current viewport dispatcher handles single-stroke bindings.
//           Window QAction shortcuts retain their existing chord sequences.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_MAP_GUI_INPUT_H
#define CYPHER_MAP_GUI_INPUT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier0/CypherCommon_BaseTypes.h"

#include <QStringList>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace cypher::editor::map
{

struct map_workspace_t;

enum map_navigation_key_flags_t : common::u32 {
    MAP_NAVIGATION_FORWARD = 1u << 0u,
    MAP_NAVIGATION_BACK = 1u << 1u,
    MAP_NAVIGATION_LEFT = 1u << 2u,
    MAP_NAVIGATION_RIGHT = 1u << 3u,
    MAP_NAVIGATION_UP = 1u << 4u,
    MAP_NAVIGATION_DOWN = 1u << 5u,
    MAP_NAVIGATION_FAST = 1u << 6u,
    MAP_NAVIGATION_SLOW = 1u << 7u
};

enum class map_tool_gesture_key_t : common::u8 { NONE = 0u, CONFIRM, CANCEL };

enum class map_camera_gesture_t : common::u8 { NONE = 0u, LOOK, ORBIT, PAN, DOLLY };

// Resolve a pressed button into its configured camera drag. A view retains
// this result until release, so changing modifiers cannot change the drag.
// Only Space is currently tracked as a non-modifier mouse chord key.
// Context priority is active tool, selection mode, 3D viewport, viewport, map, global;
// within one context the most constrained matching gesture wins. Ctrl/Meta
// must be explicitly bound, while Shift/Alt can accompany basic gestures.
map_camera_gesture_t MapInput_CameraDragGesture( const map_workspace_t *pWorkspace,
                                                const QMouseEvent *pEvent, bool spaceHeld );

// True for an effective dolly binding matching a nonzero vertical wheel.
// Horizontal-only and unsupported held-key wheel triggers are omitted.
bool MapInput_CameraWheelGesture( const map_workspace_t *pWorkspace, const QWheelEvent *pEvent );

// Canonical effective camera triggers for help. Unsupported held keys,
// clicks, horizontal wheels, shadowed triggers and explicit unbindings are
// never advertised. Defaults apply only when there is no keymap chain.
QStringList MapInput_CameraGestureBindings( const map_workspace_t *pWorkspace, map_camera_gesture_t gesture );
QStringList MapInput_NavigationBindings( const map_workspace_t *pWorkspace, common::u32 flags );

// Logical confirm/cancel for an active viewport gesture. This has no side
// effects and uses the same contextual lookup and explicit-unbinding shadows
// as command dispatch. The view owns gesture eligibility and publication;
// these logical bindings do not enable an unavailable registry command.
map_tool_gesture_key_t MapInput_ToolGestureKey( const map_workspace_t *pWorkspace,
                                             const QKeyEvent *pEvent, bool camera );

// Effective canonical one-stroke keys for gesture help in one viewport
// family. Candidates are filtered through the same contextual resolver as
// input, so remapped, explicitly unbound and shadowed keys are not advertised.
// Returns sorted, unique texts; unavailable multi-stroke sequences are omitted.
QStringList MapInput_ToolGestureBindings( const map_workspace_t *pWorkspace,
                                        map_tool_gesture_key_t gesture, bool camera );

// Effective canonical one-stroke keys for any command in the active input
// contexts. Filters through dispatch lookup, including selection mode,
// platform/user remaps, explicit unbindings and higher-priority shadows.
// Help can describe a disabled command's binding without enabling it.
// Returns sorted, unique texts; unsupported multi-stroke chords are omitted.
QStringList MapInput_CommandBindings( const map_workspace_t *pWorkspace, const char *pCommand, bool camera );

// True when this context consumes the key. execute=false is a side-effect-
// free ShortcutOverride preflight; execute=true runs enabled commands via
// the same registry path used by menus and the console. A disabled/unknown
// binding and an explicitly unbound inherited chord reserve the key without
// executing, so a less-specific window shortcut cannot capture it. The stack is active
// tool, selection mode, viewport family, viewport, map, global. Modifier/platform overlays
// and explicit user unbindings are resolved by the framework keymap.
bool MapInput_DispatchKey( map_workspace_t *pWorkspace, QKeyEvent *pEvent,
                          bool camera, bool navigationActive, bool execute, bool navigationOwned = false );

// Held camera actions have priority while navigating. A view should accept
// ShortcutOverride for these keys, then let its camera keyPress/keyRelease
// handlers consume them. This prevents window shortcuts such as E=Scale
// from capturing E=CameraUp during flight. Uses the user's held bindings.
// navigationOwned means a captured camera gesture or previously held movement
// owns input. Otherwise an explicit modified tool chord may leave idle neutral
// navigation. Determine ownership before observing the current press, using the
// same state for ShortcutOverride and KeyPress.
bool MapInput_IsNavigationKey( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent,
                              bool navigationOwned = false );

// All camera actions whose user-held triggers match this event, or zero.
// Ctrl/Meta only match explicitly declared held chords. Shift/Alt speed
// modifiers may accompany a movement key. A camera should remember the
// returned mask at key press so release still clears it if modifiers change.
common::u32 MapInput_NavigationKeyMask( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent );

} // namespace cypher::editor::map

#endif // CYPHER_MAP_GUI_INPUT_H
