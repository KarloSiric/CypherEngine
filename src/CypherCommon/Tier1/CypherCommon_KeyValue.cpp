//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_KeyValue.cpp
//  Purpose: Implements the owned hierarchical CYKV data model.
//  Details: Nodes come from stable geometric blocks and textual/binary payloads live
//           in document arenas. Removal invalidates only the removed subtree.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_KeyValueInternal.h"

#include "CypherCommon_Char.h"

#include <cmath>
#include <new>

namespace cypher::common
{

namespace
{

inline constexpr u32 CY_KEY_VALUE_DOCUMENT_MAGIC = 0x4B56444Fu;

static_assert(
    alignof( key_value_node_block_t ) >= alignof( key_value_t ) &&
    sizeof( key_value_node_block_t ) % alignof( key_value_t ) == 0u,
    "KeyValue node blocks must preserve node alignment." );

CYPHER_NODISCARD bool_t IsValidType( key_value_type_t type ) noexcept
{
    return static_cast<u8>( type ) <=
           static_cast<u8>( key_value_type_t::ARRAY );
}

CYPHER_NODISCARD bool_t IsContainerType( key_value_type_t type ) noexcept
{
    return type == key_value_type_t::OBJECT ||
           type == key_value_type_t::ARRAY;
}

CYPHER_NODISCARD bool_t IsValidSchemaId( string_view_t schemaId ) noexcept
{
    if ( !StringView_IsValid( schemaId ) || schemaId.cchLength == 0u ) {
        return CY_FALSE;
    }
    bool_t bAtComponentStart = CY_TRUE;
    bool_t bSawDot = CY_FALSE;
    for ( usize iByte = 0u; iByte < schemaId.cchLength; ++iByte ) {
        const char ch = schemaId.pData[iByte];
        if ( ch == '.' ) {
            if ( bAtComponentStart ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_TRUE;
            bSawDot = CY_TRUE;
        } else if ( bAtComponentStart ) {
            if ( ch < 'a' || ch > 'z' ) {
                return CY_FALSE;
            }
            bAtComponentStart = CY_FALSE;
        } else if ( ( ch < 'a' || ch > 'z' ) &&
                    !Char_IsDigitAscii( ch ) && ch != '_' && ch != '-' ) {
            return CY_FALSE;
        }
    }
    return bSawDot && !bAtComponentStart;
}

CYPHER_NODISCARD key_value_t *NodeBlockData(
    key_value_node_block_t *pBlock ) noexcept
{
    return reinterpret_cast<key_value_t *>( pBlock + 1u );
}

CYPHER_NODISCARD byte *DataBlockData(
    key_value_data_block_t *pBlock ) noexcept
{
    return reinterpret_cast<byte *>( pBlock + 1u );
}

CYPHER_NODISCARD bool_t NodeAllocationSize(
    usize nCapacity,
    usize &cbAllocation ) noexcept
{
    if ( nCapacity == 0u ||
         nCapacity >
            ( CY_USIZE_MAX - sizeof( key_value_node_block_t ) ) /
                sizeof( key_value_t ) ) {
        cbAllocation = 0u;
        return CY_FALSE;
    }
    cbAllocation = sizeof( key_value_node_block_t ) +
                   nCapacity * sizeof( key_value_t );
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t DataAllocationSize(
    usize cbCapacity,
    usize &cbAllocation ) noexcept
{
    if ( cbCapacity == 0u ||
         cbCapacity > CY_USIZE_MAX - sizeof( key_value_data_block_t ) ) {
        cbAllocation = 0u;
        return CY_FALSE;
    }
    cbAllocation = sizeof( key_value_data_block_t ) + cbCapacity;
    return CY_TRUE;
}

void InitializeRoot( key_value_document_t &document ) noexcept
{
    document.root = {};
    document.root.pDocument = &document;
    document.nNodes = 1u;
}

void FreeNodeBlocks( key_value_document_t &document ) noexcept
{
    key_value_node_block_t *pBlock = document.pNodeBlocks;
    while ( pBlock != nullptr ) {
        key_value_node_block_t *pNext = pBlock->pNext;
        usize cbAllocation = 0u;
        if ( NodeAllocationSize( pBlock->nCapacity, cbAllocation ) ) {
            Allocator_Free(
                document.pAllocator,
                pBlock,
                cbAllocation,
                alignof( key_value_node_block_t ) );
        }
        pBlock = pNext;
    }
    document.pNodeBlocks = nullptr;
    document.pFreeNodes = nullptr;
}

void FreeDataBlocks( key_value_document_t &document ) noexcept
{
    key_value_data_block_t *pBlock = document.pDataBlocks;
    while ( pBlock != nullptr ) {
        key_value_data_block_t *pNext = pBlock->pNext;
        usize cbAllocation = 0u;
        if ( DataAllocationSize( pBlock->cbCapacity, cbAllocation ) ) {
            Allocator_Free(
                document.pAllocator,
                pBlock,
                cbAllocation,
                alignof( key_value_data_block_t ) );
        }
        pBlock = pNext;
    }
    document.pDataBlocks = nullptr;
    document.cbData = 0u;
}

CYPHER_NODISCARD key_value_node_block_t *AllocateNodeBlock(
    key_value_document_t &document ) noexcept
{
    const usize nCapacity = document.nNextNodes;
    usize cbAllocation = 0u;
    if ( !NodeAllocationSize( nCapacity, cbAllocation ) ) {
        return nullptr;
    }
    void *pStorage = Allocator_Allocate(
        document.pAllocator,
        cbAllocation,
        alignof( key_value_node_block_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }
    auto *pBlock = new ( pStorage ) key_value_node_block_t{};
    pBlock->pNext = document.pNodeBlocks;
    pBlock->nCapacity = nCapacity;
    document.pNodeBlocks = pBlock;
    document.nNextNodes = nCapacity <= CY_USIZE_MAX / 2u
        ? nCapacity * 2u
        : nCapacity;
    return pBlock;
}

CYPHER_NODISCARD key_value_t *AllocateNode(
    key_value_document_t &document ) noexcept
{
    key_value_t *pNode = document.pFreeNodes;
    if ( pNode != nullptr ) {
        document.pFreeNodes = pNode->pNext;
    } else {
        key_value_node_block_t *pBlock = document.pNodeBlocks;
        if ( pBlock == nullptr || pBlock->nUsed == pBlock->nCapacity ) {
            pBlock = AllocateNodeBlock( document );
            if ( pBlock == nullptr ) {
                return nullptr;
            }
        }
        pNode = NodeBlockData( pBlock ) + pBlock->nUsed++;
    }
    *pNode = {};
    pNode->pDocument = &document;
    ++document.nNodes;
    return pNode;
}

CYPHER_NODISCARD key_value_data_block_t *AllocateDataBlock(
    key_value_document_t &document,
    usize cbRequired ) noexcept
{
    usize cbCapacity = document.cbNextData;
    if ( cbCapacity < cbRequired ) {
        cbCapacity = cbRequired;
    }
    usize cbAllocation = 0u;
    if ( !DataAllocationSize( cbCapacity, cbAllocation ) ) {
        return nullptr;
    }
    void *pStorage = Allocator_Allocate(
        document.pAllocator,
        cbAllocation,
        alignof( key_value_data_block_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }
    auto *pBlock = new ( pStorage ) key_value_data_block_t{};
    pBlock->pNext = document.pDataBlocks;
    pBlock->cbCapacity = cbCapacity;
    document.pDataBlocks = pBlock;
    document.cbNextData = cbCapacity <= CY_USIZE_MAX / 2u
        ? cbCapacity * 2u
        : cbCapacity;
    return pBlock;
}

CYPHER_NODISCARD byte *AllocateData(
    key_value_document_t &document,
    usize cbRequired ) noexcept
{
    if ( cbRequired == 0u || cbRequired > CY_USIZE_MAX - document.cbData ) {
        return nullptr;
    }
    key_value_data_block_t *pBlock = document.pDataBlocks;
    if ( pBlock == nullptr ||
         cbRequired > pBlock->cbCapacity - pBlock->cbUsed ) {
        pBlock = AllocateDataBlock( document, cbRequired );
        if ( pBlock == nullptr ) {
            return nullptr;
        }
    }
    byte *pData = DataBlockData( pBlock ) + pBlock->cbUsed;
    pBlock->cbUsed += cbRequired;
    document.cbData += cbRequired;
    return pData;
}

CYPHER_NODISCARD const char *CopyString(
    key_value_document_t &document,
    string_view_t text ) noexcept
{
    if ( !StringView_IsValid( text ) || text.cchLength == CY_USIZE_MAX ) {
        return nullptr;
    }
    byte *pCopy = AllocateData( document, text.cchLength + 1u );
    if ( pCopy == nullptr ) {
        return nullptr;
    }
    if ( text.cchLength != 0u ) {
        Cy_MemCopy( pCopy, text.pData, text.cchLength );
    }
    pCopy[text.cchLength] = 0u;
    return reinterpret_cast<const char *>( pCopy );
}

CYPHER_NODISCARD const byte *CopyBinary(
    key_value_document_t &document,
    binary_block_t value ) noexcept
{
    if ( !BinaryBlock_IsValid( value ) ) {
        return nullptr;
    }
    if ( value.cbSize == 0u ) {
        return reinterpret_cast<const byte *>( "" );
    }
    byte *pCopy = AllocateData( document, value.cbSize );
    if ( pCopy == nullptr ) {
        return nullptr;
    }
    Cy_MemCopy( pCopy, value.pData, value.cbSize );
    return pCopy;
}

CYPHER_NODISCARD bool_t BelongsTo(
    const key_value_document_t *pDocument,
    const key_value_t *pValue ) noexcept
{
    return KeyValue_InternalDocumentIsValid( pDocument ) &&
           pValue != nullptr &&
           pValue->pDocument == pDocument;
}

void RecycleSubtree(
    key_value_document_t &document,
    key_value_t *pNode ) noexcept
{
    key_value_t *pChild = pNode->pFirstChild;
    while ( pChild != nullptr ) {
        key_value_t *pNext = pChild->pNext;
        RecycleSubtree( document, pChild );
        pChild = pNext;
    }
    *pNode = {};
    pNode->pDocument = &document;
    pNode->pNext = document.pFreeNodes;
    document.pFreeNodes = pNode;
    --document.nNodes;
}

void ResetValue( key_value_document_t &document, key_value_t &value ) noexcept
{
    key_value_t *pChild = value.pFirstChild;
    while ( pChild != nullptr ) {
        key_value_t *pNext = pChild->pNext;
        RecycleSubtree( document, pChild );
        pChild = pNext;
    }
    value.pFirstChild = nullptr;
    value.pLastChild = nullptr;
    value.nChildren = 0u;
    value.value = {};
}

void AppendChild( key_value_t &parent, key_value_t &child ) noexcept
{
    child.pParent = &parent;
    child.pPrevious = parent.pLastChild;
    if ( parent.pLastChild != nullptr ) {
        parent.pLastChild->pNext = &child;
    } else {
        parent.pFirstChild = &child;
    }
    parent.pLastChild = &child;
    ++parent.nChildren;
}

CYPHER_NODISCARD bool_t NamesEqual(
    const key_value_document_t &document,
    string_view_t left,
    string_view_t right ) noexcept
{
    return document.bCaseInsensitiveKeys
        ? StringView_EqualsInsensitiveAscii( left, right )
        : StringView_Equals( left, right );
}

CYPHER_NODISCARD bool_t SetScalarType(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    key_value_type_t type ) noexcept
{
    if ( !BelongsTo( pDocument, pValue ) || IsContainerType( type ) ||
         !IsValidType( type ) ) {
        return CY_FALSE;
    }
    ResetValue( *pDocument, *pValue );
    pValue->type = type;
    return CY_TRUE;
}

CYPHER_NODISCARD key_value_t *InsertChild(
    key_value_document_t *pDocument,
    key_value_t *pParent,
    string_view_t name,
    key_value_type_t type ) noexcept
{
    if ( !BelongsTo( pDocument, pParent ) || !IsValidType( type ) ||
         !StringView_IsValid( name ) ) {
        return nullptr;
    }
    usize nParentDepth = 0u;
    for ( const key_value_t *pAncestor = pParent;
          pAncestor->pParent != nullptr;
          pAncestor = pAncestor->pParent ) {
        if ( ++nParentDepth >= CY_KEY_VALUE_MAX_DEPTH ) {
            return nullptr;
        }
    }
    key_value_t *pChild = AllocateNode( *pDocument );
    if ( pChild == nullptr ) {
        return nullptr;
    }
    if ( name.cchLength != 0u ) {
        pChild->pName = CopyString( *pDocument, name );
        if ( pChild->pName == nullptr ) {
            RecycleSubtree( *pDocument, pChild );
            return nullptr;
        }
        pChild->cchName = name.cchLength;
    }
    pChild->type = type;
    AppendChild( *pParent, *pChild );
    return pChild;
}

CYPHER_NODISCARD bool_t CloneValue(
    key_value_document_t &document,
    key_value_t &dest,
    const key_value_t &source ) noexcept
{
    switch ( source.type ) {
        case key_value_type_t::NULL_VALUE:
            return KeyValue_SetNull( &document, &dest );
        case key_value_type_t::BOOL:
            return KeyValue_SetBool( &document, &dest, source.value.bValue );
        case key_value_type_t::I64:
            return KeyValue_SetI64( &document, &dest, source.value.iValue );
        case key_value_type_t::U64:
            return KeyValue_SetU64( &document, &dest, source.value.uValue );
        case key_value_type_t::F64:
            return KeyValue_SetF64( &document, &dest, source.value.flValue );
        case key_value_type_t::STRING:
            return KeyValue_SetString(
                &document,
                &dest,
                {
                    reinterpret_cast<const char *>( source.value.bytes.pData ),
                    source.value.bytes.cbSize
                } );
        case key_value_type_t::BINARY:
            return KeyValue_SetBinary(
                &document,
                &dest,
                { source.value.bytes.pData, source.value.bytes.cbSize } );
        case key_value_type_t::OBJECT:
        case key_value_type_t::ARRAY:
            if ( !KeyValue_SetContainerType( &document, &dest, source.type ) ) {
                return CY_FALSE;
            }
            for ( const key_value_t *pChild = source.pFirstChild;
                  pChild != nullptr;
                  pChild = pChild->pNext ) {
                key_value_t *pCopy = source.type == key_value_type_t::OBJECT
                    ? KeyValue_ObjectInsert(
                        &document,
                        &dest,
                        { pChild->pName, pChild->cchName },
                        pChild->type )
                    : KeyValue_ArrayAppend( &document, &dest, pChild->type );
                if ( pCopy == nullptr || !CloneValue( document, *pCopy, *pChild ) ) {
                    return CY_FALSE;
                }
            }
            return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD bool_t ValidateSubtree(
    const key_value_t *pNode,
    const key_value_document_t *pDocument,
    usize &nVisited ) noexcept
{
    if ( pNode == nullptr || pDocument == nullptr ) return CY_FALSE;
    const key_value_t *pRoot = pNode;
    usize nDepth = 0u;
    while ( pNode != nullptr ) {
        if ( pNode->pDocument != pDocument ||
             !IsValidType( pNode->type ) ||
             ++nVisited > pDocument->nNodes ||
             ( pNode->cchName != 0u && pNode->pName == nullptr ) ) {
            return CY_FALSE;
        }
        const bool_t bContainer = IsContainerType( pNode->type );
        if ( !bContainer &&
             ( pNode->pFirstChild != nullptr ||
               pNode->pLastChild != nullptr ||
               pNode->nChildren != 0u ) ) {
            return CY_FALSE;
        }
        if ( bContainer &&
             ( ( pNode->nChildren == 0u ) !=
               ( pNode->pFirstChild == nullptr ) ||
               ( pNode->nChildren == 0u ) !=
               ( pNode->pLastChild == nullptr ) ) ) {
            return CY_FALSE;
        }
        if ( pNode->type == key_value_type_t::STRING &&
             pNode->value.bytes.pData == nullptr ) {
            return CY_FALSE;
        }
        if ( pNode->type == key_value_type_t::BINARY &&
             pNode->value.bytes.cbSize != 0u &&
             pNode->value.bytes.pData == nullptr ) {
            return CY_FALSE;
        }
        if ( pNode->type == key_value_type_t::F64 &&
             !std::isfinite( pNode->value.flValue ) ) {
            return CY_FALSE;
        }

        usize nChildren = 0u;
        const key_value_t *pPrevious = nullptr;
        for ( const key_value_t *pChild = pNode->pFirstChild;
              pChild != nullptr;
              pChild = pChild->pNext ) {
            if ( ++nChildren > pDocument->nNodes ||
                 pChild->pParent != pNode ||
                 pChild->pPrevious != pPrevious ||
                 ( pNode->type == key_value_type_t::ARRAY &&
                   ( pChild->cchName != 0u || pChild->pName != nullptr ) ) ) {
                return CY_FALSE;
            }
            pPrevious = pChild;
        }
        if ( pPrevious != pNode->pLastChild ||
             nChildren != pNode->nChildren ) {
            return CY_FALSE;
        }

        if ( pNode->pFirstChild != nullptr ) {
            if ( ++nDepth > CY_KEY_VALUE_MAX_DEPTH ) return CY_FALSE;
            pNode = pNode->pFirstChild;
            continue;
        }
        while ( pNode != pRoot && pNode->pNext == nullptr ) {
            pNode = pNode->pParent;
            if ( nDepth == 0u ) return CY_FALSE;
            --nDepth;
        }
        pNode = pNode == pRoot ? nullptr : pNode->pNext;
    }
    return CY_TRUE;
}

void RewriteOwnership(
    key_value_t *pNode,
    key_value_document_t *pDocument ) noexcept
{
    pNode->pDocument = pDocument;
    for ( key_value_t *pChild = pNode->pFirstChild;
          pChild != nullptr;
          pChild = pChild->pNext ) {
        pChild->pParent = pNode;
        RewriteOwnership( pChild, pDocument );
    }
}

} // namespace

key_value_document_t *KeyValue_CreateDocument(
    const key_value_document_desc_t &desc ) noexcept
{
    const allocator_t *pAllocator = desc.pAllocator != nullptr
        ? desc.pAllocator
        : Allocator_GetSystem();
    if ( !Allocator_IsValid( pAllocator ) || desc.nInitialNodes == 0u ||
         desc.cbInitialStrings == 0u ) {
        return nullptr;
    }
    void *pStorage = Allocator_Allocate(
        pAllocator,
        sizeof( key_value_document_t ),
        alignof( key_value_document_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }
    auto *pDocument = new ( pStorage ) key_value_document_t{};
    pDocument->pAllocator = pAllocator;
    pDocument->nInitialNodes = desc.nInitialNodes;
    pDocument->nNextNodes = desc.nInitialNodes;
    pDocument->cbInitialData = desc.cbInitialStrings;
    pDocument->cbNextData = desc.cbInitialStrings;
    pDocument->bCaseInsensitiveKeys = desc.bCaseInsensitiveKeys;
    pDocument->nMagic = CY_KEY_VALUE_DOCUMENT_MAGIC;
    InitializeRoot( *pDocument );
    return pDocument;
}

void KeyValue_DestroyDocument( key_value_document_t *pDocument ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) ) {
        return;
    }
    const allocator_t *pAllocator = pDocument->pAllocator;
    FreeNodeBlocks( *pDocument );
    FreeDataBlocks( *pDocument );
    pDocument->nMagic = 0u;
    pDocument->~key_value_document_t();
    Allocator_Free(
        pAllocator,
        pDocument,
        sizeof( key_value_document_t ),
        alignof( key_value_document_t ) );
}

void KeyValue_ClearDocument( key_value_document_t *pDocument ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) ) {
        return;
    }
    FreeNodeBlocks( *pDocument );
    FreeDataBlocks( *pDocument );
    pDocument->nNextNodes = pDocument->nInitialNodes;
    pDocument->cbNextData = pDocument->cbInitialData;
    pDocument->pSchemaId = nullptr;
    pDocument->cchSchemaId = 0u;
    pDocument->nLanguageVersion = 0u;
    pDocument->nSchemaVersion = 0u;
    InitializeRoot( *pDocument );
}

bool_t KeyValue_SetDocumentHeader(
    key_value_document_t *pDocument,
    const key_value_document_header_t &header ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) ||
         header.nLanguageVersion == 0u ||
         header.nSchemaVersion == 0u ||
         !IsValidSchemaId( header.schemaId ) ) {
        return CY_FALSE;
    }

    const char *pSchemaId = CopyString( *pDocument, header.schemaId );
    if ( pSchemaId == nullptr ) {
        return CY_FALSE;
    }

    pDocument->pSchemaId = pSchemaId;
    pDocument->cchSchemaId = header.schemaId.cchLength;
    pDocument->nLanguageVersion = header.nLanguageVersion;
    pDocument->nSchemaVersion = header.nSchemaVersion;
    return CY_TRUE;
}

key_value_document_header_t KeyValue_DocumentHeader(
    const key_value_document_t *pDocument ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) ||
         pDocument->pSchemaId == nullptr ) {
        return {};
    }

