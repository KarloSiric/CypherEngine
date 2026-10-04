//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Input.cpp
//  Purpose: Resolves viewport keys against the active tool, selection mode,
//           viewport, workspace, and global command contexts.
//  Details: ShortcutOverride uses the same lookup without executing it.
//           Held camera input remains with the camera while navigating.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Input.h"

#include "CypherMapGui_Workspace.h"
#include "CypherEditorGui_Actions.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QWheelEvent>

#include <bit>
#include <iterator>

namespace cypher::editor::map
{

using namespace cypher::common;

namespace
{

bool EventChord( const QKeyEvent &event, key_chord_t &chord )
{
    // Qt's conversion owns the platform modifier conventions and keypad
    // identity, exactly as it does for the corresponding QAction shortcut.
    return gui::EditorKeyChord_FromKeySequence(
        QKeySequence( QKeyCombination( event.modifiers(), static_cast<Qt::Key>( event.key() ) ) ), &chord );
}

bool HeldEventStroke( const QKeyEvent &event, key_stroke_t &stroke )
{
    key_chord_t chord{};
    if ( EventChord( event, chord ) && chord.nStrokes == 1u ) {
        stroke = chord.strokes[0];
        return true;
    }
    // A lone modifier is not a command chord, but it can hold fast/slow
    // navigation. Remove the modifier key's own flag for a like-for-like
    // comparison with EditorHeldKey_Parse("Shift"), for example.
    stroke = {};
    if ( event.modifiers().testFlag( Qt::ControlModifier ) ) { stroke.modifiers |= KEY_MODIFIER_CTRL; }
    if ( event.modifiers().testFlag( Qt::AltModifier ) ) { stroke.modifiers |= KEY_MODIFIER_ALT; }
    if ( event.modifiers().testFlag( Qt::ShiftModifier ) ) { stroke.modifiers |= KEY_MODIFIER_SHIFT; }
    if ( event.modifiers().testFlag( Qt::MetaModifier ) ) { stroke.modifiers |= KEY_MODIFIER_META; }
    switch ( event.key() ) {
        case Qt::Key_Control: stroke.key = KEY_MODIFIER_KEY_CTRL; stroke.modifiers &= ~KEY_MODIFIER_CTRL; return true;
        case Qt::Key_Alt: stroke.key = KEY_MODIFIER_KEY_ALT; stroke.modifiers &= ~KEY_MODIFIER_ALT; return true;
        case Qt::Key_Shift: stroke.key = KEY_MODIFIER_KEY_SHIFT; stroke.modifiers &= ~KEY_MODIFIER_SHIFT; return true;
        case Qt::Key_Meta: stroke.key = KEY_MODIFIER_KEY_META; stroke.modifiers &= ~KEY_MODIFIER_META; return true;
        default: return false;
    }
}

bool HeldMatches( const key_stroke_t &actual, const key_stroke_t &binding ) noexcept
{
    if ( actual.key != binding.key || ( actual.modifiers & binding.modifiers ) != binding.modifiers ) { return false; }
    // Speed modifiers may accompany a movement key. Ctrl/Command and Meta
    // document shortcuts remain usable unless explicitly bound for flight.
    constexpr u8 commandModifiers = KEY_MODIFIER_CTRL | KEY_MODIFIER_META;
    return ( actual.modifiers & commandModifiers ) == ( binding.modifiers & commandModifiers );
}

bool BindingHas( const keymap_binding_t &binding, const key_chord_t &chord ) noexcept
{
    for ( usize i = 0u; i < binding.nChords; ++i ) {
        if ( EditorKeyChord_Equals( binding.chords[i], chord ) ) { return true; }
    }
    return false;
}

const char *PlatformName( keymap_platform_t platform ) noexcept
{
    switch ( platform ) {
        case keymap_platform_t::MACOS: return "macos";
        case keymap_platform_t::WINDOWS: return "windows";
        case keymap_platform_t::LINUX: return "linux";
        default: return "";
    }
}

const key_value_t *BindingContext( const key_value_t *pRoot, string_view_t context, keymap_platform_t platform, bool overlay ) noexcept
{
    const key_value_t *pBase = pRoot;
    if ( overlay ) {
        pBase = KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( "platforms" ) ), StringView_FromCString( PlatformName( platform ) ) );
    }
    return KeyValue_Find( KeyValue_Find( pBase, StringView_FromCString( "bindings" ) ), context );
}

