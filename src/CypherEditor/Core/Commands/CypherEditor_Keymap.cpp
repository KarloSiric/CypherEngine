//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Keymap.cpp
//  Purpose: Implements key-chord text, keymap lookup through base chains,
//           conflict detection, and sparse keymap editing.
//  Details: Punctuation keys are written as words (Comma, Slash, ...) in the
//           canonical form because "," also separates chord strokes; the
//           parser still accepts the symbols where they are unambiguous.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Keymap.h"

#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <string>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t KeyText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

struct key_name_t {
    const char *pName; // First entry per code is canonical.
    u16 key;
};

inline constexpr key_name_t kKeyNames[]{
    { "Space", KEY_SPACE }, { "Escape", KEY_ESCAPE }, { "Esc", KEY_ESCAPE }, { "Tab", KEY_TAB },
    { "Backspace", KEY_BACKSPACE }, { "Enter", KEY_ENTER }, { "Return", KEY_ENTER },
    { "Insert", KEY_INSERT }, { "Ins", KEY_INSERT }, { "Delete", KEY_DELETE }, { "Del", KEY_DELETE },
    { "Home", KEY_HOME }, { "End", KEY_END }, { "PageUp", KEY_PAGE_UP }, { "PgUp", KEY_PAGE_UP },
    { "PageDown", KEY_PAGE_DOWN }, { "PgDn", KEY_PAGE_DOWN }, { "Left", KEY_LEFT }, { "Right", KEY_RIGHT },
    { "Up", KEY_UP }, { "Down", KEY_DOWN },
    { "Backquote", '`' }, { "Minus", '-' }, { "Equal", '=' }, { "Plus", '+' }, { "BracketLeft", '[' }, { "BracketRight", ']' },
    { "Backslash", '\\' }, { "Semicolon", ';' }, { "Quote", '\'' }, { "Comma", ',' }, { "Period", '.' },
    { "Slash", '/' },
    { "NumAdd", KEY_NUMPAD_ADD }, { "NumSubtract", KEY_NUMPAD_SUBTRACT }, { "NumMultiply", KEY_NUMPAD_MULTIPLY },
    { "NumDivide", KEY_NUMPAD_DIVIDE }, { "NumDecimal", KEY_NUMPAD_DECIMAL }, { "NumEnter", KEY_NUMPAD_ENTER },
};

CYPHER_NODISCARD char ToUpper( char c ) noexcept
{
    return c >= 'a' && c <= 'z' ? static_cast<char>( c - 'a' + 'A' ) : c;
}

CYPHER_NODISCARD bool_t EqualsIgnoreCase( string_view_t a, const char *pB ) noexcept
{
    usize iChar = 0u;
    for ( ; iChar < a.cchLength; ++iChar ) {
        if ( pB[iChar] == '\0' || ToUpper( a.pData[iChar] ) != ToUpper( pB[iChar] ) ) { return CY_FALSE; }
    }
    return pB[iChar] == '\0';
}

CYPHER_NODISCARD string_view_t Trim( string_view_t text ) noexcept
{
    while ( text.cchLength != 0u && text.pData[0] == ' ' ) { ++text.pData; --text.cchLength; }
    while ( text.cchLength != 0u && text.pData[text.cchLength - 1u] == ' ' ) { --text.cchLength; }
    return text;
}

CYPHER_NODISCARD u8 ParseModifier( string_view_t token ) noexcept
{
    if ( EqualsIgnoreCase( token, "Ctrl" ) || EqualsIgnoreCase( token, "Control" ) ) { return KEY_MODIFIER_CTRL; }
    if ( EqualsIgnoreCase( token, "Alt" ) || EqualsIgnoreCase( token, "Option" ) ) { return KEY_MODIFIER_ALT; }
    if ( EqualsIgnoreCase( token, "Shift" ) ) { return KEY_MODIFIER_SHIFT; }
    if ( EqualsIgnoreCase( token, "Meta" ) || EqualsIgnoreCase( token, "Cmd" ) || EqualsIgnoreCase( token, "Command" ) ||
         EqualsIgnoreCase( token, "Super" ) || EqualsIgnoreCase( token, "Win" ) ) {
        return KEY_MODIFIER_META;
    }
    return KEY_MODIFIER_NONE;
}

CYPHER_NODISCARD u16 ParseKey( string_view_t token ) noexcept
{
    if ( token.cchLength == 1u ) {
        const char c = ToUpper( token.pData[0] );
        if ( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ) { return static_cast<u16>( c ); }
        for ( const key_name_t &name : kKeyNames ) {
            if ( name.key < 0x80u && name.key != KEY_SPACE && static_cast<char>( name.key ) == c ) { return name.key; }
        }
        return KEY_NONE;
    }
    for ( const key_name_t &name : kKeyNames ) {
        if ( EqualsIgnoreCase( token, name.pName ) ) { return name.key; }
    }
    // F1..F24 and Num0..Num9.
    const auto number = [&]( usize iStart, u32 &valueOut ) noexcept {
        if ( iStart >= token.cchLength || token.cchLength - iStart > 2u ) { return CY_FALSE; }
        valueOut = 0u;
        for ( usize iChar = iStart; iChar < token.cchLength; ++iChar ) {
            if ( token.pData[iChar] < '0' || token.pData[iChar] > '9' ) { return CY_FALSE; }
            valueOut = valueOut * 10u + static_cast<u32>( token.pData[iChar] - '0' );
        }
        return CY_TRUE;
    };
    u32 n = 0u;
    if ( ToUpper( token.pData[0] ) == 'F' && number( 1u, n ) && n >= 1u && n <= 24u ) {
        return static_cast<u16>( KEY_F1 + n - 1u );
    }
    if ( token.cchLength >= 4u && EqualsIgnoreCase( { token.pData, 3u }, "Num" ) && number( 3u, n ) && n <= 9u ) {
        return static_cast<u16>( KEY_NUMPAD_0 + n );
    }
    return KEY_NONE;
}

CYPHER_NODISCARD bool_t ParseStroke( string_view_t text, key_stroke_t &strokeOut ) noexcept
{
    text = Trim( text );
    if ( text.cchLength == 0u ) { return CY_FALSE; }
    key_stroke_t stroke{};
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= text.cchLength; ++iChar ) {
        // '+' separates modifiers; the named Plus key represents that
        // printable symbol without an ambiguous spelling such as "Ctrl++".
        if ( iChar < text.cchLength && text.pData[iChar] != '+' ) { continue; }
        const string_view_t token{ text.pData + iStart, iChar - iStart };
        const bool_t bLast = iChar == text.cchLength;
        if ( token.cchLength == 0u ) { return CY_FALSE; }
        if ( !bLast ) {
            const u8 modifier = ParseModifier( token );
            if ( modifier == KEY_MODIFIER_NONE ) { return CY_FALSE; }
            stroke.modifiers = static_cast<u8>( stroke.modifiers | modifier );
        } else {
            stroke.key = ParseKey( token );
            if ( stroke.key == KEY_NONE ) { return CY_FALSE; }
        }
        iStart = iChar + 1u;
    }
    strokeOut = stroke;
    return CY_TRUE;
}