    return {
        pDocument->nLanguageVersion,
        { pDocument->pSchemaId, pDocument->cchSchemaId },
        pDocument->nSchemaVersion
    };
}

key_value_t *KeyValue_Root( key_value_document_t *pDocument ) noexcept
{
    return KeyValue_InternalDocumentIsValid( pDocument )
        ? &pDocument->root
        : nullptr;
}

const key_value_t *KeyValue_Root(
    const key_value_document_t *pDocument ) noexcept
{
    return KeyValue_InternalDocumentIsValid( pDocument )
        ? &pDocument->root
        : nullptr;
}

bool_t KeyValue_SetRootType(
    key_value_document_t *pDocument,
    key_value_type_t type ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) || !IsValidType( type ) ) {
        return CY_FALSE;
    }
    ResetValue( *pDocument, pDocument->root );
    pDocument->root.type = type;
    return CY_TRUE;
}

key_value_type_t KeyValue_Type( const key_value_t *pValue ) noexcept
{
    return pValue != nullptr ? pValue->type : key_value_type_t::NULL_VALUE;
}

string_view_t KeyValue_Name( const key_value_t *pValue ) noexcept
{
    return pValue != nullptr
        ? string_view_t{ pValue->pName, pValue->cchName }
        : string_view_t{};
}