const key_value_t *TriggerContext( const key_value_t *pRoot, keymap_section_t section, string_view_t context,
                                   keymap_platform_t platform, bool overlay ) noexcept
{
    if ( overlay ) {
        pRoot = KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( "platforms" ) ), StringView_FromCString( PlatformName( platform ) ) );
    }
    return KeyValue_Find( KeyValue_Find( pRoot, StringView_FromCString( section == keymap_section_t::MOUSE ? "mouse" : "held" ) ), context );
}

bool InheritedTriggers( const gui::editor_gui_t &gui, keymap_section_t section, string_view_t context, string_view_t action,
                        usize source, keymap_triggers_t &triggers ) noexcept
{
    const auto platform = EditorKeymap_HostPlatform();
    for ( usize i = source; i < gui.nKeymapChain && i < EDITOR_KEYMAP_MAX_DEPTH; ++i ) {
        const auto status = EditorKeymap_FindTriggers( &gui.keymapChain[i], 1u, section, platform, context, action, &triggers );
        if ( status == keymap_lookup_t::BOUND ) { return true; }
        if ( status == keymap_lookup_t::UNBOUND && platform != keymap_platform_t::NONE &&
             EditorKeymap_FindTriggers( &gui.keymapChain[i], 1u, section, keymap_platform_t::NONE, context, action, &triggers ) ==
                 keymap_lookup_t::BOUND ) { return true; }
    }
    return false;
}

bool UnboundInheritedChord( const gui::editor_gui_t &gui, string_view_t context, string_view_t command,
                            usize iSource, const key_chord_t &chord ) noexcept
{
    // An empty overlay can unbind a main-section chord in the same file;
    // an empty main entry can unbind its base. Only the nearest usable
    // inherited binding reserves keys, not every historical base binding.
    const keymap_platform_t platform = EditorKeymap_HostPlatform();
    for ( usize i = iSource; i < gui.nKeymapChain && i < EDITOR_KEYMAP_MAX_DEPTH; ++i ) {
        keymap_binding_t inherited{};
        const keymap_lookup_t status = EditorKeymap_FindBindingOn( &gui.keymapChain[i], 1u, platform, context, command, &inherited );
        if ( status == keymap_lookup_t::BOUND ) { return BindingHas( inherited, chord ); }
        if ( status == keymap_lookup_t::UNBOUND && platform != keymap_platform_t::NONE &&
             EditorKeymap_FindBindingOn( &gui.keymapChain[i], 1u, keymap_platform_t::NONE, context, command, &inherited ) == keymap_lookup_t::BOUND ) {
            return BindingHas( inherited, chord );
        }
    }
    return false;
}

bool ContextUnbindsChord( const gui::editor_gui_t &gui, string_view_t context, const key_chord_t &chord ) noexcept
{
    const keymap_platform_t platform = EditorKeymap_HostPlatform();
    for ( usize i = 0u; i < gui.nKeymapChain && i < EDITOR_KEYMAP_MAX_DEPTH; ++i ) {
        for ( const bool overlay : { true, false } ) {
            const key_value_t *pContext = BindingContext( gui.keymapChain[i], context, platform, overlay );
            for ( usize entry = 0u; entry < KeyValue_ChildCount( pContext ); ++entry ) {
                const string_view_t command = KeyValue_Name( KeyValue_ChildAt( pContext, entry ) );
                keymap_binding_t effective{};
                if ( EditorKeymap_FindBindingOn( gui.keymapChain, gui.nKeymapChain, platform, context, command, &effective ) ==
                         keymap_lookup_t::UNBOUND &&
                     UnboundInheritedChord( gui, context, command, effective.iSource, chord ) ) { return true; }
            }
        }
    }
    return false;
}

struct input_binding_t {
    string_view_t command{};
    bool reserved{ false };
};

struct input_contexts_t {
    QByteArray tool;
    QByteArray selection;
    string_view_t values[6]{};
    input_contexts_t( const map_workspace_t &workspace, bool camera ) {
        tool = QStringLiteral( "map.tool.%1" )
            .arg( QString::fromUtf8( MapWorkspace_ToolName( workspace.tool ) ).toLower().replace( QLatin1Char( ' ' ), QLatin1Char( '_' ) ) ).toUtf8();
        selection = QStringLiteral( "map.selection.%1" )
            .arg( workspace.tool == map_tool_t::NONE ? QStringLiteral( "navigation" ) :
                QString::fromUtf8( MapWorkspace_ElementModeName( workspace.elementMode ) ).toLower() ).toUtf8();
        values[0] = { tool.constData(), static_cast<usize>( tool.size() ) };
        values[1] = { selection.constData(), static_cast<usize>( selection.size() ) };
        values[2] = StringView_FromCString( camera ? "map.viewport.3d" : "map.viewport.2d" );
        values[3] = StringView_FromCString( "map.viewport" );
        values[4] = StringView_FromCString( "map" ); values[5] = StringView_FromCString( "global" );
    }
};

