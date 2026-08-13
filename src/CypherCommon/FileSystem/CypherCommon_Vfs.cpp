//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/FileSystem/CypherCommon_Vfs.cpp
//  Purpose: Implements the provider-neutral virtual filesystem facade.
//  Details: The facade validates capability tables, canonical resource paths,
//           and operation arguments before dispatching through an injected VFS
//           provider. It contains no native directory or package policy.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Vfs.h"

#include "CypherCommon_DataValidation.h"

namespace cypher::common
{
namespace
{

CYPHER_NODISCARD bool_t VfsContractIsValid(
    const vfs_t *pVfs ) noexcept
{
    if ( pVfs == nullptr || pVfs->pOps == nullptr ||
         ( pVfs->capabilities & ~CY_VFS_CAPABILITY_MASK ) != 0u ) {
        return CY_FALSE;
    }
    if ( ( pVfs->capabilities & VFS_CAPABILITY_READ_ALL ) != 0u &&
         pVfs->pOps->pfnReadAll == nullptr ) {
        return CY_FALSE;
    }
    if ( ( pVfs->capabilities & VFS_CAPABILITY_STAT ) != 0u &&
         pVfs->pOps->pfnStat == nullptr ) {
        return CY_FALSE;
    }
    if ( ( pVfs->capabilities & VFS_CAPABILITY_ENUMERATE ) != 0u &&
         pVfs->pOps->pfnEnumerate == nullptr ) {
        return CY_FALSE;
    }
    if ( ( pVfs->capabilities & VFS_CAPABILITY_DIAGNOSTIC_PATH ) != 0u &&
         pVfs->pOps->pfnResolveDiagnosticPath == nullptr ) {
        return CY_FALSE;
    }
    return pVfs->capabilities != VFS_CAPABILITY_NONE;
}

} // namespace

bool_t Vfs_IsCanonicalPath( string_view_t virtualPath ) noexcept
{
    return DataValidation_Succeeded(
        DataValidation_CheckCanonicalVirtualPath(
            virtualPath,
            CY_VFS_MAX_VIRTUAL_PATH ) );
}

bool_t Vfs_IsValid( const vfs_t *pVfs ) noexcept
{
    return VfsContractIsValid( pVfs );
}

vfs_status_t Vfs_ReadAll(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest ) noexcept
{
    if ( !VfsContractIsValid( pVfs ) ||
         ( pVfs->capabilities & VFS_CAPABILITY_READ_ALL ) == 0u ) {
        return vfs_status_t::UNSUPPORTED;
    }
    if ( !Vfs_IsCanonicalPath( virtualPath ) || cbMaximum == 0u ||
         !Blob_IsValid( pDest ) ||
         !Allocator_IsValid( pDest->pAllocator ) ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return pVfs->pOps->pfnReadAll(
        pVfs->pUserData,
        virtualPath,
        cbMaximum,
        pDest );
}

vfs_status_t Vfs_Stat(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    vfs_file_info_t *pInfoOut ) noexcept
{
    if ( pInfoOut != nullptr ) {
        *pInfoOut = {};
    }
    if ( !VfsContractIsValid( pVfs ) ||
         ( pVfs->capabilities & VFS_CAPABILITY_STAT ) == 0u ) {
        return vfs_status_t::UNSUPPORTED;
    }
    if ( !Vfs_IsCanonicalPath( virtualPath ) || pInfoOut == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return pVfs->pOps->pfnStat(
        pVfs->pUserData,
        virtualPath,
        pInfoOut );
}

vfs_status_t Vfs_Exists(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    bool_t *pExistsOut ) noexcept
{
    if ( pExistsOut == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    *pExistsOut = CY_FALSE;
    vfs_file_info_t info{};
    const vfs_status_t status = Vfs_Stat( pVfs, virtualPath, &info );
    if ( status == vfs_status_t::NOT_FOUND ) {
        return vfs_status_t::OK;
    }
    if ( status == vfs_status_t::OK ) {
        *pExistsOut = CY_TRUE;
    }
    return status;
}

vfs_status_t Vfs_Enumerate(
    const vfs_t *pVfs,
    string_view_t virtualRoot,
    bool_t bRecursive,
    vfs_visit_fn_t pVisit,
    void *pVisitUserData ) noexcept
{
    if ( !VfsContractIsValid( pVfs ) ||
         ( pVfs->capabilities & VFS_CAPABILITY_ENUMERATE ) == 0u ) {
        return vfs_status_t::UNSUPPORTED;
    }
    if ( !StringView_IsValid( virtualRoot ) ||
         ( virtualRoot.cchLength != 0u &&
           !Vfs_IsCanonicalPath( virtualRoot ) ) ||
         pVisit == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return pVfs->pOps->pfnEnumerate(
        pVfs->pUserData,
        virtualRoot,
        bRecursive,
        pVisit,
        pVisitUserData );
}

vfs_status_t Vfs_ResolveDiagnosticPath(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    text_buffer_t *pNativePathOut ) noexcept
{
    if ( !VfsContractIsValid( pVfs ) ||
         ( pVfs->capabilities & VFS_CAPABILITY_DIAGNOSTIC_PATH ) == 0u ) {
        return vfs_status_t::UNSUPPORTED;
    }
    if ( !Vfs_IsCanonicalPath( virtualPath ) ||
         !TextBuffer_IsValid( pNativePathOut ) ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return pVfs->pOps->pfnResolveDiagnosticPath(
        pVfs->pUserData,
        virtualPath,
        pNativePathOut );
}

const char *Vfs_StatusName( vfs_status_t status ) noexcept
{
    switch ( status ) {
        case vfs_status_t::OK: return "OK";
        case vfs_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case vfs_status_t::INVALID_PATH: return "INVALID_PATH";
        case vfs_status_t::NOT_FOUND: return "NOT_FOUND";
        case vfs_status_t::NOT_A_FILE: return "NOT_A_FILE";
        case vfs_status_t::NOT_A_DIRECTORY: return "NOT_A_DIRECTORY";
        case vfs_status_t::SIZE_LIMIT: return "SIZE_LIMIT";
        case vfs_status_t::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case vfs_status_t::IO_ERROR: return "IO_ERROR";
        case vfs_status_t::UNSUPPORTED: return "UNSUPPORTED";
        case vfs_status_t::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