CYPHER_NODISCARD const char *CanonicalKeyName( u16 key, char ( &scratch )[8] ) noexcept
{
    if ( ( key >= 'A' && key <= 'Z' ) || ( key >= '0' && key <= '9' ) ) {
        scratch[0] = static_cast<char>( key );
        scratch[1] = '\0';
        return scratch;
    }
    if ( key >= KEY_F1 && key < KEY_F1 + 24u ) {
        const u32 n = key - KEY_F1 + 1u;
        usize i = 0u;
        scratch[i++] = 'F';
        if ( n >= 10u ) { scratch[i++] = static_cast<char>( '0' + n / 10u ); }
        scratch[i++] = static_cast<char>( '0' + n % 10u );
        scratch[i] = '\0';
        return scratch;
    }
    if ( key >= KEY_NUMPAD_0 && key <= KEY_NUMPAD_0 + 9u ) {
        scratch[0] = 'N'; scratch[1] = 'u'; scratch[2] = 'm';
        scratch[3] = static_cast<char>( '0' + ( key - KEY_NUMPAD_0 ) );
        scratch[4] = '\0';
        return scratch;
    }
    for ( const key_name_t &name : kKeyNames ) {
        if ( name.key == key ) { return name.pName; }
    }
    return nullptr;
}

CYPHER_NODISCARD bool_t IsContextId( string_view_t context ) noexcept
{
    // Dotted lower-case parts, the whole starting with a letter; a part may
    // start with a digit ("map.viewport.3d"), unlike command IDs.
    if ( context.cchLength == 0u || context.cchLength > EDITOR_COMMAND_ID_MAX_LENGTH || context.pData[0] < 'a' || context.pData[0] > 'z' ||
         context.pData[context.cchLength - 1u] == '.' ) {
        return CY_FALSE;
    }
    for ( usize i = 0u; i < context.cchLength; ++i ) {
        const char c = context.pData[i];
        const bool_t bOk = ( c >= 'a' && c <= 'z' ) || ( c >= '0' && c <= '9' ) || c == '_' || ( c == '.' && context.pData[i - 1u] != '.' );
        if ( !bOk ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD const key_value_t *FindContext( const key_value_t *pKeymapRoot, string_view_t context ) noexcept
{
    const key_value_t *pBindings = KeyValue_Find( pKeymapRoot, KeyText( "bindings" ) );
    if ( KeyValue_Type( pBindings ) != key_value_type_t::OBJECT ) { return nullptr; }
    const key_value_t *pContext = KeyValue_Find( pBindings, context );
    return KeyValue_Type( pContext ) == key_value_type_t::OBJECT ? pContext : nullptr;
}

enum class entry_t : u8 { ABSENT, INVALID, UNBOUND, BOUND };

// Reads one command entry. An entry whose chords are all unparseable is
// invalid, so the base keymap's binding applies instead of silently losing it.
CYPHER_NODISCARD entry_t ReadEntry( const key_value_t *pEntry, keymap_binding_t &bindingOut ) noexcept
{
    bindingOut.nChords = 0u;
    bindingOut.nInvalidChords = 0u;
    if ( pEntry == nullptr ) { return entry_t::ABSENT; }
    if ( KeyValue_Type( pEntry ) != key_value_type_t::ARRAY ) { return entry_t::INVALID; }
    const usize nElements = KeyValue_ChildCount( pEntry );
    if ( nElements == 0u ) { return entry_t::UNBOUND; }
    for ( usize iElement = 0u; iElement < nElements; ++iElement ) {
        string_view_t text{};
        key_chord_t chord{};
        if ( bindingOut.nChords == EDITOR_KEYMAP_MAX_CHORDS ||
             !KeyValue_GetString( KeyValue_ChildAt( pEntry, iElement ), &text ) ||
             !EditorKeyChord_Parse( text, &chord ) ) {
            ++bindingOut.nInvalidChords;
            continue;
        }
        bindingOut.chords[bindingOut.nChords++] = chord;
    }
    return bindingOut.nChords != 0u ? entry_t::BOUND : entry_t::INVALID;
}

CYPHER_NODISCARD bool_t BindingHas( const keymap_binding_t &binding, const key_chord_t &chord ) noexcept
{
    for ( usize iChord = 0u; iChord < binding.nChords; ++iChord ) {
        if ( EditorKeyChord_Equals( binding.chords[iChord], chord ) ) { return CY_TRUE; }
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t IsHeaderId( string_view_t id ) noexcept
{
    return DataValidation_Succeeded( DataValidation_CheckStableIdentifier( id, 64u ) );
}

CYPHER_NODISCARD keymap_status_t FromStore( settings_document_status_t status ) noexcept
{
    switch ( status ) {
        case settings_document_status_t::OK: return keymap_status_t::OK;
        case settings_document_status_t::OUT_OF_MEMORY: return keymap_status_t::OUT_OF_MEMORY;
        case settings_document_status_t::INVALID_ARGUMENT: return keymap_status_t::INVALID_ARGUMENT;
        default: return keymap_status_t::STORE_FAILED;
    }
}

CYPHER_NODISCARD string_view_t SectionName( keymap_section_t section ) noexcept
{
    switch ( section ) {
        case keymap_section_t::BINDINGS: return KeyText( "bindings" );
        case keymap_section_t::HELD: return KeyText( "held" );
        case keymap_section_t::MOUSE: return KeyText( "mouse" );
    }
    return KeyText( "bindings" );
}

CYPHER_NODISCARD string_view_t PlatformName( keymap_platform_t platform ) noexcept
{
    switch ( platform ) {
        case keymap_platform_t::MACOS: return KeyText( "macos" );
        case keymap_platform_t::WINDOWS: return KeyText( "windows" );
        case keymap_platform_t::LINUX: return KeyText( "linux" );
        case keymap_platform_t::NONE: break;
    }
    return {};
}

// A context object in a section, either in the main sections (bOverlay
// false) or in the platform's overlay.
CYPHER_NODISCARD const key_value_t *SectionContext(
    const key_value_t *pKeymapRoot,
    keymap_section_t section,
    keymap_platform_t platform,
    bool_t bOverlay,
    string_view_t context ) noexcept
{
    const key_value_t *pBase = pKeymapRoot;
    if ( bOverlay ) {
        if ( platform == keymap_platform_t::NONE ) { return nullptr; }
        const key_value_t *pPlatforms = KeyValue_Find( pKeymapRoot, KeyText( "platforms" ) );
        pBase = KeyValue_Type( pPlatforms ) == key_value_type_t::OBJECT ? KeyValue_Find( pPlatforms, PlatformName( platform ) ) : nullptr;
        if ( KeyValue_Type( pBase ) != key_value_type_t::OBJECT ) { return nullptr; }
    }
    const key_value_t *pSection = KeyValue_Find( pBase, SectionName( section ) );
    if ( KeyValue_Type( pSection ) != key_value_type_t::OBJECT ) { return nullptr; }
    const key_value_t *pContext = KeyValue_Find( pSection, context );
    return KeyValue_Type( pContext ) == key_value_type_t::OBJECT ? pContext : nullptr;
}

// The entry that decides an ID within one keymap: the platform overlay's if
// it has one, else the main section's.
CYPHER_NODISCARD const key_value_t *DecidingEntry(
    const key_value_t *pKeymapRoot,
    keymap_section_t section,
    keymap_platform_t platform,
    string_view_t context,
    string_view_t id ) noexcept
{
    const key_value_t *pOverlay = SectionContext( pKeymapRoot, section, platform, CY_TRUE, context );
    const key_value_t *pEntry = pOverlay != nullptr ? KeyValue_Find( pOverlay, id ) : nullptr;
    if ( pEntry != nullptr ) { return pEntry; }
    const key_value_t *pMain = SectionContext( pKeymapRoot, section, platform, CY_FALSE, context );
    return pMain != nullptr ? KeyValue_Find( pMain, id ) : nullptr;
}

struct gesture_name_t {
    const char *pName;
    u8 value;
};

inline constexpr gesture_name_t kButtons[]{
    { "Left", MOUSE_BUTTON_LEFT }, { "Right", MOUSE_BUTTON_RIGHT }, { "Middle", MOUSE_BUTTON_MIDDLE },
    { "Back", MOUSE_BUTTON_BACK }, { "Forward", MOUSE_BUTTON_FORWARD },
};

inline constexpr gesture_name_t kActions[]{
    { "DoubleClick", MOUSE_ACTION_DOUBLE_CLICK }, { "Click", MOUSE_ACTION_CLICK }, { "Drag", MOUSE_ACTION_DRAG },
    { "Press", MOUSE_ACTION_PRESS },
};

inline constexpr gesture_name_t kWheels[]{
    { "WheelUp", MOUSE_ACTION_WHEEL_UP }, { "WheelDown", MOUSE_ACTION_WHEEL_DOWN }, { "WheelLeft", MOUSE_ACTION_WHEEL_LEFT },
    { "WheelRight", MOUSE_ACTION_WHEEL_RIGHT }, { "Wheel", MOUSE_ACTION_WHEEL },
};

CYPHER_NODISCARD u16 ModifierKey( u8 modifier ) noexcept
{
    switch ( modifier ) {
        case KEY_MODIFIER_CTRL: return KEY_MODIFIER_KEY_CTRL;
        case KEY_MODIFIER_ALT: return KEY_MODIFIER_KEY_ALT;
        case KEY_MODIFIER_SHIFT: return KEY_MODIFIER_KEY_SHIFT;
        case KEY_MODIFIER_META: return KEY_MODIFIER_KEY_META;
        default: return KEY_NONE;
    }
}

CYPHER_NODISCARD const char *ModifierKeyName( u16 key ) noexcept
{
    switch ( key ) {
        case KEY_MODIFIER_KEY_CTRL: return "Ctrl";
        case KEY_MODIFIER_KEY_ALT: return "Alt";
        case KEY_MODIFIER_KEY_SHIFT: return "Shift";
        case KEY_MODIFIER_KEY_META: return "Meta";
        default: return nullptr;
    }
}

CYPHER_NODISCARD bool_t ValidTrigger( keymap_section_t section, string_view_t text, char ( &canonical )[EDITOR_KEY_CHORD_TEXT_CAPACITY], usize &cchOut ) noexcept;

} // namespace

bool_t EditorKeyChord_Parse( string_view_t text, key_chord_t *pChordOut ) noexcept
{
    if ( pChordOut == nullptr || text.pData == nullptr ) { return CY_FALSE; }
    key_chord_t chord{};
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= text.cchLength; ++iChar ) {
        // Strokes are separated by a comma followed by a space; a comma
        // elsewhere is the comma key itself (as in "Ctrl+,").
        const bool_t bEnd = iChar == text.cchLength;
        const bool_t bSeparator = !bEnd && text.pData[iChar] == ',' && iChar + 1u < text.cchLength &&
                                  text.pData[iChar + 1u] == ' ';
        if ( !bEnd && !bSeparator ) { continue; }
        if ( chord.nStrokes == EDITOR_KEY_CHORD_MAX_STROKES ||
             !ParseStroke( { text.pData + iStart, iChar - iStart }, chord.strokes[chord.nStrokes] ) ) {
            return CY_FALSE;
        }
        ++chord.nStrokes;
        iStart = iChar + 1u;
    }
    *pChordOut = chord;
    return CY_TRUE;
}

usize EditorKeyChord_Format( const key_chord_t &chord, char ( &buffer )[EDITOR_KEY_CHORD_TEXT_CAPACITY] ) noexcept
{
    usize cch = 0u;
    const auto append = [&]( const char *pText ) noexcept {
        for ( ; *pText != '\0'; ++pText ) {
            if ( cch + 1u >= EDITOR_KEY_CHORD_TEXT_CAPACITY ) { return CY_FALSE; }
            buffer[cch++] = *pText;
        }
        return CY_TRUE;
    };
    buffer[0] = '\0';
    if ( chord.nStrokes == 0u || chord.nStrokes > EDITOR_KEY_CHORD_MAX_STROKES ) { return 0u; }
    for ( usize iStroke = 0u; iStroke < chord.nStrokes; ++iStroke ) {
        const key_stroke_t &stroke = chord.strokes[iStroke];
        char scratch[8]{};
        const char *pKey = CanonicalKeyName( stroke.key, scratch );
        const bool_t bOk = pKey != nullptr &&
            ( iStroke == 0u || append( ", " ) ) &&
            ( ( stroke.modifiers & KEY_MODIFIER_CTRL ) == 0u || append( "Ctrl+" ) ) &&
            ( ( stroke.modifiers & KEY_MODIFIER_ALT ) == 0u || append( "Alt+" ) ) &&
            ( ( stroke.modifiers & KEY_MODIFIER_SHIFT ) == 0u || append( "Shift+" ) ) &&
            ( ( stroke.modifiers & KEY_MODIFIER_META ) == 0u || append( "Meta+" ) ) &&
            append( pKey );
        if ( !bOk ) {
            buffer[0] = '\0';
            return 0u;
        }
    }
    buffer[cch] = '\0';
    return cch;
}

bool_t EditorKeyChord_Equals( const key_chord_t &a, const key_chord_t &b ) noexcept
{
    if ( a.nStrokes != b.nStrokes ) { return CY_FALSE; }
    for ( usize iStroke = 0u; iStroke < a.nStrokes; ++iStroke ) {
        if ( a.strokes[iStroke].modifiers != b.strokes[iStroke].modifiers ||
             a.strokes[iStroke].key != b.strokes[iStroke].key ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t EditorHeldKey_Parse( string_view_t text, key_stroke_t *pStrokeOut ) noexcept
{
    if ( pStrokeOut == nullptr || text.pData == nullptr ) { return CY_FALSE; }
    const string_view_t trimmed = Trim( text );
    // A lone modifier is the key; otherwise it is a normal stroke.
    const u8 lone = ParseModifier( trimmed );
    if ( lone != KEY_MODIFIER_NONE ) {
        *pStrokeOut = key_stroke_t{ KEY_MODIFIER_NONE, ModifierKey( lone ) };
        return CY_TRUE;
    }
    key_stroke_t stroke{};
    if ( !ParseStroke( trimmed, stroke ) ) { return CY_FALSE; }
    *pStrokeOut = stroke;
    return CY_TRUE;
}

usize EditorHeldKey_Format( const key_stroke_t &stroke, char ( &buffer )[EDITOR_KEY_CHORD_TEXT_CAPACITY] ) noexcept
{
    if ( const char *pModifier = ModifierKeyName( stroke.key ) ) {
        usize cch = 0u;
        for ( ; pModifier[cch] != '\0'; ++cch ) { buffer[cch] = pModifier[cch]; }
        buffer[cch] = '\0';
        return cch;
    }
    key_chord_t chord{};
    chord.strokes[0] = stroke;
    chord.nStrokes = 1u;
    return EditorKeyChord_Format( chord, buffer );
}

bool_t EditorMouseGesture_Parse( string_view_t text, mouse_gesture_t *pGestureOut ) noexcept
{
    if ( pGestureOut == nullptr || text.pData == nullptr ) { return CY_FALSE; }
    text = Trim( text );
    if ( text.cchLength == 0u ) { return CY_FALSE; }
    mouse_gesture_t gesture{};
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= text.cchLength; ++iChar ) {
        if ( iChar < text.cchLength && text.pData[iChar] != '+' ) { continue; }
        const string_view_t token{ text.pData + iStart, iChar - iStart };
        iStart = iChar + 1u;
        if ( token.cchLength == 0u ) { return CY_FALSE; }
        if ( iChar < text.cchLength ) {
            // Prefix: a modifier, or the one key held during the gesture.
            const u8 modifier = ParseModifier( token );
            if ( modifier != KEY_MODIFIER_NONE ) {
                gesture.modifiers = static_cast<u8>( gesture.modifiers | modifier );
                continue;
            }
            const u16 key = ParseKey( token );
            if ( key == KEY_NONE || gesture.heldKey != KEY_NONE ) { return CY_FALSE; }
            gesture.heldKey = key;
            continue;
        }
        // Last token: a wheel, or a button followed by an action.
        for ( const gesture_name_t &wheel : kWheels ) {
            if ( EqualsIgnoreCase( token, wheel.pName ) ) {
                gesture.button = MOUSE_BUTTON_NONE;
                gesture.action = wheel.value;
                *pGestureOut = gesture;
                return CY_TRUE;
            }
        }
        for ( const gesture_name_t &button : kButtons ) {
            const usize cchButton = std::char_traits<char>::length( button.pName );
            if ( token.cchLength <= cchButton || !EqualsIgnoreCase( { token.pData, cchButton }, button.pName ) ) { continue; }
            const string_view_t actionText{ token.pData + cchButton, token.cchLength - cchButton };
            for ( const gesture_name_t &action : kActions ) {
                if ( EqualsIgnoreCase( actionText, action.pName ) ) {
                    gesture.button = button.value;
                    gesture.action = action.value;
                    *pGestureOut = gesture;
                    return CY_TRUE;
                }
            }
        }
        return CY_FALSE;
    }
    return CY_FALSE;
}

usize EditorMouseGesture_Format( const mouse_gesture_t &gesture, char ( &buffer )[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY] ) noexcept
{
    usize cch = 0u;
    const auto append = [&]( const char *pText ) noexcept {
        for ( ; pText != nullptr && *pText != '\0'; ++pText ) {
            if ( cch + 1u >= EDITOR_MOUSE_GESTURE_TEXT_CAPACITY ) { return CY_FALSE; }
            buffer[cch++] = *pText;
        }
        return pText != nullptr;
    };
    buffer[0] = '\0';
    const char *pButton = nullptr;
    const char *pAction = nullptr;
    for ( const gesture_name_t &button : kButtons ) { if ( button.value == gesture.button ) { pButton = button.pName; } }
    for ( const gesture_name_t &action : kActions ) { if ( action.value == gesture.action ) { pAction = action.pName; } }
    for ( const gesture_name_t &wheel : kWheels ) { if ( wheel.value == gesture.action ) { pAction = wheel.pName; } }
    const bool_t bWheel = gesture.action >= MOUSE_ACTION_WHEEL;
    if ( pAction == nullptr || ( bWheel != ( gesture.button == MOUSE_BUTTON_NONE ) ) || ( !bWheel && pButton == nullptr ) ) { return 0u; }
    char scratch[8]{};
    const char *pHeld = gesture.heldKey != KEY_NONE ? CanonicalKeyName( gesture.heldKey, scratch ) : nullptr;
    const bool_t bOk = ( ( gesture.modifiers & KEY_MODIFIER_CTRL ) == 0u || append( "Ctrl+" ) ) &&
                       ( ( gesture.modifiers & KEY_MODIFIER_ALT ) == 0u || append( "Alt+" ) ) &&
                       ( ( gesture.modifiers & KEY_MODIFIER_SHIFT ) == 0u || append( "Shift+" ) ) &&
                       ( ( gesture.modifiers & KEY_MODIFIER_META ) == 0u || append( "Meta+" ) ) &&
                       ( gesture.heldKey == KEY_NONE || ( append( pHeld ) && append( "+" ) ) ) &&
                       ( bWheel || append( pButton ) ) && append( pAction );
    if ( !bOk ) {
        buffer[0] = '\0';
        return 0u;
    }
    buffer[cch] = '\0';
    return cch;
}

bool_t EditorMouseGesture_Equals( const mouse_gesture_t &a, const mouse_gesture_t &b ) noexcept
{
    return a.modifiers == b.modifiers && a.heldKey == b.heldKey && a.button == b.button && a.action == b.action;
}

keymap_platform_t EditorKeymap_HostPlatform() noexcept
{
#if defined( __APPLE__ )
    return keymap_platform_t::MACOS;
#elif defined( _WIN32 )
    return keymap_platform_t::WINDOWS;
#else
    return keymap_platform_t::LINUX;
#endif
}

settings_document_identity_t EditorKeymap_Identity() noexcept
{
    return { KeyText( "cypher.editor_keymap" ), EDITOR_KEYMAP_OLDEST_SCHEMA_VERSION, EDITOR_KEYMAP_SCHEMA_VERSION };
}

keymap_header_t EditorKeymap_Header( const key_value_t *pKeymapRoot ) noexcept
{
    keymap_header_t header{};
    string_view_t text{};
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "id" ) ), &text ) && IsHeaderId( text ) ) { header.id = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "name" ) ), &text ) && text.cchLength <= 128u ) { header.name = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "base" ) ), &text ) && IsHeaderId( text ) ) { header.base = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "author" ) ), &text ) && text.cchLength <= 128u ) { header.author = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "description" ) ), &text ) && text.cchLength <= 1024u ) {
        header.description = text;
    }
    return header;
}