constexpr struct {
    const char *id;
    map_camera_gesture_t gesture;
    const char *defaults[2];
} kCameraGestures[]{
    { "map.camera.look", map_camera_gesture_t::LOOK, { "RightDrag", "Alt+MiddleDrag" } },
    { "map.camera.orbit", map_camera_gesture_t::ORBIT, { "Alt+LeftDrag", nullptr } },
    { "map.camera.pan", map_camera_gesture_t::PAN, { "MiddleDrag", "Space+LeftDrag" } },
    { "map.camera.dolly", map_camera_gesture_t::DOLLY, { "Alt+RightDrag", "Wheel" } }
};

constexpr struct {
    const char *id;
    map_camera_wheel_action_t action;
    const char *defaultTrigger;
} kCameraLookWheels[]{
    { "map.camera.speed_increase", map_camera_wheel_action_t::SPEED_INCREASE, "WheelUp" },
    { "map.camera.speed_decrease", map_camera_wheel_action_t::SPEED_DECREASE, "WheelDown" }
};

constexpr struct {
    const char *id;
    u32 flag;
    const char *defaults[2];
} kCameraNavigation[]{
    { "map.camera.forward", MAP_NAVIGATION_FORWARD, { "W", "Up" } },
    { "map.camera.back", MAP_NAVIGATION_BACK, { "S", "Down" } },
    { "map.camera.left", MAP_NAVIGATION_LEFT, { "A", "Left" } },
    { "map.camera.right", MAP_NAVIGATION_RIGHT, { "D", "Right" } },
    { "map.camera.up", MAP_NAVIGATION_UP, { "E", "PageUp" } },
    { "map.camera.down", MAP_NAVIGATION_DOWN, { "Q", "PageDown" } },
    { "map.camera.fast", MAP_NAVIGATION_FAST, { "Shift", nullptr } },
    { "map.camera.slow", MAP_NAVIGATION_SLOW, { "Alt", nullptr } }
};

map_camera_gesture_t CameraGesture( string_view_t command ) noexcept
{
    for ( const auto &entry : kCameraGestures ) {
        if ( StringView_Equals( command, StringView_FromCString( entry.id ) ) ) { return entry.gesture; }
    }
    return map_camera_gesture_t::NONE;
}

u32 CameraNavigation( string_view_t command ) noexcept
{
    for ( const auto &entry : kCameraNavigation ) {
        if ( StringView_Equals( command, StringView_FromCString( entry.id ) ) ) { return entry.flag; }
    }
    return 0u;
}

u8 MouseModifiers( Qt::KeyboardModifiers modifiers ) noexcept
{
    u8 result = KEY_MODIFIER_NONE;
    if ( modifiers.testFlag( Qt::ControlModifier ) ) { result |= KEY_MODIFIER_CTRL; }
    if ( modifiers.testFlag( Qt::AltModifier ) ) { result |= KEY_MODIFIER_ALT; }
    if ( modifiers.testFlag( Qt::ShiftModifier ) ) { result |= KEY_MODIFIER_SHIFT; }
    if ( modifiers.testFlag( Qt::MetaModifier ) ) { result |= KEY_MODIFIER_META; }
    return result;
}

u8 MouseButton( Qt::MouseButton button ) noexcept
{
    switch ( button ) {
        case Qt::LeftButton: return MOUSE_BUTTON_LEFT;
        case Qt::RightButton: return MOUSE_BUTTON_RIGHT;
        case Qt::MiddleButton: return MOUSE_BUTTON_MIDDLE;
        case Qt::BackButton: return MOUSE_BUTTON_BACK;
        case Qt::ForwardButton: return MOUSE_BUTTON_FORWARD;
        default: return MOUSE_BUTTON_NONE;
    }
}

bool SupportedCameraTrigger( map_camera_gesture_t gesture, const mouse_gesture_t &trigger ) noexcept
{
    if ( trigger.heldKey != KEY_NONE && trigger.heldKey != KEY_SPACE ) { return false; }
    if ( trigger.action == MOUSE_ACTION_DRAG ) { return trigger.button != MOUSE_BUTTON_NONE; }
    return gesture == map_camera_gesture_t::DOLLY && trigger.heldKey == KEY_NONE && trigger.button == MOUSE_BUTTON_NONE &&
           ( trigger.action == MOUSE_ACTION_WHEEL || trigger.action == MOUSE_ACTION_WHEEL_UP || trigger.action == MOUSE_ACTION_WHEEL_DOWN );
}

