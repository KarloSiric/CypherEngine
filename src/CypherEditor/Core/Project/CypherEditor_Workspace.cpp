//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Workspace.cpp
//  Purpose: Implements workspace decoding, creation, and the settings scope
//           stack.
//  Details: Identity (id, name, project) is strict because a workspace that
//           points at the wrong project would load the wrong game data; the
//           optional members are tolerant and reported through flags.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Workspace.h"

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

template <usize nExtent>
CYPHER_NODISCARD constexpr string_view_t WorkspaceText( const char ( &text )[nExtent] ) noexcept
{
    static_assert( nExtent > 0u );
    return { text, nExtent - 1u };
}

CYPHER_NODISCARD bool_t IsNativePath( string_view_t path ) noexcept
{
    if ( path.cchLength == 0u || path.cchLength > EDITOR_WORKSPACE_PATH_MAX_LENGTH ) { return CY_FALSE; }
    for ( usize iChar = 0u; iChar < path.cchLength; ++iChar ) {
        if ( static_cast<unsigned char>( path.pData[iChar] ) < 0x20u ) { return CY_FALSE; }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t SetString( settings_document_t *pStore, const char *pKey, string_view_t value ) noexcept
{
    settings_path_t path{};
    key_value_t *pNode = nullptr;
    return SettingsPath_Append( &path, StringView_FromCString( pKey ) ) &&
           SettingsDocument_Ensure( pStore, path, &pNode ) == settings_document_status_t::OK &&
           KeyValue_SetString( pStore->pDocument, pNode, value );
}

} // namespace

settings_document_identity_t EditorWorkspace_Identity() noexcept
{
    return { WorkspaceText( "cypher.workspace" ), EDITOR_WORKSPACE_SCHEMA_VERSION, EDITOR_WORKSPACE_SCHEMA_VERSION };
}

bool_t EditorWorkspace_IsContentPath( string_view_t path ) noexcept
{
    if ( !IsNativePath( path ) || path.pData[0] == '/' || path.pData[0] == '\\' ||
         ( path.cchLength >= 2u && path.pData[1] == ':' ) ) {
        return CY_FALSE;
    }
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= path.cchLength; ++iChar ) {
        if ( iChar < path.cchLength && path.pData[iChar] != '/' && path.pData[iChar] != '\\' ) { continue; }
        const string_view_t segment{ path.pData + iStart, iChar - iStart };
        if ( segment.cchLength == 0u || StringView_Equals( segment, WorkspaceText( ".." ) ) ) { return CY_FALSE; }
        iStart = iChar + 1u;
    }
    return CY_TRUE;
}

workspace_status_t EditorWorkspace_Decode(
    const key_value_document_t *pDocument,
    workspace_view_t *pWorkspaceOut,
    u32 *pProblemFlagsOut ) noexcept
{
    if ( pDocument == nullptr || pWorkspaceOut == nullptr ) { return workspace_status_t::INVALID_ARGUMENT; }
    const key_value_document_header_t header = KeyValue_DocumentHeader( pDocument );
    const key_value_t *pRoot = KeyValue_Root( pDocument );
    if ( header.nLanguageVersion != CYKV_LANGUAGE_VERSION ||
         !StringView_Equals( header.schemaId, WorkspaceText( "cypher.workspace" ) ) ||
         header.nSchemaVersion != EDITOR_WORKSPACE_SCHEMA_VERSION || KeyValue_Type( pRoot ) != key_value_type_t::OBJECT ) {
        return workspace_status_t::INVALID_HEADER;
    }

    workspace_view_t workspace{};
    u32 problems = WORKSPACE_PROBLEM_NONE;
    string_view_t idText{};
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, WorkspaceText( "id" ) ), &idText ) ||
         !UniqueId_FromString( idText, &workspace.id ) || !UniqueId_IsValid( workspace.id ) ) {
        return workspace_status_t::INVALID_ID;
    }
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, WorkspaceText( "name" ) ), &workspace.name ) ||
         workspace.name.cchLength == 0u || workspace.name.cchLength > EDITOR_WORKSPACE_NAME_MAX_LENGTH ) {
        return workspace_status_t::INVALID_NAME;
    }
    if ( !KeyValue_GetString( KeyValue_Find( pRoot, WorkspaceText( "project" ) ), &workspace.project ) ||
         !IsNativePath( workspace.project ) ) {
        return workspace_status_t::INVALID_PROJECT;
    }

    const key_value_t *pOwner = KeyValue_Find( pRoot, WorkspaceText( "owner" ) );
    if ( pOwner != nullptr && ( !KeyValue_GetString( pOwner, &workspace.owner ) ||
                                workspace.owner.cchLength > EDITOR_WORKSPACE_NAME_MAX_LENGTH ) ) {
        workspace.owner = {};
        problems |= WORKSPACE_PROBLEM_OWNER;
    }
    const key_value_t *pContent = KeyValue_Find( pRoot, WorkspaceText( "content" ) );
    if ( pContent != nullptr && ( !KeyValue_GetString( pContent, &workspace.content ) ||
                                  !EditorWorkspace_IsContentPath( workspace.content ) ) ) {
        workspace.content = {};
        problems |= WORKSPACE_PROBLEM_CONTENT;
    }
    const key_value_t *pSettings = KeyValue_Find( pRoot, WorkspaceText( "settings" ) );
    if ( pSettings != nullptr ) {
        if ( KeyValue_Type( pSettings ) == key_value_type_t::OBJECT ) { workspace.pSettings = pSettings; }
        else { problems |= WORKSPACE_PROBLEM_SETTINGS; }
    }
    const key_value_t *pState = KeyValue_Find( pRoot, WorkspaceText( "state" ) );
    if ( pState != nullptr ) {
        if ( KeyValue_Type( pState ) == key_value_type_t::OBJECT ) { workspace.pState = pState; }
        else { problems |= WORKSPACE_PROBLEM_STATE; }
    }

    *pWorkspaceOut = workspace;
    if ( pProblemFlagsOut != nullptr ) { *pProblemFlagsOut = problems; }
    return workspace_status_t::OK;
}

