//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Keymap.h
//  Purpose: Declares editor keymaps (`.cykeymap`, cypher.editor_keymap V1): key
//           chords, their canonical text form, and bindings from editor
//           commands to chords per input context, layered over a base keymap.
//  Details: Editor shortcuts bind editor commands (ADR 0008's
//           `module.command` registry). They are a separate document family
//           from game input (`.cyinput` action maps and `.cybindings` player
//           overrides, which bind gameplay actions, gamepads, and axes): the
//           schema IDs differ, so neither loader accepts the other's files.
//           A keymap names its base and stores only what it
//           changes; an empty chord list explicitly unbinds a command the
//           base binds:
//
//             @cykv 1
//             @schema "cypher.editor_keymap" 1
//             {
//                 id = "karlo"
//                 name = "Karlo's keys"
//                 base = "cypher_default"
//                 bindings = {
//                     global = { "file.save" = [ "Ctrl+S" ] "view.console" = [ "Backquote" ] }
//                     "map.viewport" = { "map.tool.clip" = [ "Shift+X" ] "map.tool.vertex" = [] }
//                 }
//             }
//
//           Key codes are Qt-free: printable keys use their upper-case ASCII
//           code and named keys use values from 0x100 up; the GUI layer maps
//           them to its toolkit.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_CORE_KEYMAP_H
#define CYPHER_EDITOR_CORE_KEYMAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Commands.h"

#include "CypherCommon/Tier2/CypherCommon_SettingsDocument.h"