bool MouseMatches( const mouse_gesture_t &actual, const mouse_gesture_t &binding ) noexcept
{
    if ( actual.button != binding.button || ( actual.modifiers & binding.modifiers ) != binding.modifiers ||
         ( binding.heldKey != KEY_NONE && binding.heldKey != actual.heldKey ) ) { return false; }
    constexpr u8 commandModifiers = KEY_MODIFIER_CTRL | KEY_MODIFIER_META;
    if ( ( actual.modifiers & commandModifiers ) != ( binding.modifiers & commandModifiers ) ) { return false; }
    return binding.action == actual.action ||
           ( binding.action == MOUSE_ACTION_WHEEL && ( actual.action == MOUSE_ACTION_WHEEL_UP || actual.action == MOUSE_ACTION_WHEEL_DOWN ) );
}

int MouseSpecificity( const mouse_gesture_t &gesture ) noexcept
{
    return std::popcount( gesture.modifiers ) + ( gesture.heldKey != KEY_NONE ? 1 : 0 ) +
           ( gesture.action == MOUSE_ACTION_WHEEL_UP || gesture.action == MOUSE_ACTION_WHEEL_DOWN ? 1 : 0 );
}

input_binding_t MouseContextBinding( const map_workspace_t &workspace, const mouse_gesture_t &actual, string_view_t context )
{
    const auto &gui = *workspace.pGui;
    const auto platform = EditorKeymap_HostPlatform();
    input_binding_t result{}; int specificity = -1; usize priority = CY_INVALID_SIZE;
    for ( usize source = 0u; source < gui.nKeymapChain && source < EDITOR_KEYMAP_MAX_DEPTH; ++source ) {
        for ( const bool overlay : { true, false } ) {
            const auto *entries = TriggerContext( gui.keymapChain[source], keymap_section_t::MOUSE, context, platform, overlay );
            for ( usize entry = 0u; entry < KeyValue_ChildCount( entries ); ++entry ) {
                const auto action = KeyValue_Name( KeyValue_ChildAt( entries, entry ) );
                keymap_triggers_t triggers{};
                const auto status = EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, keymap_section_t::MOUSE,
                                                               platform, context, action, &triggers );
                if ( status == keymap_lookup_t::NOT_DEFINED || triggers.iSource != source ) { continue; }
                const bool unbound = status == keymap_lookup_t::UNBOUND;
                if ( unbound && !InheritedTriggers( gui, keymap_section_t::MOUSE, context, action, source, triggers ) ) { continue; }
                for ( usize i = 0u; i < triggers.nTexts; ++i ) {
                    mouse_gesture_t gesture{};
                    if ( !EditorMouseGesture_Parse( triggers.texts[i], &gesture ) || !MouseMatches( actual, gesture ) ) { continue; }
                    const int score = MouseSpecificity( gesture );
                    const usize candidatePriority = source * 2u + ( overlay ? 0u : 1u );
                    // A deliberate reassignment at the deciding tier can
                    // reuse an unbound trigger. A lower base or main
                    // section cannot silently revive that same gesture.
                    if ( score > specificity || ( score == specificity && candidatePriority == priority &&
                                                 result.command.cchLength == 0u && !unbound ) ) {
                        specificity = score; priority = candidatePriority; result = { unbound ? string_view_t{} : action, true };
                    }
                }
            }
        }
    }
    return result;
}

input_binding_t MouseBinding( const map_workspace_t &workspace, const mouse_gesture_t &actual )
{
    const auto &gui = *workspace.pGui;
    if ( gui.nKeymapChain == 0u ) {
        input_binding_t result{}; int specificity = -1;
        for ( const auto &entry : kCameraGestures ) {
            for ( const auto *text : entry.defaults ) {
                mouse_gesture_t gesture{};
                if ( text != nullptr && EditorMouseGesture_Parse( StringView_FromCString( text ), &gesture ) && MouseMatches( actual, gesture ) &&
                     MouseSpecificity( gesture ) > specificity ) {
                    specificity = MouseSpecificity( gesture ); result = { StringView_FromCString( entry.id ), true };
                }
            }
        }
        return result;
    }
    const input_contexts_t contexts( workspace, true );
    for ( const auto context : contexts.values ) {
        const auto result = MouseContextBinding( workspace, actual, context );
        if ( result.reserved ) { return result; }
    }
    return {};
}