keymap_lookup_t EditorKeymap_FindBinding(
    const key_value_t *const *ppChain,
    usize nChain,
    string_view_t context,
    string_view_t command,
    keymap_binding_t *pBindingOut ) noexcept
{
    return EditorKeymap_FindBindingOn( ppChain, nChain, keymap_platform_t::NONE, context, command, pBindingOut );
}

keymap_lookup_t EditorKeymap_FindBindingOn(
    const key_value_t *const *ppChain,
    usize nChain,
    keymap_platform_t platform,
    string_view_t context,
    string_view_t command,
    keymap_binding_t *pBindingOut ) noexcept
{
    keymap_binding_t binding{};
    usize nInvalid = 0u;
    for ( usize iKeymap = 0u; ppChain != nullptr && iKeymap < nChain && iKeymap < EDITOR_KEYMAP_MAX_DEPTH; ++iKeymap ) {
        const entry_t entry = ReadEntry( DecidingEntry( ppChain[iKeymap], keymap_section_t::BINDINGS, platform, context, command ), binding );
        nInvalid += binding.nInvalidChords;
        if ( entry == entry_t::ABSENT || entry == entry_t::INVALID ) {
            continue;
        }
        binding.iSource = iKeymap;
        binding.nInvalidChords = nInvalid;
        if ( pBindingOut != nullptr ) { *pBindingOut = binding; }
        return entry == entry_t::BOUND ? keymap_lookup_t::BOUND : keymap_lookup_t::UNBOUND;
    }
    if ( pBindingOut != nullptr ) {
        *pBindingOut = {};
        pBindingOut->nInvalidChords = nInvalid;
    }
    return keymap_lookup_t::NOT_DEFINED;
}