bool_t KeyValue_GetBool( const key_value_t *pValue, bool_t *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::BOOL ) return CY_FALSE;
    *pOut = pValue->value.bValue;
    return CY_TRUE;
}

bool_t KeyValue_GetI64( const key_value_t *pValue, i64 *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::I64 ) return CY_FALSE;
    *pOut = pValue->value.iValue;
    return CY_TRUE;
}

bool_t KeyValue_GetU64( const key_value_t *pValue, u64 *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::U64 ) return CY_FALSE;
    *pOut = pValue->value.uValue;
    return CY_TRUE;
}

bool_t KeyValue_GetF64( const key_value_t *pValue, f64 *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::F64 ) return CY_FALSE;
    *pOut = pValue->value.flValue;
    return CY_TRUE;
}

bool_t KeyValue_GetString(
    const key_value_t *pValue,
    string_view_t *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::STRING ) return CY_FALSE;
    *pOut = {
        reinterpret_cast<const char *>( pValue->value.bytes.pData ),
        pValue->value.bytes.cbSize
    };
    return CY_TRUE;
}

bool_t KeyValue_GetBinary(
    const key_value_t *pValue,
    binary_block_t *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr ||
         pValue->type != key_value_type_t::BINARY ) return CY_FALSE;
    *pOut = { pValue->value.bytes.pData, pValue->value.bytes.cbSize };
    return CY_TRUE;
}