namespace cypher::editor
{

inline constexpr common::u32 EDITOR_KEYMAP_SCHEMA_VERSION = 1u;
inline constexpr common::usize EDITOR_KEY_CHORD_MAX_STROKES = 4u;   // "Ctrl+K, Ctrl+C" style sequences.
inline constexpr common::usize EDITOR_KEY_CHORD_TEXT_CAPACITY = 128u;
inline constexpr common::usize EDITOR_KEYMAP_MAX_CHORDS = 4u;       // Chords bound to one command.
inline constexpr common::usize EDITOR_KEYMAP_MAX_DEPTH = 8u;        // Base-chain bound.

enum key_modifier_flags_t : common::u8 {
    KEY_MODIFIER_NONE = 0u,
    KEY_MODIFIER_CTRL = 1u << 0u,  // Control; Command on macOS is Meta.
    KEY_MODIFIER_ALT = 1u << 1u,   // Alt / Option.
    KEY_MODIFIER_SHIFT = 1u << 2u,
    KEY_MODIFIER_META = 1u << 3u   // Command / Windows / Super.
};

// Named keys start above the ASCII range used by printable keys.
enum key_code_t : common::u16 {
    KEY_NONE = 0u,
    KEY_SPACE = 0x20u,
    KEY_NAMED_BASE = 0x100u,
    KEY_ESCAPE = KEY_NAMED_BASE, KEY_TAB, KEY_BACKSPACE, KEY_ENTER, KEY_INSERT, KEY_DELETE,
    KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
    KEY_F1 = 0x140u, // KEY_F1 + n - 1 for F1..F24.
    KEY_NUMPAD_0 = 0x160u, // KEY_NUMPAD_0 + n for Num0..Num9.
    KEY_NUMPAD_ADD = 0x16Au, KEY_NUMPAD_SUBTRACT, KEY_NUMPAD_MULTIPLY, KEY_NUMPAD_DIVIDE,
    KEY_NUMPAD_DECIMAL, KEY_NUMPAD_ENTER
};

struct key_stroke_t {
    common::u8 modifiers{ KEY_MODIFIER_NONE }; // key_modifier_flags_t bits.
    common::u16 key{ KEY_NONE };               // key_code_t or upper-case ASCII.
};

struct key_chord_t {
    key_stroke_t strokes[EDITOR_KEY_CHORD_MAX_STROKES]{};
    common::u8 nStrokes{ 0u };
};

// Parses "Ctrl+Shift+S" or "Ctrl+K, Ctrl+C". Modifier names are
// case-insensitive (Ctrl/Control, Alt/Option, Shift, Meta/Cmd/Command/Super/
// Win); keys accept letters, digits, punctuation, F1-F24, Num0-Num9, and
// names such as Space, Enter, Escape, PageUp. A stroke needs exactly one key.
CYPHER_NODISCARD common::bool_t EditorKeyChord_Parse( common::string_view_t text, key_chord_t *pChordOut ) noexcept;

// Canonical text: modifiers in Ctrl+Alt+Shift+Meta order, canonical key name,
// strokes joined by ", ". Returns the length; 0 for an empty or invalid chord.
common::usize EditorKeyChord_Format( const key_chord_t &chord, char ( &buffer )[EDITOR_KEY_CHORD_TEXT_CAPACITY] ) noexcept;

CYPHER_NODISCARD common::bool_t EditorKeyChord_Equals( const key_chord_t &a, const key_chord_t &b ) noexcept;

/*
================
Keymap Documents
================
*/
CYPHER_NODISCARD common::settings_document_identity_t EditorKeymap_Identity() noexcept;

struct keymap_header_t {
    common::string_view_t id{};
    common::string_view_t name{};
    common::string_view_t base{};
};

CYPHER_NODISCARD keymap_header_t EditorKeymap_Header( const common::key_value_t *pKeymapRoot ) noexcept;

enum class keymap_lookup_t : common::u8 {
    BOUND = 0u,   // Chords were found.
    UNBOUND,      // A keymap in the chain explicitly unbinds the command.
    NOT_DEFINED   // No keymap in the chain mentions the command.
};

struct keymap_binding_t {
    key_chord_t chords[EDITOR_KEYMAP_MAX_CHORDS]{};
    common::usize nChords{ 0u };
    common::usize iSource{ common::CY_INVALID_SIZE }; // Chain index that decided.
    common::usize nInvalidChords{ 0u };               // Unparseable chords skipped.
};

// The first keymap in the chain (most specific first) that mentions the
// command in the context decides its chords.
CYPHER_NODISCARD keymap_lookup_t EditorKeymap_FindBinding(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    common::string_view_t context,
    common::string_view_t command,
    keymap_binding_t *pBindingOut ) noexcept;

// Finds the command a chord triggers in a context, honouring overrides: a
// base keymap's binding only counts when no more specific keymap redefines
// that command. Returns an empty view when nothing is bound. The view
// borrows the keymap document.
CYPHER_NODISCARD common::string_view_t EditorKeymap_FindCommand(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    common::string_view_t context,
    const key_chord_t &chord ) noexcept;

struct keymap_conflict_t {
    common::string_view_t first{};  // Earlier command in document order.
    common::string_view_t second{};
    key_chord_t chord{};
};

// Lists chords that two commands both resolve to within one context.
// Returns the number found; at most nCapacity are written.
CYPHER_NODISCARD common::usize EditorKeymap_FindConflicts(
    const common::key_value_t *const *ppChain,
    common::usize nChain,
    common::string_view_t context,
    keymap_conflict_t *pConflicts,
    common::usize nCapacity ) noexcept;

enum class keymap_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT, // Bad context, command, chord, or too many chords.
    OUT_OF_MEMORY,
    STORE_FAILED
};

// Stores chords for a command in a context; nChords == 0 explicitly unbinds.
CYPHER_NODISCARD keymap_status_t EditorKeymap_SetBinding(
    common::settings_document_t *pKeymap,
    common::string_view_t context,
    common::string_view_t command,
    const key_chord_t *pChords,
    common::usize nChords ) noexcept;

// Removes the keymap's own entry so the command inherits from the base.
CYPHER_NODISCARD keymap_status_t EditorKeymap_ResetBinding(
    common::settings_document_t *pKeymap,
    common::string_view_t context,
    common::string_view_t command ) noexcept;

} // namespace cypher::editor

#endif // CYPHER_EDITOR_CORE_KEYMAP_H