keymap_lookup_t EditorKeymap_FindTriggers(
    const key_value_t *const *ppChain,
    usize nChain,
    keymap_section_t section,
    keymap_platform_t platform,
    string_view_t context,
    string_view_t id,
    keymap_triggers_t *pTriggersOut ) noexcept
{
    keymap_triggers_t triggers{};
    for ( usize iKeymap = 0u; ppChain != nullptr && iKeymap < nChain && iKeymap < EDITOR_KEYMAP_MAX_DEPTH; ++iKeymap ) {
        const key_value_t *pEntry = DecidingEntry( ppChain[iKeymap], section, platform, context, id );
        if ( KeyValue_Type( pEntry ) != key_value_type_t::ARRAY ) { continue; }
        for ( usize i = 0u; i < KeyValue_ChildCount( pEntry ) && triggers.nTexts < EDITOR_KEYMAP_MAX_CHORDS; ++i ) {
            string_view_t text{};
            if ( KeyValue_GetString( KeyValue_ChildAt( pEntry, i ), &text ) ) { triggers.texts[triggers.nTexts++] = text; }
        }
        triggers.iSource = iKeymap;
        if ( pTriggersOut != nullptr ) { *pTriggersOut = triggers; }
        return triggers.nTexts != 0u ? keymap_lookup_t::BOUND : keymap_lookup_t::UNBOUND;
    }
    if ( pTriggersOut != nullptr ) { *pTriggersOut = {}; }
    return keymap_lookup_t::NOT_DEFINED;
}