bool_t KeyValue_SetNull(
    key_value_document_t *pDocument,
    key_value_t *pValue ) noexcept
{
    return SetScalarType( pDocument, pValue, key_value_type_t::NULL_VALUE );
}

bool_t KeyValue_SetBool(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    bool_t value ) noexcept
{
    if ( !SetScalarType( pDocument, pValue, key_value_type_t::BOOL ) ) return CY_FALSE;
    pValue->value.bValue = value ? CY_TRUE : CY_FALSE;
    return CY_TRUE;
}

bool_t KeyValue_SetI64(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    i64 value ) noexcept
{
    if ( !SetScalarType( pDocument, pValue, key_value_type_t::I64 ) ) return CY_FALSE;
    pValue->value.iValue = value;
    return CY_TRUE;
}

bool_t KeyValue_SetU64(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    u64 value ) noexcept
{
    if ( !SetScalarType( pDocument, pValue, key_value_type_t::U64 ) ) return CY_FALSE;
    pValue->value.uValue = value;
    return CY_TRUE;
}

bool_t KeyValue_SetF64(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    f64 value ) noexcept
{
    if ( !std::isfinite( value ) ||
         !SetScalarType( pDocument, pValue, key_value_type_t::F64 ) ) return CY_FALSE;
    pValue->value.flValue = value;
    return CY_TRUE;
}