map_camera_wheel_action_t CameraWheelAction( const map_workspace_t &workspace, const mouse_gesture_t &actual, bool lookCaptured )
{
    input_binding_t binding{};
    if ( lookCaptured ) {
        if ( workspace.pGui->nKeymapChain == 0u ) {
            for ( const auto &entry : kCameraLookWheels ) {
                mouse_gesture_t trigger{};
                if ( EditorMouseGesture_Parse( StringView_FromCString( entry.defaultTrigger ), &trigger ) && MouseMatches( actual, trigger ) ) {
                    return entry.action;
                }
            }
        } else { binding = MouseContextBinding( workspace, actual, StringView_FromCString( "map.camera.look" ) ); }
    }
    if ( !binding.reserved ) { binding = MouseBinding( workspace, actual ); }
    if ( !binding.reserved ) { return map_camera_wheel_action_t::NONE; }
    if ( CameraGesture( binding.command ) == map_camera_gesture_t::DOLLY ) { return map_camera_wheel_action_t::DOLLY; }
    for ( const auto &entry : kCameraLookWheels ) {
        if ( StringView_Equals( binding.command, StringView_FromCString( entry.id ) ) ) { return entry.action; }
    }
    return map_camera_wheel_action_t::RESERVED;
}

u32 NavigationMask( const map_workspace_t &workspace, const key_stroke_t &actual )
{
    const auto &gui = *workspace.pGui;
    if ( gui.nKeymapChain == 0u ) {
        u32 mask = 0u;
        for ( const auto &entry : kCameraNavigation ) {
            for ( const auto *text : entry.defaults ) {
                key_stroke_t trigger{};
                if ( text != nullptr && EditorHeldKey_Parse( StringView_FromCString( text ), &trigger ) && HeldMatches( actual, trigger ) ) {
                    mask |= entry.flag; break;
                }
            }
        }
        return mask;
    }
    const auto platform = EditorKeymap_HostPlatform();
    const input_contexts_t contexts( workspace, true );
    for ( const auto context : contexts.values ) {
        u32 mask = 0u; bool reserved = false;
        for ( usize source = 0u; source < gui.nKeymapChain && source < EDITOR_KEYMAP_MAX_DEPTH; ++source ) {
            for ( const bool overlay : { true, false } ) {
                const auto *entries = TriggerContext( gui.keymapChain[source], keymap_section_t::HELD, context, platform, overlay );
                for ( usize entry = 0u; entry < KeyValue_ChildCount( entries ); ++entry ) {
                    const auto action = KeyValue_Name( KeyValue_ChildAt( entries, entry ) );
                    keymap_triggers_t triggers{};
                    const auto status = EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, keymap_section_t::HELD,
                                                                   platform, context, action, &triggers );
                    if ( status == keymap_lookup_t::NOT_DEFINED || triggers.iSource != source ) { continue; }
                    const bool unbound = status == keymap_lookup_t::UNBOUND;
                    if ( unbound && !InheritedTriggers( gui, keymap_section_t::HELD, context, action, source, triggers ) ) { continue; }
                    for ( usize i = 0u; i < triggers.nTexts; ++i ) {
                        key_stroke_t trigger{};
                        if ( EditorHeldKey_Parse( triggers.texts[i], &trigger ) && HeldMatches( actual, trigger ) ) {
                            reserved = true;
                            if ( !unbound ) { mask |= CameraNavigation( action ); }
                            break;
                        }
                    }
                }
            }
        }
        if ( reserved ) { return mask; }
    }
    return 0u;
}

input_binding_t ChordBinding( const map_workspace_t &workspace, const key_chord_t &chord, bool camera )
{
    const gui::editor_gui_t &gui = *workspace.pGui;
    const input_contexts_t contexts( workspace, camera );
    for ( const string_view_t context : contexts.values ) {
        const string_view_t command = EditorKeymap_FindCommandInStack( gui.keymapChain, gui.nKeymapChain, EditorKeymap_HostPlatform(),
                                                                      &context, 1u, chord );
        if ( command.cchLength != 0u ) { return { command, true }; }
        if ( ContextUnbindsChord( gui, context, chord ) ) { return { {}, true }; }
    }
    return {};
}

input_binding_t EventBinding( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent, bool camera )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pEvent == nullptr || pWorkspace->pGui->nKeymapChain == 0u ) { return {}; }
    key_chord_t chord{};
    if ( !EventChord( *pEvent, chord ) || chord.nStrokes != 1u ) { return {}; }
    return ChordBinding( *pWorkspace, chord, camera );
}

} // namespace