namespace
{

CYPHER_NODISCARD string_view_t FindCommandOn(
    const key_value_t *const *ppChain,
    usize nChain,
    keymap_platform_t platform,
    string_view_t context,
    const key_chord_t &chord ) noexcept
{
    for ( usize iKeymap = 0u; ppChain != nullptr && iKeymap < nChain && iKeymap < EDITOR_KEYMAP_MAX_DEPTH; ++iKeymap ) {
        for ( const bool_t bOverlay : { CY_TRUE, CY_FALSE } ) {
            const key_value_t *pContext = SectionContext( ppChain[iKeymap], keymap_section_t::BINDINGS, platform, bOverlay, context );
            for ( usize iEntry = 0u; pContext != nullptr && iEntry < KeyValue_ChildCount( pContext ); ++iEntry ) {
                const string_view_t command = KeyValue_Name( KeyValue_ChildAt( pContext, iEntry ) );
                keymap_binding_t binding{};
                // Only the keymap that decides a command's binding may claim the chord.
                if ( EditorKeymap_FindBindingOn( ppChain, nChain, platform, context, command, &binding ) == keymap_lookup_t::BOUND &&
                     binding.iSource == iKeymap && BindingHas( binding, chord ) ) {
                    return command;
                }
            }
        }
    }
    return {};
}

} // namespace

string_view_t EditorKeymap_FindCommandInStack(
    const key_value_t *const *ppChain,
    usize nChain,
    keymap_platform_t platform,
    const string_view_t *pContexts,
    usize nContexts,
    const key_chord_t &chord ) noexcept
{
    for ( usize i = 0u; pContexts != nullptr && i < nContexts && i < EDITOR_KEYMAP_MAX_CONTEXT_STACK; ++i ) {
        const string_view_t command = FindCommandOn( ppChain, nChain, platform, pContexts[i], chord );
        if ( command.cchLength != 0u ) { return command; }
    }
    return {};
}

string_view_t EditorKeymap_FindCommand(
    const key_value_t *const *ppChain,
    usize nChain,
    string_view_t context,
    const key_chord_t &chord ) noexcept
{
    return FindCommandOn( ppChain, nChain, keymap_platform_t::NONE, context, chord );
}