bool_t KeyValue_SetString(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    string_view_t value ) noexcept
{
    if ( !BelongsTo( pDocument, pValue ) || !StringView_IsValid( value ) ) {
        return CY_FALSE;
    }
    const char *pCopy = CopyString( *pDocument, value );
    if ( pCopy == nullptr ) {
        return CY_FALSE;
    }
    ResetValue( *pDocument, *pValue );
    pValue->type = key_value_type_t::STRING;
    pValue->value.bytes = {
        reinterpret_cast<const byte *>( pCopy ),
        value.cchLength
    };
    return CY_TRUE;
}

bool_t KeyValue_SetBinary(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    binary_block_t value ) noexcept
{
    if ( !BelongsTo( pDocument, pValue ) || !BinaryBlock_IsValid( value ) ) {
        return CY_FALSE;
    }
    const byte *pCopy = CopyBinary( *pDocument, value );
    if ( pCopy == nullptr ) {
        return CY_FALSE;
    }
    ResetValue( *pDocument, *pValue );
    pValue->type = key_value_type_t::BINARY;
    pValue->value.bytes = { pCopy, value.cbSize };
    return CY_TRUE;
}

bool_t KeyValue_SetContainerType(
    key_value_document_t *pDocument,
    key_value_t *pValue,
    key_value_type_t type ) noexcept
{
    if ( !BelongsTo( pDocument, pValue ) || !IsContainerType( type ) ) {
        return CY_FALSE;
    }
    ResetValue( *pDocument, *pValue );
    pValue->type = type;
    return CY_TRUE;
}