map_camera_gesture_t MapInput_CameraDragGesture( const map_workspace_t *pWorkspace, const QMouseEvent *pEvent, bool spaceHeld )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pEvent == nullptr ) { return map_camera_gesture_t::NONE; }
    const u8 button = MouseButton( pEvent->button() );
    if ( button == MOUSE_BUTTON_NONE ) { return map_camera_gesture_t::NONE; }
    const mouse_gesture_t actual{ MouseModifiers( pEvent->modifiers() ), static_cast<u16>( spaceHeld ? KEY_SPACE : KEY_NONE ),
                                  button, MOUSE_ACTION_DRAG };
    return CameraGesture( MouseBinding( *pWorkspace, actual ).command );
}

bool MapInput_CameraWheelGesture( const map_workspace_t *pWorkspace, const QWheelEvent *pEvent )
{
    return MapInput_CameraWheelAction( pWorkspace, pEvent, false ) == map_camera_wheel_action_t::DOLLY;
}

map_camera_wheel_action_t MapInput_CameraWheelAction( const map_workspace_t *pWorkspace, const QWheelEvent *pEvent, bool lookCaptured )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pEvent == nullptr ) { return map_camera_wheel_action_t::NONE; }
    const int vertical = pEvent->angleDelta().y() != 0 ? pEvent->angleDelta().y() : pEvent->pixelDelta().y();
    if ( vertical == 0 ) { return map_camera_wheel_action_t::NONE; }
    const mouse_gesture_t actual{ MouseModifiers( pEvent->modifiers() ), KEY_NONE, MOUSE_BUTTON_NONE,
                                  static_cast<u8>( vertical > 0 ? MOUSE_ACTION_WHEEL_UP : MOUSE_ACTION_WHEEL_DOWN ) };
    return CameraWheelAction( *pWorkspace, actual, lookCaptured );
}

QStringList MapInput_CameraLookWheelBindings( const map_workspace_t *pWorkspace, bool increase )
{
    QStringList texts;
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr ) { return texts; }
    const auto wanted = increase ? map_camera_wheel_action_t::SPEED_INCREASE : map_camera_wheel_action_t::SPEED_DECREASE;
    const auto &entry = kCameraLookWheels[increase ? 0u : 1u];
    const auto &gui = *pWorkspace->pGui;
    const auto add = [&]( string_view_t text ) {
        mouse_gesture_t trigger{};
        if ( !EditorMouseGesture_Parse( text, &trigger ) || trigger.heldKey != KEY_NONE || trigger.button != MOUSE_BUTTON_NONE ||
             ( trigger.action != MOUSE_ACTION_WHEEL && trigger.action != MOUSE_ACTION_WHEEL_UP && trigger.action != MOUSE_ACTION_WHEEL_DOWN ) ) { return; }
        auto actual = trigger;
        if ( trigger.action == MOUSE_ACTION_WHEEL ) {
            actual.action = MOUSE_ACTION_WHEEL_UP;
            if ( CameraWheelAction( *pWorkspace, actual, true ) != wanted ) { return; }
            actual.action = MOUSE_ACTION_WHEEL_DOWN;
        }
        if ( CameraWheelAction( *pWorkspace, actual, true ) != wanted ) { return; }
        char canonical[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
        const usize length = EditorMouseGesture_Format( trigger, canonical );
        if ( length != 0u ) {
            const QString formatted = QString::fromUtf8( canonical, static_cast<qsizetype>( length ) );
            if ( !texts.contains( formatted ) ) { texts.append( formatted ); }
        }
    };
    if ( gui.nKeymapChain == 0u ) { add( StringView_FromCString( entry.defaultTrigger ) ); }
    else {
        const auto appendContext = [&]( string_view_t context ) {
            keymap_triggers_t triggers{};
            if ( EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, keymap_section_t::MOUSE, EditorKeymap_HostPlatform(),
                                           context, StringView_FromCString( entry.id ), &triggers ) != keymap_lookup_t::BOUND ) { return; }
            for ( usize i = 0u; i < triggers.nTexts; ++i ) { add( triggers.texts[i] ); }
        };
        appendContext( StringView_FromCString( "map.camera.look" ) );
        const input_contexts_t contexts( *pWorkspace, true );
        for ( const auto context : contexts.values ) { appendContext( context ); }
    }
    texts.sort();
    return texts;
}