usize EditorKeymap_FindConflicts(
    const key_value_t *const *ppChain,
    usize nChain,
    string_view_t context,
    keymap_conflict_t *pConflicts,
    usize nCapacity ) noexcept
{
    // Each command is judged by the keymap that decides it, so a command
    // overridden by a more specific keymap never conflicts through its base.
    usize nConflicts = 0u;
    const usize nKeymaps = nChain < EDITOR_KEYMAP_MAX_DEPTH ? nChain : EDITOR_KEYMAP_MAX_DEPTH;
    for ( usize iKeymapA = 0u; ppChain != nullptr && iKeymapA < nKeymaps; ++iKeymapA ) {
        const key_value_t *pContextA = FindContext( ppChain[iKeymapA], context );
        for ( usize iEntryA = 0u; pContextA != nullptr && iEntryA < KeyValue_ChildCount( pContextA ); ++iEntryA ) {
            const string_view_t commandA = KeyValue_Name( KeyValue_ChildAt( pContextA, iEntryA ) );
            keymap_binding_t bindingA{};
            if ( EditorKeymap_FindBinding( ppChain, nChain, context, commandA, &bindingA ) != keymap_lookup_t::BOUND ||
                 bindingA.iSource != iKeymapA ) {
                continue;
            }
            // Compare against every later deciding entry (same keymap after
            // this one, then wider keymaps), so each pair is reported once.
            for ( usize iKeymapB = iKeymapA; iKeymapB < nKeymaps; ++iKeymapB ) {
                const key_value_t *pContextB = FindContext( ppChain[iKeymapB], context );
                const usize iFirst = iKeymapB == iKeymapA ? iEntryA + 1u : 0u;
                for ( usize iEntryB = iFirst; pContextB != nullptr && iEntryB < KeyValue_ChildCount( pContextB ); ++iEntryB ) {
                    const string_view_t commandB = KeyValue_Name( KeyValue_ChildAt( pContextB, iEntryB ) );
                    if ( StringView_Equals( commandA, commandB ) ) { continue; }
                    keymap_binding_t bindingB{};
                    if ( EditorKeymap_FindBinding( ppChain, nChain, context, commandB, &bindingB ) != keymap_lookup_t::BOUND ||
                         bindingB.iSource != iKeymapB ) {
                        continue;
                    }
                    for ( usize iChord = 0u; iChord < bindingA.nChords; ++iChord ) {
                        if ( BindingHas( bindingB, bindingA.chords[iChord] ) ) {
                            if ( pConflicts != nullptr && nConflicts < nCapacity ) {
                                pConflicts[nConflicts] = { commandA, commandB, bindingA.chords[iChord] };
                            }
                            ++nConflicts;
                        }
                    }
                }
            }
        }
    }
    return nConflicts;
}