usize KeyValue_ChildCount( const key_value_t *pContainer ) noexcept
{
    return pContainer != nullptr && IsContainerType( pContainer->type )
        ? pContainer->nChildren
        : 0u;
}

key_value_t *KeyValue_ChildAt(
    key_value_t *pContainer,
    usize iChild ) noexcept
{
    if ( pContainer == nullptr || !IsContainerType( pContainer->type ) ||
         iChild >= pContainer->nChildren ) return nullptr;
    key_value_t *pChild = pContainer->pFirstChild;
    while ( iChild-- != 0u ) pChild = pChild->pNext;
    return pChild;
}

const key_value_t *KeyValue_ChildAt(
    const key_value_t *pContainer,
    usize iChild ) noexcept
{
    return KeyValue_ChildAt( const_cast<key_value_t *>( pContainer ), iChild );
}

key_value_t *KeyValue_Find(
    key_value_t *pObject,
    string_view_t name ) noexcept
{
    if ( pObject == nullptr || pObject->type != key_value_type_t::OBJECT ||
         !StringView_IsValid( name ) ) return nullptr;
    for ( key_value_t *pChild = pObject->pFirstChild;
          pChild != nullptr;
          pChild = pChild->pNext ) {
        if ( NamesEqual(
                 *pObject->pDocument,
                 { pChild->pName, pChild->cchName },
                 name ) ) return pChild;
    }
    return nullptr;
}

