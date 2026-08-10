//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Localization.cpp
//  Purpose: Implements owned locale catalogs and named text substitution.
//  Details: Catalog entries own immutable key/text copies. Stable FNV IDs provide
//           fast external handles while exact key checks preserve collision safety.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Localization.h"

#include "CypherCommon_HashFNV.h"

namespace cypher::common
{

namespace
{

struct owned_localization_entry_t {
    localized_string_id_t id{ 0u };
    char *pKey{ nullptr };
    usize cchKey{ 0u };
    char *pText{ nullptr };
    usize cchText{ 0u };
};

struct localization_catalog_storage_t {
    const allocator_t *pAllocator{ nullptr };
    char *pLocaleTag{ nullptr };
    usize cchLocaleTag{ 0u };
    owned_localization_entry_t *pEntries{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
};

static_assert(
    sizeof( localization_catalog_storage_t ) >= sizeof( void * ),
    "Localization catalog storage must be a complete object." );

localization_catalog_storage_t *CatalogStorage(
    localization_catalog_t *pCatalog ) noexcept
{
    return reinterpret_cast<localization_catalog_storage_t *>( pCatalog );
}

const localization_catalog_storage_t *CatalogStorage(
    const localization_catalog_t *pCatalog ) noexcept
{
    return reinterpret_cast<const localization_catalog_storage_t *>( pCatalog );
}

bool_t CopyView(
    const allocator_t *pAllocator,
    string_view_t source,
    char **ppTextOut ) noexcept
{
    *ppTextOut = nullptr;
    if ( !StringView_IsValid( source ) || source.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    char *pCopy = static_cast<char *>( Allocator_Allocate(
        pAllocator,
        source.cchLength + 1u,
        alignof( char ) ) );
    if ( pCopy == nullptr ) {
        return CY_FALSE;
    }
    if ( source.cchLength != 0u ) {
        Cy_MemCopy( pCopy, source.pData, source.cchLength );
    }
    pCopy[source.cchLength] = '\0';
    *ppTextOut = pCopy;
    return CY_TRUE;
}

void FreeText(
    localization_catalog_storage_t &catalog,
    char *pText,
    usize cchText ) noexcept
{
    Allocator_Free(
        catalog.pAllocator,
        pText,
        cchText + 1u,
        alignof( char ) );
}

void FreeEntry(
    localization_catalog_storage_t &catalog,
    owned_localization_entry_t &entry ) noexcept
{
    FreeText( catalog, entry.pKey, entry.cchKey );
    FreeText( catalog, entry.pText, entry.cchText );
    entry = {};
}

bool_t ReserveEntries(
    localization_catalog_storage_t &catalog,
    usize nCapacity ) noexcept
{
    if ( nCapacity <= catalog.nCapacity ) {
        return CY_TRUE;
    }
    if ( nCapacity > CY_USIZE_MAX / sizeof( owned_localization_entry_t ) ) {
        return CY_FALSE;
    }
    const usize cbOld = catalog.nCapacity * sizeof( owned_localization_entry_t );
    const usize cbNew = nCapacity * sizeof( owned_localization_entry_t );
    void *pMemory = Allocator_Reallocate(
        catalog.pAllocator,
        catalog.pEntries,
        cbOld,
        cbNew,
        alignof( owned_localization_entry_t ) );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }
    catalog.pEntries = static_cast<owned_localization_entry_t *>( pMemory );
    Cy_MemZero(
        catalog.pEntries + catalog.nCapacity,
        ( nCapacity - catalog.nCapacity ) * sizeof( owned_localization_entry_t ) );
    catalog.nCapacity = nCapacity;
    return CY_TRUE;
}

usize FindKeyIndex(
    const localization_catalog_storage_t &catalog,
    string_view_t key ) noexcept
{
    for ( usize iEntry = 0u; iEntry < catalog.nCount; ++iEntry ) {
        const owned_localization_entry_t &entry = catalog.pEntries[iEntry];
        if ( StringView_Equals(
                 { entry.pKey, entry.cchKey },
                 key ) ) {
            return iEntry;
        }
    }
    return CY_INVALID_SIZE;
}

bool_t AppendByte(
    char value,
    char *pDest,
    usize cchDest,
    usize &cchRequired,
    usize &cchWritten ) noexcept
{
    if ( cchRequired == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    ++cchRequired;
    if ( pDest != nullptr && cchDest != 0u && cchWritten + 1u < cchDest ) {
        pDest[cchWritten++] = value;
    }
    return CY_TRUE;
}

bool_t AppendView(
    string_view_t text,
    char *pDest,
    usize cchDest,
    usize &cchRequired,
    usize &cchWritten ) noexcept
{
    if ( text.cchLength > CY_USIZE_MAX - cchRequired ) {
        cchRequired = CY_USIZE_MAX;
        return CY_FALSE;
    }
    cchRequired += text.cchLength;
    if ( pDest == nullptr || cchDest == 0u || cchWritten >= cchDest - 1u ) {
        return CY_TRUE;
    }
    const usize cchAvailable = cchDest - 1u - cchWritten;
    const usize cchCopy = text.cchLength < cchAvailable
        ? text.cchLength
        : cchAvailable;
    if ( cchCopy != 0u ) {
        Cy_MemCopy( pDest + cchWritten, text.pData, cchCopy );
        cchWritten += cchCopy;
    }
    return CY_TRUE;
}

string_view_t FindArgument(
    string_view_t name,
    const localization_argument_t *pArguments,
    usize nArgumentCount,
    bool_t &bFoundOut ) noexcept
{
    for ( usize iArgument = 0u; iArgument < nArgumentCount; ++iArgument ) {
        if ( StringView_Equals( pArguments[iArgument].name, name ) ) {
            bFoundOut = CY_TRUE;
            return pArguments[iArgument].value;
        }
    }
    bFoundOut = CY_FALSE;
    return {};
}

} // namespace

struct localization_catalog_t : localization_catalog_storage_t {
};

localization_catalog_t *Localization_CreateCatalog(
    const localization_catalog_desc_t &desc ) noexcept
{
    if ( !Allocator_IsValid( desc.pAllocator ) ||
         !StringView_IsValid( desc.localeTag ) ||
         desc.localeTag.cchLength == 0u ) {
        return nullptr;
    }

    auto *pCatalog = static_cast<localization_catalog_t *>( Allocator_AllocateZeroed(
        desc.pAllocator,
        sizeof( localization_catalog_t ),
        alignof( localization_catalog_t ) ) );
    if ( pCatalog == nullptr ) {
        return nullptr;
    }
    pCatalog->pAllocator = desc.pAllocator;
    pCatalog->cchLocaleTag = desc.localeTag.cchLength;
    if ( !CopyView( desc.pAllocator, desc.localeTag, &pCatalog->pLocaleTag ) ||
         !ReserveEntries( *pCatalog, desc.nInitialEntries ) ) {
        Localization_DestroyCatalog( pCatalog );
        return nullptr;
    }
    return pCatalog;
}

void Localization_DestroyCatalog(
    localization_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr ) {
        return;
    }
    localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    Localization_Clear( pCatalog );
    Allocator_Free(
        catalog.pAllocator,
        catalog.pEntries,
        catalog.nCapacity * sizeof( owned_localization_entry_t ),
        alignof( owned_localization_entry_t ) );
    FreeText( catalog, catalog.pLocaleTag, catalog.cchLocaleTag );
    const allocator_t *pAllocator = catalog.pAllocator;
    Allocator_Free(
        pAllocator,
        pCatalog,
        sizeof( localization_catalog_t ),
        alignof( localization_catalog_t ) );
}

void Localization_Clear(
    localization_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr ) {
        return;
    }
    localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    for ( usize iEntry = 0u; iEntry < catalog.nCount; ++iEntry ) {
        FreeEntry( catalog, catalog.pEntries[iEntry] );
    }
    catalog.nCount = 0u;
}

localized_string_id_t Localization_IdFromKey( string_view_t key ) noexcept
{
    return StringView_IsValid( key ) && key.cchLength != 0u
        ? HashFNV1a64_String( key )
        : 0u;
}

bool_t Localization_Add(
    localization_catalog_t *pCatalog,
    string_view_t key,
    string_view_t text ) noexcept
{
    if ( pCatalog == nullptr ||
         !StringView_IsValid( key ) || key.cchLength == 0u ||
         !StringView_IsValid( text ) ) {
        return CY_FALSE;
    }
    localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    const usize iExisting = FindKeyIndex( catalog, key );
    if ( iExisting != CY_INVALID_SIZE ) {
        char *pNewText = nullptr;
        if ( !CopyView( catalog.pAllocator, text, &pNewText ) ) {
            return CY_FALSE;
        }
        owned_localization_entry_t &entry = catalog.pEntries[iExisting];
        FreeText( catalog, entry.pText, entry.cchText );
        entry.pText = pNewText;
        entry.cchText = text.cchLength;
        return CY_TRUE;
    }

    char *pKey = nullptr;
    char *pText = nullptr;
    if ( !CopyView( catalog.pAllocator, key, &pKey ) ||
         !CopyView( catalog.pAllocator, text, &pText ) ) {
        if ( pKey != nullptr ) {
            FreeText( catalog, pKey, key.cchLength );
        }
        return CY_FALSE;
    }

    if ( catalog.nCount == catalog.nCapacity ) {
        usize nCapacity = catalog.nCapacity == 0u ? 8u : catalog.nCapacity;
        if ( nCapacity > CY_USIZE_MAX / 2u ) {
            FreeText( catalog, pKey, key.cchLength );
            FreeText( catalog, pText, text.cchLength );
            return CY_FALSE;
        }
        nCapacity *= 2u;
        if ( !ReserveEntries( catalog, nCapacity ) ) {
            FreeText( catalog, pKey, key.cchLength );
            FreeText( catalog, pText, text.cchLength );
            return CY_FALSE;
        }
    }

    catalog.pEntries[catalog.nCount++] = {
        Localization_IdFromKey( key ),
        pKey,
        key.cchLength,
        pText,
        text.cchLength
    };
    return CY_TRUE;
}

string_view_t Localization_Find(
    const localization_catalog_t *pCatalog,
    localized_string_id_t id ) noexcept
{
    if ( pCatalog == nullptr || id == 0u ) {
        return {};
    }
    const localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    const owned_localization_entry_t *pMatch = nullptr;
    for ( usize iEntry = 0u; iEntry < catalog.nCount; ++iEntry ) {
        if ( catalog.pEntries[iEntry].id == id ) {
            if ( pMatch != nullptr ) {
                return {};
            }
            pMatch = &catalog.pEntries[iEntry];
        }
    }
    return pMatch != nullptr
        ? string_view_t{ pMatch->pText, pMatch->cchText }
        : string_view_t{};
}

string_view_t Localization_FindByKey(
    const localization_catalog_t *pCatalog,
    string_view_t key ) noexcept
{
    if ( pCatalog == nullptr || !StringView_IsValid( key ) ) {
        return {};
    }
    const localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    const usize iEntry = FindKeyIndex( catalog, key );
    if ( iEntry == CY_INVALID_SIZE ) {
        return {};
    }
    return {
        catalog.pEntries[iEntry].pText,
        catalog.pEntries[iEntry].cchText
    };
}

usize Localization_Format(
    const localization_catalog_t *pCatalog,
    localized_string_id_t id,
    const localization_argument_t *pArguments,
    usize nArgumentCount,
    char *pDest,
    usize cchDest ) noexcept
{
    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[0] = '\0';
    }
    if ( pCatalog == nullptr ||
         ( nArgumentCount != 0u && pArguments == nullptr ) ||
         ( pDest == nullptr && cchDest != 0u ) ) {
        return 0u;
    }
    for ( usize iArgument = 0u; iArgument < nArgumentCount; ++iArgument ) {
        if ( !StringView_IsValid( pArguments[iArgument].name ) ||
             !StringView_IsValid( pArguments[iArgument].value ) ) {
            return 0u;
        }
    }