QStringList MapInput_CameraGestureBindings( const map_workspace_t *pWorkspace, map_camera_gesture_t gesture )
{
    QStringList texts;
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || gesture == map_camera_gesture_t::NONE ) { return texts; }
    const auto &gui = *pWorkspace->pGui;
    const auto add = [&]( string_view_t text, string_view_t command ) {
        mouse_gesture_t trigger{};
        if ( !EditorMouseGesture_Parse( text, &trigger ) || !SupportedCameraTrigger( gesture, trigger ) ) { return; }
        auto actual = trigger;
        if ( trigger.action == MOUSE_ACTION_WHEEL ) {
            actual.action = MOUSE_ACTION_WHEEL_UP;
            if ( !StringView_Equals( MouseBinding( *pWorkspace, actual ).command, command ) ) { return; }
            actual.action = MOUSE_ACTION_WHEEL_DOWN;
        }
        if ( !StringView_Equals( MouseBinding( *pWorkspace, actual ).command, command ) ) { return; }
        char canonical[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
        const usize length = EditorMouseGesture_Format( trigger, canonical );
        if ( length != 0u ) {
            const QString formatted = QString::fromUtf8( canonical, static_cast<qsizetype>( length ) );
            if ( !texts.contains( formatted ) ) { texts.append( formatted ); }
        }
    };
    for ( const auto &entry : kCameraGestures ) {
        if ( entry.gesture != gesture ) { continue; }
        const auto command = StringView_FromCString( entry.id );
        if ( gui.nKeymapChain == 0u ) {
            for ( const auto *text : entry.defaults ) { if ( text != nullptr ) { add( StringView_FromCString( text ), command ); } }
            break;
        }
        const input_contexts_t contexts( *pWorkspace, true );
        for ( const auto context : contexts.values ) {
            keymap_triggers_t triggers{};
            if ( EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, keymap_section_t::MOUSE, EditorKeymap_HostPlatform(),
                                           context, command, &triggers ) != keymap_lookup_t::BOUND ) { continue; }
            for ( usize i = 0u; i < triggers.nTexts; ++i ) { add( triggers.texts[i], command ); }
        }
    }
    texts.sort();
    return texts;
}

QStringList MapInput_NavigationBindings( const map_workspace_t *pWorkspace, u32 flags )
{
    QStringList texts;
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr ) { return texts; }
    const auto &gui = *pWorkspace->pGui;
    const auto add = [&]( string_view_t text, u32 flag ) {
        key_stroke_t trigger{};
        if ( !EditorHeldKey_Parse( text, &trigger ) || ( NavigationMask( *pWorkspace, trigger ) & flag ) == 0u ) { return; }
        char canonical[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
        const usize length = EditorHeldKey_Format( trigger, canonical );
        if ( length != 0u ) {
            const QString formatted = QString::fromUtf8( canonical, static_cast<qsizetype>( length ) );
            if ( !texts.contains( formatted ) ) { texts.append( formatted ); }
        }
    };
    for ( const auto &entry : kCameraNavigation ) {
        if ( ( entry.flag & flags ) == 0u ) { continue; }
        if ( gui.nKeymapChain == 0u ) {
            for ( const auto *text : entry.defaults ) { if ( text != nullptr ) { add( StringView_FromCString( text ), entry.flag ); } }
            continue;
        }
        const input_contexts_t contexts( *pWorkspace, true );
        for ( const auto context : contexts.values ) {
            keymap_triggers_t triggers{};
            if ( EditorKeymap_FindTriggers( gui.keymapChain, gui.nKeymapChain, keymap_section_t::HELD, EditorKeymap_HostPlatform(), context,
                                           StringView_FromCString( entry.id ), &triggers ) != keymap_lookup_t::BOUND ) { continue; }
            for ( usize i = 0u; i < triggers.nTexts; ++i ) { add( triggers.texts[i], entry.flag ); }
        }
    }
    texts.sort();
    return texts;
}

map_tool_gesture_key_t MapInput_ToolGestureKey( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent, bool camera )
{
    const input_binding_t binding = EventBinding( pWorkspace, pEvent, camera );
    if ( StringView_Equals( binding.command, StringView_FromCString( "map.tool.confirm" ) ) ) { return map_tool_gesture_key_t::CONFIRM; }
    if ( StringView_Equals( binding.command, StringView_FromCString( "map.tool.cancel" ) ) ) { return map_tool_gesture_key_t::CANCEL; }
    return map_tool_gesture_key_t::NONE;
}