const key_value_t *KeyValue_Find(
    const key_value_t *pObject,
    string_view_t name ) noexcept
{
    return KeyValue_Find( const_cast<key_value_t *>( pObject ), name );
}

key_value_t *KeyValue_ObjectInsert(
    key_value_document_t *pDocument,
    key_value_t *pObject,
    string_view_t name,
    key_value_type_t type ) noexcept
{
    return pObject != nullptr && pObject->type == key_value_type_t::OBJECT
        ? InsertChild( pDocument, pObject, name, type )
        : nullptr;
}

key_value_t *KeyValue_ArrayAppend(
    key_value_document_t *pDocument,
    key_value_t *pArray,
    key_value_type_t type ) noexcept
{
    return pArray != nullptr && pArray->type == key_value_type_t::ARRAY
        ? InsertChild( pDocument, pArray, {}, type )
        : nullptr;
}

bool_t KeyValue_Remove(
    key_value_document_t *pDocument,
    key_value_t *pParent,
    key_value_t *pChild ) noexcept
{
    if ( !BelongsTo( pDocument, pParent ) ||
         !BelongsTo( pDocument, pChild ) || pChild->pParent != pParent ) {
        return CY_FALSE;
    }
    if ( pChild->pPrevious != nullptr ) {
        pChild->pPrevious->pNext = pChild->pNext;
    } else {
        pParent->pFirstChild = pChild->pNext;
    }
    if ( pChild->pNext != nullptr ) {
        pChild->pNext->pPrevious = pChild->pPrevious;
    } else {
        pParent->pLastChild = pChild->pPrevious;
    }
    --pParent->nChildren;
    RecycleSubtree( *pDocument, pChild );
    return CY_TRUE;
}