    const string_view_t format = Localization_Find( pCatalog, id );
    if ( StringView_IsEmpty( format ) ) {
        return 0u;
    }

    usize cchRequired = 0u;
    usize cchWritten = 0u;
    for ( usize iByte = 0u; iByte < format.cchLength; ) {
        if ( format.pData[iByte] == '{' &&
             iByte + 1u < format.cchLength &&
             format.pData[iByte + 1u] == '{' ) {
            if ( !AppendByte( '{', pDest, cchDest, cchRequired, cchWritten ) ) {
                return CY_INVALID_SIZE;
            }
            iByte += 2u;
            continue;
        }
        if ( format.pData[iByte] == '}' &&
             iByte + 1u < format.cchLength &&
             format.pData[iByte + 1u] == '}' ) {
            if ( !AppendByte( '}', pDest, cchDest, cchRequired, cchWritten ) ) {
                return CY_INVALID_SIZE;
            }
            iByte += 2u;
            continue;
        }
        if ( format.pData[iByte] == '{' ) {
            const usize iClose = StringView_FindChar( format, '}', iByte + 1u );
            if ( iClose != CY_STRING_VIEW_NPOS && iClose > iByte + 1u ) {
                const string_view_t name = StringView_Subview(
                    format,
                    iByte + 1u,
                    iClose - iByte - 1u );
                bool_t bFound = CY_FALSE;
                const string_view_t value = FindArgument(
                    name,
                    pArguments,
                    nArgumentCount,
                    bFound );
                if ( bFound ) {
                    if ( !AppendView(
                             value,
                             pDest,
                             cchDest,
                             cchRequired,
                             cchWritten ) ) {
                        return CY_INVALID_SIZE;
                    }
                    iByte = iClose + 1u;
                    continue;
                }
            }
        }
        if ( !AppendByte(
                 format.pData[iByte],
                 pDest,
                 cchDest,
                 cchRequired,
                 cchWritten ) ) {
            return CY_INVALID_SIZE;
        }
        ++iByte;
    }
    if ( pDest != nullptr && cchDest != 0u ) {
        pDest[cchWritten] = '\0';
    }
    return cchRequired;
}

string_view_t Localization_LocaleTag(
    const localization_catalog_t *pCatalog ) noexcept
{
    if ( pCatalog == nullptr ) {
        return {};
    }
    const localization_catalog_storage_t &catalog = *CatalogStorage( pCatalog );
    return { catalog.pLocaleTag, catalog.cchLocaleTag };
}

usize Localization_Count(
    const localization_catalog_t *pCatalog ) noexcept
{
    return pCatalog != nullptr ? CatalogStorage( pCatalog )->nCount : 0u;
}

} // namespace cypher::common