keymap_status_t EditorKeymap_SetBinding(
    settings_document_t *pKeymap,
    string_view_t context,
    string_view_t command,
    const key_chord_t *pChords,
    usize nChords ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pKeymap ) || !IsContextId( context ) || !EditorCommand_IsValidId( command ) ||
         nChords > EDITOR_KEYMAP_MAX_CHORDS || ( pChords == nullptr && nChords != 0u ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    char texts[EDITOR_KEYMAP_MAX_CHORDS][EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    usize lengths[EDITOR_KEYMAP_MAX_CHORDS]{};
    for ( usize iChord = 0u; iChord < nChords; ++iChord ) {
        lengths[iChord] = EditorKeyChord_Format( pChords[iChord], texts[iChord] );
        if ( lengths[iChord] == 0u ) { return keymap_status_t::INVALID_ARGUMENT; }
    }
    settings_path_t path{};
    if ( !SettingsPath_Append( &path, KeyText( "bindings" ) ) || !SettingsPath_Append( &path, context ) ||
         !SettingsPath_Append( &path, command ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    key_value_t *pEntry = nullptr;
    const keymap_status_t ensured = FromStore( SettingsDocument_Ensure( pKeymap, path, &pEntry ) );
    if ( ensured != keymap_status_t::OK ) { return ensured; }
    key_value_document_t *pDocument = pKeymap->pDocument;
    if ( !KeyValue_SetContainerType( pDocument, pEntry, key_value_type_t::ARRAY ) ) { return keymap_status_t::OUT_OF_MEMORY; }
    for ( usize iChord = 0u; iChord < nChords; ++iChord ) {
        key_value_t *pElement = KeyValue_ArrayAppend( pDocument, pEntry, key_value_type_t::NULL_VALUE );
        if ( pElement == nullptr || !KeyValue_SetString( pDocument, pElement, { texts[iChord], lengths[iChord] } ) ) {
            return keymap_status_t::OUT_OF_MEMORY;
        }
    }
    return keymap_status_t::OK;
}

namespace
{

bool_t ValidTrigger( keymap_section_t section, string_view_t text, char ( &canonical )[EDITOR_KEY_CHORD_TEXT_CAPACITY], usize &cchOut ) noexcept
{
    cchOut = 0u;
    switch ( section ) {
        case keymap_section_t::BINDINGS: {
            key_chord_t chord{};
            if ( EditorKeyChord_Parse( text, &chord ) ) { cchOut = EditorKeyChord_Format( chord, canonical ); }
            break;
        }
        case keymap_section_t::HELD: {
            key_stroke_t stroke{};
            if ( EditorHeldKey_Parse( text, &stroke ) ) { cchOut = EditorHeldKey_Format( stroke, canonical ); }
            break;
        }
        case keymap_section_t::MOUSE: {
            mouse_gesture_t gesture{};
            char gestureText[EDITOR_MOUSE_GESTURE_TEXT_CAPACITY]{};
            if ( EditorMouseGesture_Parse( text, &gesture ) ) {
                cchOut = EditorMouseGesture_Format( gesture, gestureText );
                for ( usize i = 0u; i <= cchOut; ++i ) { canonical[i] = gestureText[i]; }
            }
            break;
        }
    }
    return cchOut != 0u;
}

} // namespace

namespace
{

// The path of one entry: `<section>.<context>.<id>`, or under
// `platforms.<platform>` for an overlay.
CYPHER_NODISCARD bool_t EntryPath(
    keymap_platform_t platform,
    keymap_section_t section,
    string_view_t context,
    string_view_t id,
    settings_path_t &pathOut ) noexcept
{
    pathOut = settings_path_t{};
    if ( platform != keymap_platform_t::NONE &&
         ( !SettingsPath_Append( &pathOut, KeyText( "platforms" ) ) || !SettingsPath_Append( &pathOut, PlatformName( platform ) ) ) ) {
        return CY_FALSE;
    }
    return SettingsPath_Append( &pathOut, SectionName( section ) ) && SettingsPath_Append( &pathOut, context ) && SettingsPath_Append( &pathOut, id );
}

// A section object (context to entries), main or overlay.
CYPHER_NODISCARD const key_value_t *SectionObject( const key_value_t *pKeymapRoot, keymap_section_t section, keymap_platform_t platform ) noexcept
{
    const key_value_t *pBase = pKeymapRoot;
    if ( platform != keymap_platform_t::NONE ) {
        const key_value_t *pPlatforms = KeyValue_Find( pKeymapRoot, KeyText( "platforms" ) );
        pBase = KeyValue_Type( pPlatforms ) == key_value_type_t::OBJECT ? KeyValue_Find( pPlatforms, PlatformName( platform ) ) : nullptr;
        if ( KeyValue_Type( pBase ) != key_value_type_t::OBJECT ) { return nullptr; }
    }
    const key_value_t *pSection = KeyValue_Find( pBase, SectionName( section ) );
    return KeyValue_Type( pSection ) == key_value_type_t::OBJECT ? pSection : nullptr;
}

// One entry's triggers in canonical form. Like ReadEntry: an entry whose
// texts are all unusable is invalid, so a wider keymap decides instead.
struct trigger_texts_t {
    char texts[EDITOR_KEYMAP_MAX_CHORDS][EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    usize lengths[EDITOR_KEYMAP_MAX_CHORDS]{};
    usize nTexts{ 0u };
};

CYPHER_NODISCARD entry_t ReadTriggerEntry( keymap_section_t section, const key_value_t *pEntry, trigger_texts_t &textsOut ) noexcept
{
    textsOut.nTexts = 0u;
    if ( pEntry == nullptr ) { return entry_t::ABSENT; }
    if ( KeyValue_Type( pEntry ) != key_value_type_t::ARRAY ) { return entry_t::INVALID; }
    const usize nElements = KeyValue_ChildCount( pEntry );
    if ( nElements == 0u ) { return entry_t::UNBOUND; }
    for ( usize i = 0u; i < nElements && textsOut.nTexts < EDITOR_KEYMAP_MAX_CHORDS; ++i ) {
        string_view_t text{};
        if ( KeyValue_GetString( KeyValue_ChildAt( pEntry, i ), &text ) &&
             ValidTrigger( section, text, textsOut.texts[textsOut.nTexts], textsOut.lengths[textsOut.nTexts] ) ) {
            ++textsOut.nTexts;
        }
    }
    return textsOut.nTexts != 0u ? entry_t::BOUND : entry_t::INVALID;
}

CYPHER_NODISCARD const key_value_t *EntryIn( const key_value_t *pKeymapRoot, keymap_section_t section, keymap_platform_t platform,
                                             string_view_t context, string_view_t id ) noexcept
{
    const key_value_t *pContext = SectionContext( pKeymapRoot, section, platform, platform != keymap_platform_t::NONE, context );
    return pContext != nullptr ? KeyValue_Find( pContext, id ) : nullptr;
}

} // namespace

keymap_status_t EditorKeymap_SetTriggers(
    settings_document_t *pKeymap,
    keymap_section_t section,
    string_view_t context,
    string_view_t id,
    const string_view_t *pTexts,
    usize nTexts ) noexcept
{
    return EditorKeymap_SetTriggersOn( pKeymap, keymap_platform_t::NONE, section, context, id, pTexts, nTexts );
}

keymap_status_t EditorKeymap_SetTriggersOn(
    settings_document_t *pKeymap,
    keymap_platform_t platform,
    keymap_section_t section,
    string_view_t context,
    string_view_t id,
    const string_view_t *pTexts,
    usize nTexts ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pKeymap ) || !IsContextId( context ) || !EditorCommand_IsValidId( id ) ||
         nTexts > EDITOR_KEYMAP_MAX_CHORDS || ( pTexts == nullptr && nTexts != 0u ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    char texts[EDITOR_KEYMAP_MAX_CHORDS][EDITOR_KEY_CHORD_TEXT_CAPACITY]{};
    usize lengths[EDITOR_KEYMAP_MAX_CHORDS]{};
    for ( usize i = 0u; i < nTexts; ++i ) {
        if ( !ValidTrigger( section, pTexts[i], texts[i], lengths[i] ) ) { return keymap_status_t::INVALID_ARGUMENT; }
    }
    settings_path_t path{};
    if ( !EntryPath( platform, section, context, id, path ) ) { return keymap_status_t::INVALID_ARGUMENT; }
    // Replace, not append: remove any earlier value first.
    const keymap_status_t removed = FromStore( SettingsDocument_Remove( pKeymap, path ) );
    if ( removed != keymap_status_t::OK ) { return removed; }
    key_value_t *pEntry = nullptr;
    const keymap_status_t ensured = FromStore( SettingsDocument_Ensure( pKeymap, path, &pEntry ) );
    if ( ensured != keymap_status_t::OK ) { return ensured; }
    key_value_document_t *pDocument = pKeymap->pDocument;
    if ( !KeyValue_SetContainerType( pDocument, pEntry, key_value_type_t::ARRAY ) ) { return keymap_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < nTexts; ++i ) {
        key_value_t *pElement = KeyValue_ArrayAppend( pDocument, pEntry, key_value_type_t::NULL_VALUE );
        if ( pElement == nullptr || !KeyValue_SetString( pDocument, pElement, { texts[i], lengths[i] } ) ) { return keymap_status_t::OUT_OF_MEMORY; }
    }
    return keymap_status_t::OK;
}

keymap_status_t EditorKeymap_ResetOn(
    settings_document_t *pKeymap,
    keymap_platform_t platform,
    keymap_section_t section,
    string_view_t context,
    string_view_t id ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pKeymap ) || !IsContextId( context ) || !EditorCommand_IsValidId( id ) ||
         !EntryPath( platform, section, context, id, path ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    return FromStore( SettingsDocument_Remove( pKeymap, path ) );
}

keymap_status_t EditorKeymap_SetHeader( settings_document_t *pKeymap, string_view_t id, string_view_t name, string_view_t base ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pKeymap ) || !IsHeaderId( id ) || name.cchLength == 0u || name.cchLength > 128u ||
         ( base.cchLength != 0u && !IsHeaderId( base ) ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    const struct { const char *pKey; string_view_t value; } fields[]{ { "id", id }, { "name", name }, { "base", base } };
    for ( const auto &field : fields ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.pKey ) ) ) { return keymap_status_t::INVALID_ARGUMENT; }
        if ( field.value.cchLength == 0u ) {
            const keymap_status_t removed = FromStore( SettingsDocument_Remove( pKeymap, path ) );
            if ( removed != keymap_status_t::OK ) { return removed; }
            continue;
        }
        key_value_t *pNode = nullptr;
        const keymap_status_t ensured = FromStore( SettingsDocument_Ensure( pKeymap, path, &pNode ) );
        if ( ensured != keymap_status_t::OK ) { return ensured; }
        if ( !KeyValue_SetString( pKeymap->pDocument, pNode, field.value ) ) { return keymap_status_t::OUT_OF_MEMORY; }
    }
    return keymap_status_t::OK;
}

usize EditorKeymap_BuildChain(
    const key_value_t *const *ppLibrary,
    usize nLibrary,
    const key_value_t *pRoot,
    const key_value_t **ppChainOut,
    usize nCapacity,
    bool_t *pbCompleteOut ) noexcept
{
    bool_t bComplete = CY_TRUE;
    usize nChain = 0u;
    const usize nMax = nCapacity < EDITOR_KEYMAP_MAX_DEPTH ? nCapacity : EDITOR_KEYMAP_MAX_DEPTH;
    const key_value_t *pCurrent = ppChainOut != nullptr ? pRoot : nullptr;
    while ( pCurrent != nullptr && nChain < nMax ) {
        for ( usize i = 0u; i < nChain; ++i ) {
            if ( ppChainOut[i] == pCurrent ) { pCurrent = nullptr; } // A cycle: stop before repeating.
        }
        if ( pCurrent == nullptr ) {
            bComplete = CY_FALSE;
            break;
        }
        ppChainOut[nChain++] = pCurrent;
        const string_view_t base = EditorKeymap_Header( pCurrent ).base;
        pCurrent = nullptr;
        if ( base.cchLength == 0u ) { break; }
        for ( usize i = 0u; ppLibrary != nullptr && i < nLibrary; ++i ) {
            if ( StringView_Equals( EditorKeymap_Header( ppLibrary[i] ).id, base ) ) {
                pCurrent = ppLibrary[i];
                break;
            }
        }
        if ( pCurrent == nullptr ) { bComplete = CY_FALSE; } // The base is not installed.
    }
    if ( pCurrent != nullptr ) { bComplete = CY_FALSE; } // Cut at the depth bound.
    if ( pbCompleteOut != nullptr ) { *pbCompleteOut = bComplete; }
    return nChain;
}

keymap_status_t EditorKeymap_WriteComplete( settings_document_t *pKeymap, const key_value_t *const *ppChain, usize nChain ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pKeymap ) || ( ppChain == nullptr && nChain != 0u ) ) { return keymap_status_t::INVALID_ARGUMENT; }
    const usize nKeymaps = nChain < EDITOR_KEYMAP_MAX_DEPTH ? nChain : EDITOR_KEYMAP_MAX_DEPTH;
    constexpr keymap_section_t kSections[]{ keymap_section_t::BINDINGS, keymap_section_t::HELD, keymap_section_t::MOUSE };
    constexpr keymap_platform_t kLayers[]{ keymap_platform_t::NONE, keymap_platform_t::MACOS, keymap_platform_t::WINDOWS, keymap_platform_t::LINUX };
    for ( const keymap_section_t section : kSections ) {
        for ( const keymap_platform_t layer : kLayers ) {
            // Widest keymap first, so the output keeps the catalogue order of
            // the built-in keymap and more specific keymaps only add to it.
            for ( usize iSource = nKeymaps; iSource-- > 0u; ) {
                const key_value_t *pSection = SectionObject( ppChain[iSource], section, layer );
                for ( usize iContext = 0u; pSection != nullptr && iContext < KeyValue_ChildCount( pSection ); ++iContext ) {
                    const key_value_t *pContext = KeyValue_ChildAt( pSection, iContext );
                    const string_view_t context = KeyValue_Name( pContext );
                    if ( KeyValue_Type( pContext ) != key_value_type_t::OBJECT || !IsContextId( context ) ) { continue; }
                    for ( usize iEntry = 0u; iEntry < KeyValue_ChildCount( pContext ); ++iEntry ) {
                        const string_view_t id = KeyValue_Name( KeyValue_ChildAt( pContext, iEntry ) );
                        if ( !EditorCommand_IsValidId( id ) || EntryIn( SettingsDocument_Root( pKeymap ), section, layer, context, id ) != nullptr ) {
                            continue;
                        }
                        // The keymap that decides the entry on this layer: on a
                        // platform, its overlay or, failing that, its main entry
                        // (DecidingEntry's rule). A main-section decision needs
                        // no overlay: the flattened main section already has it.
                        trigger_texts_t decided{};
                        bool_t bFound = CY_FALSE;
                        bool_t bOverlay = CY_FALSE;
                        for ( usize iKeymap = 0u; iKeymap < nKeymaps && !bFound; ++iKeymap ) {
                            if ( layer != keymap_platform_t::NONE ) {
                                const entry_t overlay = ReadTriggerEntry( section, EntryIn( ppChain[iKeymap], section, layer, context, id ), decided );
                                if ( overlay == entry_t::BOUND || overlay == entry_t::UNBOUND ) {
                                    bFound = CY_TRUE;
                                    bOverlay = CY_TRUE;
                                    break;
                                }
                            }
                            const entry_t main = ReadTriggerEntry( section, EntryIn( ppChain[iKeymap], section, keymap_platform_t::NONE, context, id ), decided );
                            bFound = main == entry_t::BOUND || main == entry_t::UNBOUND;
                        }
                        if ( !bFound || ( layer != keymap_platform_t::NONE && !bOverlay ) ) { continue; }
                        string_view_t views[EDITOR_KEYMAP_MAX_CHORDS]{};
                        for ( usize i = 0u; i < decided.nTexts; ++i ) { views[i] = { decided.texts[i], decided.lengths[i] }; }
                        const keymap_status_t status = EditorKeymap_SetTriggersOn( pKeymap, layer, section, context, id, views, decided.nTexts );
                        if ( status != keymap_status_t::OK ) { return status; }
                    }
                }
            }
        }
    }
    return keymap_status_t::OK;
}