key_value_t *KeyValue_CloneInto(
    key_value_document_t *pDestDocument,
    key_value_t *pDestParent,
    const key_value_t *pSource ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDestDocument ) ||
         !KeyValue_InternalTreeIsValid( pSource ) ||
         ( pDestParent != nullptr &&
           !BelongsTo( pDestDocument, pDestParent ) ) ) return nullptr;
    key_value_t *pDest = nullptr;
    if ( pDestParent == nullptr ) {
        pDest = &pDestDocument->root;
    } else if ( pDestParent->type == key_value_type_t::OBJECT ) {
        pDest = KeyValue_ObjectInsert(
            pDestDocument,
            pDestParent,
            { pSource->pName, pSource->cchName },
            pSource->type );
    } else if ( pDestParent->type == key_value_type_t::ARRAY ) {
        pDest = KeyValue_ArrayAppend(
            pDestDocument,
            pDestParent,
            pSource->type );
    }
    if ( pDest == nullptr || !CloneValue( *pDestDocument, *pDest, *pSource ) ) {
        if ( pDestParent != nullptr && pDest != nullptr ) {
            static_cast<void>( KeyValue_Remove(
                pDestDocument,
                pDestParent,
                pDest ) );
        } else if ( pDestParent == nullptr ) {
            static_cast<void>( KeyValue_SetNull(
                pDestDocument,
                &pDestDocument->root ) );
        }
        return nullptr;
    }
    return pDest;
}

bool_t KeyValue_InternalDocumentIsValid(
    const key_value_document_t *pDocument ) noexcept
{
    return pDocument != nullptr &&
           pDocument->nMagic == CY_KEY_VALUE_DOCUMENT_MAGIC &&
           Allocator_IsValid( pDocument->pAllocator ) &&
           pDocument->nInitialNodes != 0u &&
           pDocument->nNextNodes != 0u &&
           pDocument->cbInitialData != 0u &&
           pDocument->cbNextData != 0u &&
           pDocument->root.pDocument == pDocument &&
           pDocument->root.pParent == nullptr &&
           pDocument->nNodes >= 1u;
}

bool_t KeyValue_InternalTreeIsValid( const key_value_t *pRoot ) noexcept
{
    if ( pRoot == nullptr ||
         !KeyValue_InternalDocumentIsValid( pRoot->pDocument ) ) {
        return CY_FALSE;
    }
    usize nVisited = 0u;
    return ValidateSubtree( pRoot, pRoot->pDocument, nVisited );
}

key_value_document_t *KeyValue_InternalCreateLike(
    const key_value_document_t *pDocument ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDocument ) ) {
        return nullptr;
    }
    return KeyValue_CreateDocument({
        pDocument->pAllocator,
        pDocument->nInitialNodes,
        pDocument->cbInitialData,
        pDocument->bCaseInsensitiveKeys
    });
}

void KeyValue_InternalMoveDocumentContents(
    key_value_document_t *pDest,
    key_value_document_t *pSource ) noexcept
{
    if ( !KeyValue_InternalDocumentIsValid( pDest ) ||
         !KeyValue_InternalDocumentIsValid( pSource ) || pDest == pSource ) {
        return;
    }
    KeyValue_ClearDocument( pDest );
    pDest->pNodeBlocks = pSource->pNodeBlocks;
    pDest->pDataBlocks = pSource->pDataBlocks;
    pDest->pFreeNodes = pSource->pFreeNodes;
    pDest->nNextNodes = pSource->nNextNodes;
    pDest->cbNextData = pSource->cbNextData;
    pDest->nNodes = pSource->nNodes;
    pDest->cbData = pSource->cbData;
    pDest->pSchemaId = pSource->pSchemaId;
    pDest->cchSchemaId = pSource->cchSchemaId;
    pDest->nLanguageVersion = pSource->nLanguageVersion;
    pDest->nSchemaVersion = pSource->nSchemaVersion;
    pDest->root = pSource->root;
    pDest->root.pParent = nullptr;
    RewriteOwnership( &pDest->root, pDest );

    pSource->pNodeBlocks = nullptr;
    pSource->pDataBlocks = nullptr;
    pSource->pFreeNodes = nullptr;
    pSource->nNextNodes = pSource->nInitialNodes;
    pSource->cbNextData = pSource->cbInitialData;
    pSource->cbData = 0u;
    pSource->pSchemaId = nullptr;
    pSource->cchSchemaId = 0u;
    pSource->nLanguageVersion = 0u;
    pSource->nSchemaVersion = 0u;
    InitializeRoot( *pSource );
}

usize KeyValue_InternalNodeCount(
    const key_value_document_t *pDocument ) noexcept
{
    return KeyValue_InternalDocumentIsValid( pDocument )
        ? pDocument->nNodes
        : 0u;
}

usize KeyValue_InternalDataSize(
    const key_value_document_t *pDocument ) noexcept
{
    return KeyValue_InternalDocumentIsValid( pDocument )
        ? pDocument->cbData
        : 0u;
}

} // namespace cypher::common
