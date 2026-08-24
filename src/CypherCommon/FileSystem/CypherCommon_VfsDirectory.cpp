//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/FileSystem/CypherCommon_VfsDirectory.cpp
//  Purpose: Implements the read-only loose-directory VFS provider.
//  Details: The provider resolves symlinks before access, rejects paths escaping
//           its canonical root, bounds whole-file reads before allocation, and
//           sorts enumeration results for deterministic compiler discovery.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_VfsDirectory.h"

#include "CypherCommon_FileIo.h"
#include "CypherCommon_Stream.h"

#include <algorithm>
#include <filesystem>
#include <new>
#include <string>
#include <system_error>
#include <vector>

namespace cypher::common
{
namespace
{

struct directory_entry_record_t {
    std::string virtualPath{}; // Canonical path relative to the provider root.
    vfs_file_info_t info{};    // Type and size captured during traversal.
};

inline constexpr usize CY_VFS_DIRECTORY_MAX_ENTRIES = 1u << 20u; // Traversal bound.
inline constexpr usize CY_VFS_DIRECTORY_MAX_PATH_BYTES = 256u * CY_MIB; // Aggregate.

CYPHER_NODISCARD bool_t DirectoryIsInitialized(
    const vfs_directory_t *pDirectory ) noexcept
{
    return pDirectory != nullptr &&
           TextBuffer_IsValid( &pDirectory->nativeRoot ) &&
           pDirectory->nativeRoot.cchLength != 0u;
}

template <typename operation_t>
CYPHER_NODISCARD vfs_status_t GuardFilesystemOperation(
    operation_t &&operation ) noexcept
{
    // std::filesystem may throw despite error_code overloads during allocation.
#if CYPHER_CPP_EXCEPTIONS
    try {
        return operation();
    } catch ( const std::bad_alloc & ) {
        return vfs_status_t::OUT_OF_MEMORY;
    } catch ( ... ) {
        return vfs_status_t::IO_ERROR;
    }
#else
    return operation();
#endif
}

CYPHER_NODISCARD std::filesystem::path PathFromUtf8(
    string_view_t text )
{
    const auto *pBegin = reinterpret_cast<const char8_t *>( text.pData );
    return std::filesystem::path( pBegin, pBegin + text.cchLength );
}

CYPHER_NODISCARD std::string PathToUtf8(
    const std::filesystem::path &path )
{
    const std::u8string text = path.u8string();
    return {
        reinterpret_cast<const char *>( text.data() ),
        text.size()
    };
}

CYPHER_NODISCARD bool_t PathIsBelowRoot(
    const std::filesystem::path &root,
    const std::filesystem::path &candidate )
{
    // Compare canonical path components, avoiding unsafe textual-prefix checks.
    auto iRoot = root.begin();
    auto iCandidate = candidate.begin();
    for ( ; iRoot != root.end(); ++iRoot, ++iCandidate ) {
        if ( iCandidate == candidate.end() || *iRoot != *iCandidate ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

CYPHER_NODISCARD vfs_status_t ResolveDirectoryPath(
    const vfs_directory_t &directory,
    string_view_t virtualPath,
    bool_t bAllowEmpty,
    std::filesystem::path *pPathOut )
{
    if ( pPathOut == nullptr ||
         ( virtualPath.cchLength == 0u && !bAllowEmpty ) ||
         ( virtualPath.cchLength != 0u &&
           !Vfs_IsCanonicalPath( virtualPath ) ) ) {
        return vfs_status_t::INVALID_PATH;
    }

    const std::filesystem::path root = PathFromUtf8(
        TextBuffer_View( &directory.nativeRoot ) );
    std::filesystem::path candidate = root;
    if ( virtualPath.cchLength != 0u ) {
        candidate /= PathFromUtf8( virtualPath );
    }

    std::error_code error{};
    // Resolve existing symlinks before enforcing the provider-root boundary.
    candidate = std::filesystem::weakly_canonical( candidate, error );
    if ( error ) {
        return vfs_status_t::IO_ERROR;
    }
    if ( !PathIsBelowRoot( root, candidate ) ) {
        return vfs_status_t::INVALID_PATH;
    }
    *pPathOut = std::move( candidate );
    return vfs_status_t::OK;
}

CYPHER_NODISCARD vfs_status_t QueryDirectoryEntry(
    const std::filesystem::path &path,
    vfs_file_info_t *pInfoOut )
{
    if ( pInfoOut == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    *pInfoOut = {};

    std::error_code error{};
    const std::filesystem::file_status status =
        std::filesystem::status( path, error );
    if ( error == std::errc::no_such_file_or_directory ) {
        return vfs_status_t::NOT_FOUND;
    }
    if ( error ) {
        return vfs_status_t::IO_ERROR;
    }
    if ( !std::filesystem::exists( status ) ) {
        return vfs_status_t::NOT_FOUND;
    }
    if ( std::filesystem::is_regular_file( status ) ) {
        const std::uintmax_t cbSize = std::filesystem::file_size( path, error );
        if ( error || cbSize > CY_U64_MAX ) {
            return vfs_status_t::IO_ERROR;
        }
        pInfoOut->type = vfs_entry_type_t::FILE;
        pInfoOut->cbSize = static_cast<u64>( cbSize );
        return vfs_status_t::OK;
    }
    if ( std::filesystem::is_directory( status ) ) {
        pInfoOut->type = vfs_entry_type_t::DIRECTORY;
        return vfs_status_t::OK;
    }
    return vfs_status_t::NOT_A_FILE;
}

CYPHER_NODISCARD vfs_status_t DirectoryReadAllImpl(
    vfs_directory_t &directory,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest )
{
    std::filesystem::path nativePath{};
    vfs_status_t status = ResolveDirectoryPath(
        directory,
        virtualPath,
        CY_FALSE,
        &nativePath );
    if ( status != vfs_status_t::OK ) {
        return status;
    }

    vfs_file_info_t info{};
    status = QueryDirectoryEntry( nativePath, &info );
    if ( status != vfs_status_t::OK ) {
        return status;
    }
    if ( info.type != vfs_entry_type_t::FILE ) {
        return vfs_status_t::NOT_A_FILE;
    }
    if ( info.cbSize > cbMaximum || info.cbSize > CY_USIZE_MAX ) {
        return vfs_status_t::SIZE_LIMIT;
    }

    const std::string pathText = PathToUtf8( nativePath );
    const string_view_t pathView{ pathText.data(), pathText.size() };
    native_file_t *pFile = FileIo_OpenNative(
        pathView,
        FILE_OPEN_FLAG_READ,
        pDest->pAllocator );
    if ( pFile == nullptr ) {
        return vfs_status_t::IO_ERROR;
    }

    // Read into a temporary blob so failure leaves the caller's destination intact.
    blob_t pending{};
    const usize cbSize = static_cast<usize>( info.cbSize );
    const bool_t bInitialized = Blob_Init(
        &pending,
        pDest->pAllocator,
        cbSize );
    const bool_t bResized = bInitialized && Blob_Resize( &pending, cbSize );
    stream_t stream = FileIo_AsStream( pFile );
    const bool_t bRead = bResized &&
        Stream_ReadExact( &stream, pending.pData, cbSize ) ==
            stream_status_t::OK;
    FileIo_CloseNative( pFile );
    if ( !bInitialized || !bResized ) {
        Blob_Shutdown( &pending );
        return vfs_status_t::OUT_OF_MEMORY;
    }
    if ( !bRead ) {
        Blob_Shutdown( &pending );
        return vfs_status_t::IO_ERROR;
    }

    Blob_Shutdown( pDest );
    Blob_Move( pDest, &pending );
    return vfs_status_t::OK;
}

vfs_status_t DirectoryReadAll(
    void *pUserData,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest ) noexcept
{
    auto *pDirectory = static_cast<vfs_directory_t *>( pUserData );
    if ( !DirectoryIsInitialized( pDirectory ) ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return GuardFilesystemOperation( [&]() {
        return DirectoryReadAllImpl(
            *pDirectory,
            virtualPath,
            cbMaximum,
            pDest );
    } );
}

vfs_status_t DirectoryStat(
    void *pUserData,
    string_view_t virtualPath,
    vfs_file_info_t *pInfoOut ) noexcept
{
    auto *pDirectory = static_cast<vfs_directory_t *>( pUserData );
    if ( !DirectoryIsInitialized( pDirectory ) ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return GuardFilesystemOperation( [&]() {
        std::filesystem::path nativePath{};
        const vfs_status_t status = ResolveDirectoryPath(
            *pDirectory,
            virtualPath,
            CY_FALSE,
            &nativePath );
        return status == vfs_status_t::OK
            ? QueryDirectoryEntry( nativePath, pInfoOut )
            : status;
    } );
}

template <typename iterator_t>
CYPHER_NODISCARD vfs_status_t CollectDirectoryEntries(
    iterator_t iterator,
    const iterator_t &end,
    const std::filesystem::path &providerRoot,
    std::vector<directory_entry_record_t> *pEntries,
    usize *pcbPathData )
{
    if ( pEntries == nullptr || pcbPathData == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    std::error_code error{};
    for ( ; iterator != end; iterator.increment( error ) ) {
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        const std::filesystem::directory_entry &entry = *iterator;
        const std::filesystem::file_status symlinkStatus =
            entry.symlink_status( error );
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        // Enumeration never follows symlinks, even when their targets stay in root.
        if ( std::filesystem::is_symlink( symlinkStatus ) ) {
            continue;
        }

        vfs_file_info_t info{};
        if ( std::filesystem::is_regular_file( symlinkStatus ) ) {
            const std::uintmax_t cbSize = entry.file_size( error );
            if ( error || cbSize > CY_U64_MAX ) {
                return vfs_status_t::IO_ERROR;
            }
            info.type = vfs_entry_type_t::FILE;
            info.cbSize = static_cast<u64>( cbSize );
        } else if ( std::filesystem::is_directory( symlinkStatus ) ) {
            info.type = vfs_entry_type_t::DIRECTORY;
        } else {
            continue;
        }

        // Provider results always expose root-relative generic paths with '/'.
        const std::filesystem::path relative =
            std::filesystem::relative( entry.path(), providerRoot, error );
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        const std::u8string genericPath = relative.generic_u8string();
        std::string virtualPath{
            reinterpret_cast<const char *>( genericPath.data() ),
            genericPath.size()
        };
        const string_view_t pathView{
            virtualPath.data(),
            virtualPath.size()
        };
        if ( !Vfs_IsCanonicalPath( pathView ) ) {
            continue;
        }
        if ( pEntries->size() >= CY_VFS_DIRECTORY_MAX_ENTRIES ||
             virtualPath.size() >
                 CY_VFS_DIRECTORY_MAX_PATH_BYTES - *pcbPathData ) {
            return vfs_status_t::SIZE_LIMIT;
        }
        *pcbPathData += virtualPath.size();
        pEntries->push_back( { std::move( virtualPath ), info } );
    }
    return vfs_status_t::OK;
}

CYPHER_NODISCARD vfs_status_t DirectoryEnumerateImpl(
    vfs_directory_t &directory,
    string_view_t virtualRoot,
    bool_t bRecursive,
    vfs_visit_fn_t pVisit,
    void *pVisitUserData )
{
    std::filesystem::path nativeRoot{};
    vfs_status_t status = ResolveDirectoryPath(
        directory,
        virtualRoot,
        CY_TRUE,
        &nativeRoot );
    if ( status != vfs_status_t::OK ) {
        return status;
    }

    vfs_file_info_t rootInfo{};
    status = QueryDirectoryEntry( nativeRoot, &rootInfo );
    if ( status != vfs_status_t::OK ) {
        return status;
    }
    if ( rootInfo.type != vfs_entry_type_t::DIRECTORY ) {
        return vfs_status_t::NOT_A_DIRECTORY;
    }

    const std::filesystem::path providerRoot = PathFromUtf8(
        TextBuffer_View( &directory.nativeRoot ) );
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code error{};
    std::vector<directory_entry_record_t> entries{};
    usize cbPathData = 0u;
    if ( bRecursive ) {
        std::filesystem::recursive_directory_iterator begin(
            nativeRoot,
            options,
            error );
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        status = CollectDirectoryEntries(
            std::move( begin ),
            std::filesystem::recursive_directory_iterator{},
            providerRoot,
            &entries,
            &cbPathData );
    } else {
        std::filesystem::directory_iterator begin(
            nativeRoot,
            options,
            error );
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        status = CollectDirectoryEntries(
            std::move( begin ),
            std::filesystem::directory_iterator{},
            providerRoot,
            &entries,
            &cbPathData );
    }
    if ( status != vfs_status_t::OK ) {
        return status;
    }

    // Native iterator order is unspecified; sort before invoking user callbacks.
    std::sort(
        entries.begin(),
        entries.end(),
        []( const directory_entry_record_t &left,
            const directory_entry_record_t &right ) {
            return left.virtualPath < right.virtualPath;
        } );
    for ( const directory_entry_record_t &entry : entries ) {
        const string_view_t path{
            entry.virtualPath.data(),
            entry.virtualPath.size()
        };
        if ( !pVisit( path, entry.info, pVisitUserData ) ) {
            return vfs_status_t::CANCELLED;
        }
    }
    return vfs_status_t::OK;
}

vfs_status_t DirectoryEnumerate(
    void *pUserData,
    string_view_t virtualRoot,
    bool_t bRecursive,
    vfs_visit_fn_t pVisit,
    void *pVisitUserData ) noexcept
{
    auto *pDirectory = static_cast<vfs_directory_t *>( pUserData );
    if ( !DirectoryIsInitialized( pDirectory ) || pVisit == nullptr ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return GuardFilesystemOperation( [&]() {
        return DirectoryEnumerateImpl(
            *pDirectory,
            virtualRoot,
            bRecursive,
            pVisit,
            pVisitUserData );
    } );
}

vfs_status_t DirectoryResolveDiagnosticPath(
    void *pUserData,
    string_view_t virtualPath,
    text_buffer_t *pNativePathOut ) noexcept
{
    auto *pDirectory = static_cast<vfs_directory_t *>( pUserData );
    if ( !DirectoryIsInitialized( pDirectory ) ||
         !TextBuffer_IsValid( pNativePathOut ) ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    return GuardFilesystemOperation( [&]() {
        std::filesystem::path nativePath{};
        const vfs_status_t status = ResolveDirectoryPath(
            *pDirectory,
            virtualPath,
            CY_FALSE,
            &nativePath );
        if ( status != vfs_status_t::OK ) {
            return status;
        }
        const std::string text = PathToUtf8( nativePath );
        return TextBuffer_Assign(
                   pNativePathOut,
                   { text.data(), text.size() } )
            ? vfs_status_t::OK
            : vfs_status_t::OUT_OF_MEMORY;
    } );
}

const vfs_ops_t DIRECTORY_VFS_OPS{
    &DirectoryReadAll,
    &DirectoryStat,
    &DirectoryEnumerate,
    &DirectoryResolveDiagnosticPath
};

} // namespace

vfs_status_t VfsDirectory_Init(
    vfs_directory_t *pDirectory,
    string_view_t nativeRoot ) noexcept
{
    if ( pDirectory == nullptr ||
         !TextBuffer_IsValid( &pDirectory->nativeRoot ) ||
         pDirectory->nativeRoot.pAllocator != nullptr ||
         !StringView_IsValid( nativeRoot ) || nativeRoot.cchLength == 0u ) {
        return vfs_status_t::INVALID_ARGUMENT;
    }
    for ( usize iByte = 0u; iByte < nativeRoot.cchLength; ++iByte ) {
        if ( nativeRoot.pData[iByte] == '\0' ) {
            return vfs_status_t::INVALID_PATH;
        }
    }

    // Store the real canonical root once so every later containment check agrees.
    return GuardFilesystemOperation( [&]() {
        std::error_code error{};
        const std::filesystem::path root = std::filesystem::canonical(
            PathFromUtf8( nativeRoot ),
            error );
        if ( error == std::errc::no_such_file_or_directory ) {
            return vfs_status_t::NOT_FOUND;
        }
        if ( error ) {
            return vfs_status_t::IO_ERROR;
        }
        if ( !std::filesystem::is_directory( root, error ) || error ) {
            return error
                ? vfs_status_t::IO_ERROR
                : vfs_status_t::NOT_A_DIRECTORY;
        }

        const std::string rootText = PathToUtf8( root );
        if ( !TextBuffer_Init(
                 &pDirectory->nativeRoot,
                 Allocator_GetSystem(),
                 rootText.size() ) ) {
            return vfs_status_t::OUT_OF_MEMORY;
        }
        if ( !TextBuffer_Assign(
                 &pDirectory->nativeRoot,
                 { rootText.data(), rootText.size() } ) ) {
            TextBuffer_Shutdown( &pDirectory->nativeRoot );
            return vfs_status_t::OUT_OF_MEMORY;
        }
        return vfs_status_t::OK;
    } );
}

void VfsDirectory_Shutdown( vfs_directory_t *pDirectory ) noexcept
{
    if ( pDirectory == nullptr ||
         !TextBuffer_IsValid( &pDirectory->nativeRoot ) ) {
        return;
    }
    if ( pDirectory->nativeRoot.pAllocator != nullptr ) {
        TextBuffer_Shutdown( &pDirectory->nativeRoot );
    }
}

vfs_t VfsDirectory_Make( vfs_directory_t *pDirectory ) noexcept
{
    if ( !DirectoryIsInitialized( pDirectory ) ) {
        return {};
    }
    // The lightweight facade borrows pDirectory; shutdown must wait for all users.
    return {
        &DIRECTORY_VFS_OPS,
        pDirectory,
        VFS_CAPABILITY_READ_ALL |
            VFS_CAPABILITY_STAT |
            VFS_CAPABILITY_ENUMERATE |
            VFS_CAPABILITY_DIAGNOSTIC_PATH
    };
}

} // namespace cypher::common