keymap_status_t EditorKeymap_SetDetails( settings_document_t *pKeymap, string_view_t author, string_view_t description ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pKeymap ) || author.cchLength > 128u || description.cchLength > 1024u ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    const struct { const char *pKey; string_view_t value; } fields[]{ { "author", author }, { "description", description } };
    for ( const auto &field : fields ) {
        settings_path_t path{};
        if ( !SettingsPath_Append( &path, StringView_FromCString( field.pKey ) ) ) { return keymap_status_t::INVALID_ARGUMENT; }
        if ( field.value.cchLength == 0u ) {
            const keymap_status_t removed = FromStore( SettingsDocument_Remove( pKeymap, path ) );
            if ( removed != keymap_status_t::OK ) { return removed; }
            continue;
        }
        key_value_t *pNode = nullptr;
        const keymap_status_t ensured = FromStore( SettingsDocument_Ensure( pKeymap, path, &pNode ) );
        if ( ensured != keymap_status_t::OK ) { return ensured; }
        if ( !KeyValue_SetString( pKeymap->pDocument, pNode, field.value ) ) { return keymap_status_t::OUT_OF_MEMORY; }
    }
    return keymap_status_t::OK;
}

keymap_status_t EditorKeymap_ResetBinding(
    settings_document_t *pKeymap,
    string_view_t context,
    string_view_t command ) noexcept
{
    settings_path_t path{};
    if ( !SettingsDocument_IsInitialized( pKeymap ) || !IsContextId( context ) || !EditorCommand_IsValidId( command ) ||
         !SettingsPath_Append( &path, KeyText( "bindings" ) ) || !SettingsPath_Append( &path, context ) ||
         !SettingsPath_Append( &path, command ) ) {
        return keymap_status_t::INVALID_ARGUMENT;
    }
    return FromStore( SettingsDocument_Remove( pKeymap, path ) );
}

} // namespace cypher::editor