workspace_status_t EditorWorkspace_Create(
    settings_document_t *pStore,
    string_view_t name,
    string_view_t owner,
    string_view_t projectPath,
    string_view_t content ) noexcept
{
    if ( !SettingsDocument_IsInitialized( pStore ) || KeyValue_ChildCount( SettingsDocument_Root( pStore ) ) != 0u ||
         name.cchLength == 0u || name.cchLength > EDITOR_WORKSPACE_NAME_MAX_LENGTH ||
         owner.cchLength > EDITOR_WORKSPACE_NAME_MAX_LENGTH || !IsNativePath( projectPath ) ||
         ( content.cchLength != 0u && !EditorWorkspace_IsContentPath( content ) ) ) {
        return workspace_status_t::INVALID_ARGUMENT;
    }
    unique_id_t id{};
    char idText[CY_UNIQUE_ID_STRING_CAPACITY]{};
    if ( !UniqueId_CreateRandom( &id ) ) { return workspace_status_t::STORE_FAILED; }
    const usize cchId = UniqueId_ToString( id, idText, sizeof( idText ) );
    if ( cchId == 0u ) { return workspace_status_t::STORE_FAILED; }
    const bool_t bOk = SetString( pStore, "id", { idText, cchId } ) && SetString( pStore, "name", name ) &&
                       ( owner.cchLength == 0u || SetString( pStore, "owner", owner ) ) &&
                       SetString( pStore, "project", projectPath ) &&
                       ( content.cchLength == 0u || SetString( pStore, "content", content ) );
    return bOk ? workspace_status_t::OK : workspace_status_t::OUT_OF_MEMORY;
}

editor_scopes_t EditorScopes_Make(
    const workspace_view_t *pWorkspace,
    const project_manifest_view_t *pProject,
    const settings_document_t *pUser ) noexcept
{
    editor_scopes_t scopes{};
    scopes.roots[EDITOR_SCOPE_WORKSPACE] = pWorkspace != nullptr ? pWorkspace->pSettings : nullptr;
    scopes.roots[EDITOR_SCOPE_PROJECT] = pProject != nullptr ? pProject->pSettings : nullptr;
    scopes.roots[EDITOR_SCOPE_USER] = SettingsDocument_Root( pUser );
    return scopes;
}

const char *EditorScope_Name( usize iScope ) noexcept
{
    switch ( iScope ) {
        case EDITOR_SCOPE_WORKSPACE: return "Workspace";
        case EDITOR_SCOPE_PROJECT: return "Project";
        case EDITOR_SCOPE_USER: return "User";
        default: return "Default";
    }
}

const char *EditorWorkspace_StatusName( workspace_status_t status ) noexcept
{
    switch ( status ) {
        case workspace_status_t::OK: return "OK";
        case workspace_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case workspace_status_t::INVALID_HEADER: return "INVALID_HEADER";
        case workspace_status_t::INVALID_ID: return "INVALID_ID";
        case workspace_status_t::INVALID_NAME: return "INVALID_NAME";
        case workspace_status_t::INVALID_PROJECT: return "INVALID_PROJECT";
        case workspace_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case workspace_status_t::STORE_FAILED: return "STORE_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