QStringList MapInput_CommandBindings( const map_workspace_t *pWorkspace, const char *pCommand, bool camera )
{
    QStringList texts;
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pWorkspace->pGui->nKeymapChain == 0u ||
         pCommand == nullptr || pCommand[0] == '\0' ) { return texts; }
    const auto &gui = *pWorkspace->pGui;
    const auto command = StringView_FromCString( pCommand );
    const input_contexts_t contexts( *pWorkspace, camera );
    for ( const auto context : contexts.values ) {
        keymap_binding_t binding{};
        if ( EditorKeymap_FindBindingOn( gui.keymapChain, gui.nKeymapChain, EditorKeymap_HostPlatform(), context, command, &binding ) !=
             keymap_lookup_t::BOUND ) { continue; }
        for ( usize i = 0u; i < binding.nChords; ++i ) {
            const auto &chord = binding.chords[i];
            if ( chord.nStrokes != 1u || !StringView_Equals( ChordBinding( *pWorkspace, chord, camera ).command, command ) ) { continue; }
            char canonical[EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
            const usize length = EditorKeyChord_Format( chord, canonical );
            if ( length == 0u ) { continue; }
            const QString text = QString::fromUtf8( canonical, static_cast<qsizetype>( length ) );
            if ( !texts.contains( text ) ) { texts.append( text ); }
        }
    }
    texts.sort();
    return texts;
}

QStringList MapInput_ToolGestureBindings( const map_workspace_t *pWorkspace, map_tool_gesture_key_t gesture, bool camera )
{
    switch ( gesture ) {
        case map_tool_gesture_key_t::CONFIRM: return MapInput_CommandBindings( pWorkspace, "map.tool.confirm", camera );
        case map_tool_gesture_key_t::CANCEL: return MapInput_CommandBindings( pWorkspace, "map.tool.cancel", camera );
        default: return {};
    }
}

u32 MapInput_NavigationKeyMask( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pEvent == nullptr ) { return 0u; }
    key_stroke_t actual{};
    if ( !HeldEventStroke( *pEvent, actual ) ) { return 0u; }
    return NavigationMask( *pWorkspace, actual );
}

bool MapInput_IsNavigationKey( const map_workspace_t *pWorkspace, const QKeyEvent *pEvent, bool navigationOwned )
{
    if ( !navigationOwned && pWorkspace != nullptr && pWorkspace->pGui != nullptr && pEvent != nullptr &&
         pWorkspace->tool == map_tool_t::NONE && pEvent->modifiers() != Qt::NoModifier ) {
        // An explicit tool chord such as Shift+S must be able to leave
        // idle Navigation. Captured look and held flight keep priority for
        // configured movement, including Shift for fast flight. Resolve the user's active
        // keymap instead of giving a hard-coded Select shortcut special rules.
        const auto binding = EventBinding( pWorkspace, pEvent, true );
        if ( StringView_StartsWith( binding.command, StringView_FromCString( "map.tool." ) ) &&
             !StringView_Equals( binding.command, StringView_FromCString( "map.tool.confirm" ) ) &&
             !StringView_Equals( binding.command, StringView_FromCString( "map.tool.cancel" ) ) &&
             EditorCommands_Find( &pWorkspace->pGui->commands, binding.command ) != nullptr &&
             ( EditorCommands_State( &pWorkspace->pGui->commands, binding.command ) & COMMAND_STATE_ENABLED ) != 0u ) { return false; }
    }
    return MapInput_NavigationKeyMask( pWorkspace, pEvent ) != 0u;
}

bool MapInput_DispatchKey( map_workspace_t *pWorkspace, QKeyEvent *pEvent, bool camera, bool navigationActive, bool execute, bool navigationOwned )
{
    if ( pWorkspace == nullptr || pWorkspace->pGui == nullptr || pEvent == nullptr ) { return false; }
    const gui::editor_gui_t &gui = *pWorkspace->pGui;
    if ( gui.nKeymapChain == 0u || ( camera && navigationActive && MapInput_IsNavigationKey( pWorkspace, pEvent, navigationOwned ) ) ) { return false; }
    const input_binding_t binding = EventBinding( pWorkspace, pEvent, camera );
    if ( execute && binding.command.cchLength != 0u && EditorCommands_Find( &gui.commands, binding.command ) != nullptr &&
         ( EditorCommands_State( &gui.commands, binding.command ) & COMMAND_STATE_ENABLED ) != 0u ) {
        // Failed validation still consumes the key, preventing a second
        // execution by a less-specific window shortcut.
        ( void )EditorCommands_Execute( &gui.commands, binding.command, command_args_t{} );
    }
    return binding.reserved;
}

} // namespace cypher::editor::map
