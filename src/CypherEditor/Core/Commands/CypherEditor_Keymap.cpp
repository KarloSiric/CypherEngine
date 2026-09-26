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
    { "Backquote", '`' }, { "Minus", '-' }, { "Equal", '=' }, { "BracketLeft", '[' }, { "BracketRight", ']' },
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
        // A '+' that is the whole final token is not a separator; there is no
        // bare plus key, so this only guards against "Ctrl++".
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
    // Contexts use the same shape as command IDs, or a single identifier ("global").
    return EditorCommand_IsValidId( context ) ||
           DataValidation_Succeeded( DataValidation_CheckStableIdentifier( context, EDITOR_COMMAND_ID_MAX_LENGTH ) );
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

settings_document_identity_t EditorKeymap_Identity() noexcept
{
    return { KeyText( "cypher.editor_keymap" ), EDITOR_KEYMAP_SCHEMA_VERSION, EDITOR_KEYMAP_SCHEMA_VERSION };
}

keymap_header_t EditorKeymap_Header( const key_value_t *pKeymapRoot ) noexcept
{
    keymap_header_t header{};
    string_view_t text{};
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "id" ) ), &text ) && IsHeaderId( text ) ) { header.id = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "name" ) ), &text ) && text.cchLength <= 128u ) { header.name = text; }
    if ( KeyValue_GetString( KeyValue_Find( pKeymapRoot, KeyText( "base" ) ), &text ) && IsHeaderId( text ) ) { header.base = text; }
    return header;
}

keymap_lookup_t EditorKeymap_FindBinding(
    const key_value_t *const *ppChain,
    usize nChain,
    string_view_t context,
    string_view_t command,
    keymap_binding_t *pBindingOut ) noexcept
{
    keymap_binding_t binding{};
    usize nInvalid = 0u;
    for ( usize iKeymap = 0u; ppChain != nullptr && iKeymap < nChain && iKeymap < EDITOR_KEYMAP_MAX_DEPTH; ++iKeymap ) {
        const key_value_t *pContext = FindContext( ppChain[iKeymap], context );
        const entry_t entry = ReadEntry( pContext != nullptr ? KeyValue_Find( pContext, command ) : nullptr, binding );
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

string_view_t EditorKeymap_FindCommand(
    const key_value_t *const *ppChain,
    usize nChain,
    string_view_t context,
    const key_chord_t &chord ) noexcept
{
    for ( usize iKeymap = 0u; ppChain != nullptr && iKeymap < nChain && iKeymap < EDITOR_KEYMAP_MAX_DEPTH; ++iKeymap ) {
        const key_value_t *pContext = FindContext( ppChain[iKeymap], context );
        for ( usize iEntry = 0u; pContext != nullptr && iEntry < KeyValue_ChildCount( pContext ); ++iEntry ) {
            const string_view_t command = KeyValue_Name( KeyValue_ChildAt( pContext, iEntry ) );
            keymap_binding_t binding{};
            // Only the keymap that decides a command's binding may claim the chord.
            if ( EditorKeymap_FindBinding( ppChain, nChain, context, command, &binding ) == keymap_lookup_t::BOUND &&
                 binding.iSource == iKeymap && BindingHas( binding, chord ) ) {
                return command;
            }
        }
    }
    return {};
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
