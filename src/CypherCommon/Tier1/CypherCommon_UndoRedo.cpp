//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UndoRedo.cpp
//  Purpose: Implements bounded command-based undo and redo history.
//  Details: Entries own copied payloads and labels. A cursor separates applied and
//           redo operations, while group IDs make transactions and merge keys atomic.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_UndoRedo.h"

#include <new>

namespace cypher::common
{

namespace
{

struct owned_undo_operation_t {
    undo_operation_id_t id{ 0u };       // Caller identity retained for diagnostics.
    u64 nMergeKey{ 0u };                // Nonzero key used to coalesce adjacent pushes.
    u64 nGroup{ 0u };                   // Atomic undo/redo group identifier.
    char *pLabel{ nullptr };             // Owned NUL-terminated display label.
    usize cchLabel{ 0u };                // Label length excluding the terminator.
    byte *pPayload{ nullptr };           // Owned opaque bytes passed to callbacks.
    usize cbPayload{ 0u };               // Payload allocation size and budget charge.
    undo_apply_fn_t pfnUndo{ nullptr };  // Inverse callback supplied by the caller.
    undo_apply_fn_t pfnRedo{ nullptr };  // Forward callback supplied by the caller.
    void *pUserData{ nullptr };          // Borrowed callback context.
};

error_code_t CommonError( common_error_t error ) noexcept
{
    return Cy_ErrorMake( error );
}

bool_t CopyBytes(
    const allocator_t *pAllocator,
    const void *pSource,
    usize cbSize,
    void **ppCopyOut ) noexcept
{
    // Every recorded operation owns a stable payload independent of caller storage.
    *ppCopyOut = nullptr;
    if ( cbSize == 0u ) {
        return CY_TRUE;
    }
    void *pCopy = Allocator_Allocate(
        pAllocator,
        cbSize,
        alignof( byte ) );
    if ( pCopy == nullptr ) {
        return CY_FALSE;
    }
    Cy_MemCopy( pCopy, pSource, cbSize );
    *ppCopyOut = pCopy;
    return CY_TRUE;
}

} // namespace

struct undo_history_t {
    owned_undo_operation_t *pOperations{ nullptr }; // Contiguous operation journal.
    usize nCount{ 0u };                             // Total undo and redo entries.
    usize nCapacity{ 0u };                          // Allocated operation slots.
    usize iCursor{ 0u };                            // Applied entries are [0, iCursor).
    usize cbPayloads{ 0u };                         // Bytes owned by all entry payloads.
    usize nMaxOperations{ 0u };                     // Hard entry budget.
    usize cbMaxPayloads{ 0u };                      // Hard payload-byte budget.
    const allocator_t *pAllocator{ nullptr };       // Borrowed allocator contract.
    u64 nNextGroup{ 1u };                           // Next nonzero atomic group ID.
    bool_t bTransactionOpen{ CY_FALSE };             // Pushes currently share one group.
    bool_t bApplying{ CY_FALSE };                    // Guards callback re-entry.
    usize iTransactionStart{ 0u };                  // First entry recorded by transaction.
    u64 nTransactionGroup{ 0u };                    // Group assigned at begin time.
    char *pTransactionLabel{ nullptr };              // Owned label until first push.
    usize cchTransactionLabel{ 0u };                // Transaction label byte count.
    bool_t bTransactionLabelTransferred{ CY_FALSE }; // First entry now owns label copy.
};

namespace
{

bool_t HistoryIsValid( const undo_history_t *pHistory ) noexcept
{
    return pHistory != nullptr &&
           Allocator_IsValid( pHistory->pAllocator ) &&
           pHistory->nMaxOperations != 0u &&
           pHistory->nCount <= pHistory->nCapacity &&
           pHistory->nCapacity <= pHistory->nMaxOperations &&
           pHistory->iCursor <= pHistory->nCount &&
           pHistory->cbPayloads <= pHistory->cbMaxPayloads;
}

void FreeLabel(
    undo_history_t &history,
    char *pLabel,
    usize cchLabel ) noexcept
{
    if ( pLabel != nullptr ) {
        Allocator_Free(
            history.pAllocator,
            pLabel,
            cchLabel + 1u,
            alignof( char ) );
    }
}

void FreeOperation(
    undo_history_t &history,
    owned_undo_operation_t &operation ) noexcept
{
    FreeLabel( history, operation.pLabel, operation.cchLabel );
    Allocator_Free(
        history.pAllocator,
        operation.pPayload,
        operation.cbPayload,
        alignof( byte ) );
    operation = {};
}

bool_t CopyLabel(
    undo_history_t &history,
    string_view_t label,
    char **ppLabelOut ) noexcept
{
    *ppLabelOut = nullptr;
    if ( label.cchLength == 0u ) {
        return CY_TRUE;
    }
    if ( label.cchLength == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    auto *pText = static_cast<char *>( Allocator_Allocate(
        history.pAllocator,
        label.cchLength + 1u,
        alignof( char ) ) );
    if ( pText == nullptr ) {
        return CY_FALSE;
    }
    Cy_MemCopy( pText, label.pData, label.cchLength );
    pText[label.cchLength] = '\0';
    *ppLabelOut = pText;
    return CY_TRUE;
}

bool_t ReserveOperations(
    undo_history_t &history,
    usize nCapacity ) noexcept
{
    if ( nCapacity <= history.nCapacity ) {
        return CY_TRUE;
    }
    if ( nCapacity > history.nMaxOperations ||
         nCapacity > CY_USIZE_MAX / sizeof( owned_undo_operation_t ) ) {
        return CY_FALSE;
    }
    const usize cbOld = history.nCapacity * sizeof( owned_undo_operation_t );
    const usize cbNew = nCapacity * sizeof( owned_undo_operation_t );
    // Reallocate first so allocation failure leaves the original journal intact.
    void *pMemory = Allocator_Reallocate(
        history.pAllocator,
        history.pOperations,
        cbOld,
        cbNew,
        alignof( owned_undo_operation_t ) );
    if ( pMemory == nullptr ) {
        return CY_FALSE;
    }
    history.pOperations = static_cast<owned_undo_operation_t *>( pMemory );
    Cy_MemZero(
        history.pOperations + history.nCapacity,
        ( nCapacity - history.nCapacity ) * sizeof( owned_undo_operation_t ) );
    history.nCapacity = nCapacity;
    return CY_TRUE;
}

void RemoveRange(
    undo_history_t &history,
    usize iFirst,
    usize iEnd ) noexcept
{
    if ( iFirst >= iEnd || iEnd > history.nCount ) {
        return;
    }
    // Destroy owned fields before compacting the trivially stored entry records.
    usize cbRemoved = 0u;
    for ( usize iOperation = iFirst; iOperation < iEnd; ++iOperation ) {
        cbRemoved += history.pOperations[iOperation].cbPayload;
        FreeOperation( history, history.pOperations[iOperation] );
    }
    const usize nTail = history.nCount - iEnd;
    if ( nTail != 0u ) {
        Cy_MemMove(
            history.pOperations + iFirst,
            history.pOperations + iEnd,
            nTail * sizeof( owned_undo_operation_t ) );
    }
    Cy_MemZero(
        history.pOperations + iFirst + nTail,
        ( iEnd - iFirst ) * sizeof( owned_undo_operation_t ) );
    history.nCount -= iEnd - iFirst;
    // Removing old or redo entries must rebase every index into the journal.
    if ( history.bTransactionOpen ) {
        history.iTransactionStart = history.iTransactionStart <= iFirst
            ? history.iTransactionStart
            : ( history.iTransactionStart < iEnd
                ? iFirst
                : history.iTransactionStart - ( iEnd - iFirst ) );
    }
    history.iCursor = history.iCursor <= iFirst
        ? history.iCursor
        : ( history.iCursor < iEnd
            ? iFirst
            : history.iCursor - ( iEnd - iFirst ) );
    history.cbPayloads -= cbRemoved;
}

void RemoveRedo( undo_history_t &history ) noexcept
{
    RemoveRange( history, history.iCursor, history.nCount );
}

bool_t RebaseGroups( undo_history_t &history ) noexcept
{
    // Group IDs carry ordering only; compact them before the 64-bit counter wraps.
    u64 nGroup = 0u;
    u64 nPrevious = 0u;
    for ( usize iOperation = 0u; iOperation < history.nCount; ++iOperation ) {
        if ( iOperation == 0u ||
             history.pOperations[iOperation].nGroup != nPrevious ) {
            if ( nGroup == CY_U64_MAX ) {
                return CY_FALSE;
            }
            ++nGroup;
            nPrevious = history.pOperations[iOperation].nGroup;
        }
        history.pOperations[iOperation].nGroup = nGroup;
    }
    history.nNextGroup = nGroup + 1u;
    return history.nNextGroup != 0u;
}

u64 AllocateGroup( undo_history_t &history ) noexcept
{
    if ( history.nNextGroup == 0u || history.nNextGroup == CY_U64_MAX ) {
        if ( !RebaseGroups( history ) ) {
            return 0u;
        }
    }
    return history.nNextGroup++;
}

usize FirstOperationInGroup(
    const undo_history_t &history,
    usize iOperation ) noexcept
{
    const u64 nGroup = history.pOperations[iOperation].nGroup;
    while ( iOperation > 0u &&
            history.pOperations[iOperation - 1u].nGroup == nGroup ) {
        --iOperation;
    }
    return iOperation;
}

void EvictOldestGroups( undo_history_t &history ) noexcept
{
    // A group is atomic: budget enforcement never leaves half a transaction behind.
    while ( history.nCount > history.nMaxOperations ||
            history.cbPayloads > history.cbMaxPayloads ) {
        if ( history.nCount == 0u ) {
            return;
        }
        const u64 nOldestGroup = history.pOperations[0].nGroup;
        usize iEnd = 1u;
        while ( iEnd < history.nCount &&
                history.pOperations[iEnd].nGroup == nOldestGroup ) {
            ++iEnd;
        }
        RemoveRange( history, 0u, iEnd );
    }
}

bool_t MakeRoomForOperation(
    undo_history_t &history,
    usize cbPayload,
    u64 nProtectedGroup ) noexcept
{
    // The group being extended cannot be evicted while its new entry is prepared.
    while ( history.nCount >= history.nMaxOperations ||
            cbPayload > history.cbMaxPayloads - history.cbPayloads ) {
        if ( history.nCount == 0u ) {
            return CY_FALSE;
        }
        const u64 nOldestGroup = history.pOperations[0].nGroup;
        if ( nOldestGroup == nProtectedGroup ) {
            return CY_FALSE;
        }
        usize iEnd = 1u;
        while ( iEnd < history.nCount &&
                history.pOperations[iEnd].nGroup == nOldestGroup ) {
            ++iEnd;
        }
        RemoveRange( history, 0u, iEnd );
    }
    return CY_TRUE;
}

string_view_t GroupLabel(
    const undo_history_t *pHistory,
    usize iOperation ) noexcept
{
    if ( !HistoryIsValid( pHistory ) || iOperation >= pHistory->nCount ) {
        return {};
    }
    const usize iFirst = FirstOperationInGroup( *pHistory, iOperation );
    const owned_undo_operation_t &operation = pHistory->pOperations[iFirst];
    return { operation.pLabel, operation.cchLabel };
}

} // namespace

undo_history_t *UndoRedo_Create(
    const undo_history_desc_t &desc ) noexcept
{
    if ( !Allocator_IsValid( desc.pAllocator ) || desc.nMaxOperations == 0u ) {
        return nullptr;
    }
    void *pStorage = Allocator_Allocate(
        desc.pAllocator,
        sizeof( undo_history_t ),
        alignof( undo_history_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }
    auto *pHistory = ::new ( pStorage ) undo_history_t{};
    pHistory->pAllocator = desc.pAllocator;
    pHistory->nMaxOperations = desc.nMaxOperations;
    pHistory->cbMaxPayloads = desc.cbMaxPayloads;
    const usize nInitialCapacity = desc.nMaxOperations < 16u
        ? desc.nMaxOperations
        : 16u;
    if ( !ReserveOperations( *pHistory, nInitialCapacity ) ) {
        pHistory->~undo_history_t();
        Allocator_Free(
            desc.pAllocator,
            pStorage,
            sizeof( undo_history_t ),
            alignof( undo_history_t ) );
        return nullptr;
    }
    return pHistory;
}

void UndoRedo_Destroy( undo_history_t *pHistory ) noexcept
{
    if ( pHistory == nullptr ) {
        return;
    }
    const bool_t bCanDestroy = HistoryIsValid( pHistory ) && !pHistory->bApplying;
    CY_ASSERT_MSG( bCanDestroy, "UndoRedo_Destroy cannot run during a callback." );
    if ( !bCanDestroy ) {
        return;
    }
    const allocator_t *pAllocator = pHistory->pAllocator;
    UndoRedo_Clear( pHistory );
    Allocator_Free(
        pAllocator,
        pHistory->pOperations,
        pHistory->nCapacity * sizeof( owned_undo_operation_t ),
        alignof( owned_undo_operation_t ) );
    pHistory->~undo_history_t();
    Allocator_Free(
        pAllocator,
        pHistory,
        sizeof( undo_history_t ),
        alignof( undo_history_t ) );
}

void UndoRedo_Clear( undo_history_t *pHistory ) noexcept
{
    if ( !HistoryIsValid( pHistory ) || pHistory->bApplying ) {
        return;
    }
    RemoveRange( *pHistory, 0u, pHistory->nCount );
    if ( !pHistory->bTransactionLabelTransferred ) {
        FreeLabel(
            *pHistory,
            pHistory->pTransactionLabel,
            pHistory->cchTransactionLabel );
    }
    pHistory->pTransactionLabel = nullptr;
    pHistory->cchTransactionLabel = 0u;
    pHistory->bTransactionOpen = CY_FALSE;
    pHistory->bTransactionLabelTransferred = CY_FALSE;
    pHistory->iTransactionStart = 0u;
    pHistory->nTransactionGroup = 0u;
}

bool_t UndoRedo_BeginTransaction(
    undo_history_t *pHistory,
    string_view_t label ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bApplying ||
         pHistory->bTransactionOpen ||
         !StringView_IsValid( label ) ) {
        return CY_FALSE;
    }
    char *pLabel = nullptr;
    if ( !CopyLabel( *pHistory, label, &pLabel ) ) {
        return CY_FALSE;
    }
    // Reserve the group before any operation is pushed into the transaction.
    const u64 nGroup = AllocateGroup( *pHistory );
    if ( nGroup == 0u ) {
        FreeLabel( *pHistory, pLabel, label.cchLength );
        return CY_FALSE;
    }
    pHistory->bTransactionOpen = CY_TRUE;
    pHistory->iTransactionStart = pHistory->iCursor;
    pHistory->nTransactionGroup = nGroup;
    pHistory->pTransactionLabel = pLabel;
    pHistory->cchTransactionLabel = label.cchLength;
    pHistory->bTransactionLabelTransferred = CY_FALSE;
    return CY_TRUE;
}

bool_t UndoRedo_CommitTransaction( undo_history_t *pHistory ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bApplying ||
         !pHistory->bTransactionOpen ) {
        return CY_FALSE;
    }
    if ( !pHistory->bTransactionLabelTransferred ) {
        FreeLabel(
            *pHistory,
            pHistory->pTransactionLabel,
            pHistory->cchTransactionLabel );
    }
    pHistory->pTransactionLabel = nullptr;
    pHistory->cchTransactionLabel = 0u;
    pHistory->bTransactionOpen = CY_FALSE;
    pHistory->bTransactionLabelTransferred = CY_FALSE;
    pHistory->iTransactionStart = 0u;
    pHistory->nTransactionGroup = 0u;
    return CY_TRUE;
}

void UndoRedo_CancelTransaction(
    undo_history_t *pHistory ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bApplying ||
         !pHistory->bTransactionOpen ) {
        return;
    }
    // Cancelling removes journal records only; it does not invoke inverse callbacks.
    RemoveRange( *pHistory, pHistory->iTransactionStart, pHistory->nCount );
    if ( !pHistory->bTransactionLabelTransferred ) {
        FreeLabel(
            *pHistory,
            pHistory->pTransactionLabel,
            pHistory->cchTransactionLabel );
    }
    pHistory->pTransactionLabel = nullptr;
    pHistory->cchTransactionLabel = 0u;
    pHistory->bTransactionOpen = CY_FALSE;
    pHistory->bTransactionLabelTransferred = CY_FALSE;
    pHistory->iTransactionStart = 0u;
    pHistory->nTransactionGroup = 0u;
}

bool_t UndoRedo_Push(
    undo_history_t *pHistory,
    const undo_operation_desc_t &operation ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bApplying ||
         operation.id == 0u ||
         operation.pfnUndo == nullptr ||
         operation.pfnRedo == nullptr ||
         !StringView_IsValid( operation.label ) ||
         !BinaryBlock_IsValid( operation.payload ) ||
         operation.payload.cbSize > pHistory->cbMaxPayloads ) {
        return CY_FALSE;
    }

    // Verify the complete group can fit before copying or mutating history state.
    usize nActiveGroupCount = 1u;
    usize cbActiveGroup = operation.payload.cbSize;
    u64 nProtectedGroup = 0u;
    if ( pHistory->bTransactionOpen ) {
        nProtectedGroup = pHistory->nTransactionGroup;
        for ( usize iOperation = pHistory->iTransactionStart;
              iOperation < pHistory->iCursor;
              ++iOperation ) {
            ++nActiveGroupCount;
            if ( pHistory->pOperations[iOperation].cbPayload >
                 CY_USIZE_MAX - cbActiveGroup ) {
                return CY_FALSE;
            }
            cbActiveGroup += pHistory->pOperations[iOperation].cbPayload;
        }
    } else if ( operation.nMergeKey != 0u && pHistory->iCursor != 0u &&
                pHistory->pOperations[pHistory->iCursor - 1u].nMergeKey ==
                    operation.nMergeKey ) {
        nProtectedGroup = pHistory->pOperations[pHistory->iCursor - 1u].nGroup;
        usize iOperation = pHistory->iCursor;
        while ( iOperation != 0u &&
                pHistory->pOperations[iOperation - 1u].nGroup == nProtectedGroup ) {
            --iOperation;
            ++nActiveGroupCount;
            if ( pHistory->pOperations[iOperation].cbPayload >
                 CY_USIZE_MAX - cbActiveGroup ) {
                return CY_FALSE;
            }
            cbActiveGroup += pHistory->pOperations[iOperation].cbPayload;
        }
    }
    if ( nActiveGroupCount > pHistory->nMaxOperations ||
         cbActiveGroup > pHistory->cbMaxPayloads ) {
        return CY_FALSE;
    }

    // Copy caller-owned data before dropping redo entries or evicting old groups.
    char *pLabel = nullptr;
    byte *pPayload = nullptr;
    string_view_t displayLabel = operation.label;
    if ( pHistory->bTransactionOpen &&
         !pHistory->bTransactionLabelTransferred ) {
        displayLabel = {
            pHistory->pTransactionLabel,
            pHistory->cchTransactionLabel
        };
    }
    void *pPayloadCopy = nullptr;
    if ( !CopyLabel( *pHistory, displayLabel, &pLabel ) ||
         !CopyBytes(
             pHistory->pAllocator,
             operation.payload.pData,
             operation.payload.cbSize,
             &pPayloadCopy ) ) {
        FreeLabel( *pHistory, pLabel, displayLabel.cchLength );
        return CY_FALSE;
    }
    pPayload = static_cast<byte *>( pPayloadCopy );

    // A new branch invalidates every redo operation after the current cursor.
    if ( pHistory->iCursor < pHistory->nCount ) {
        RemoveRedo( *pHistory );
    }
    if ( !MakeRoomForOperation(
             *pHistory,
             operation.payload.cbSize,
             nProtectedGroup ) ) {
        FreeLabel( *pHistory, pLabel, displayLabel.cchLength );
        Allocator_Free(
            pHistory->pAllocator,
            pPayload,
            operation.payload.cbSize,
            alignof( byte ) );
        return CY_FALSE;
    }
    if ( pHistory->nCount == pHistory->nCapacity ) {
        usize nCapacity = pHistory->nCapacity;
        if ( nCapacity < pHistory->nMaxOperations ) {
            const usize nRemaining = pHistory->nMaxOperations - nCapacity;
            const usize nIncrease = nCapacity < 16u
                ? ( nCapacity == 0u ? 1u : nCapacity )
                : nCapacity / 2u;
            nCapacity += nIncrease < nRemaining ? nIncrease : nRemaining;
        }
        if ( !ReserveOperations( *pHistory, nCapacity ) ) {
            FreeLabel( *pHistory, pLabel, displayLabel.cchLength );
            Allocator_Free(
                pHistory->pAllocator,
                pPayload,
                operation.payload.cbSize,
                alignof( byte ) );
            return CY_FALSE;
        }
    }

    // Transactions and equal merge keys reuse a group; ordinary pushes allocate one.
    u64 nGroup = 0u;
    if ( pHistory->bTransactionOpen ) {
        nGroup = pHistory->nTransactionGroup;
    } else if ( operation.nMergeKey != 0u && pHistory->iCursor != 0u &&
                pHistory->pOperations[pHistory->iCursor - 1u].nMergeKey ==
                    operation.nMergeKey ) {
        nGroup = pHistory->pOperations[pHistory->iCursor - 1u].nGroup;
    } else {
        nGroup = AllocateGroup( *pHistory );
    }
    if ( nGroup == 0u ) {
        FreeLabel( *pHistory, pLabel, displayLabel.cchLength );
        Allocator_Free(
            pHistory->pAllocator,
            pPayload,
            operation.payload.cbSize,
            alignof( byte ) );
        return CY_FALSE;
    }

    pHistory->pOperations[pHistory->nCount++] = {
        operation.id,
        operation.nMergeKey,
        nGroup,
        pLabel,
        displayLabel.cchLength,
        pPayload,
        operation.payload.cbSize,
        operation.pfnUndo,
        operation.pfnRedo,
        operation.pUserData
    };
    pHistory->iCursor = pHistory->nCount;
    pHistory->cbPayloads += operation.payload.cbSize;
    // Only the first transaction entry carries the group display label.
    if ( pHistory->bTransactionOpen &&
         !pHistory->bTransactionLabelTransferred ) {
        FreeLabel(
            *pHistory,
            pHistory->pTransactionLabel,
            pHistory->cchTransactionLabel );
        pHistory->pTransactionLabel = nullptr;
        pHistory->cchTransactionLabel = 0u;
        pHistory->bTransactionLabelTransferred = CY_TRUE;
    }
    EvictOldestGroups( *pHistory );
    return CY_TRUE;
}

bool_t UndoRedo_CanUndo( const undo_history_t *pHistory ) noexcept
{
    return HistoryIsValid( pHistory ) &&
           !pHistory->bTransactionOpen &&
           !pHistory->bApplying &&
           pHistory->iCursor != 0u;
}

bool_t UndoRedo_CanRedo( const undo_history_t *pHistory ) noexcept
{
    return HistoryIsValid( pHistory ) &&
           !pHistory->bTransactionOpen &&
           !pHistory->bApplying &&
           pHistory->iCursor < pHistory->nCount;
}

error_code_t UndoRedo_Undo( undo_history_t *pHistory ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bTransactionOpen ||
         pHistory->bApplying ) {
        return CommonError( common_error_t::ERR_INVALID_STATE );
    }
    if ( pHistory->iCursor == 0u ) {
        return CommonError( common_error_t::ERR_NOT_FOUND );
    }

    const u64 nGroup = pHistory->pOperations[pHistory->iCursor - 1u].nGroup;
    pHistory->bApplying = CY_TRUE;
    // Undo runs newest to oldest so dependent edits are reversed correctly.
    while ( pHistory->iCursor != 0u ) {
        owned_undo_operation_t &operation =
            pHistory->pOperations[pHistory->iCursor - 1u];
        if ( operation.nGroup != nGroup ) {
            break;
        }
        const error_code_t error = operation.pfnUndo(
            { operation.pPayload, operation.cbPayload },
            operation.pUserData );
        if ( Cy_ErrorFailed( error ) ) {
            // The cursor records callbacks already completed before the failure.
            pHistory->bApplying = CY_FALSE;
            return error;
        }
        --pHistory->iCursor;
    }
    pHistory->bApplying = CY_FALSE;
    return CY_ERROR_OK;
}

error_code_t UndoRedo_Redo( undo_history_t *pHistory ) noexcept
{
    if ( !HistoryIsValid( pHistory ) ||
         pHistory->bTransactionOpen ||
         pHistory->bApplying ) {
        return CommonError( common_error_t::ERR_INVALID_STATE );
    }
    if ( pHistory->iCursor >= pHistory->nCount ) {
        return CommonError( common_error_t::ERR_NOT_FOUND );
    }

    const u64 nGroup = pHistory->pOperations[pHistory->iCursor].nGroup;
    pHistory->bApplying = CY_TRUE;
    // Redo runs oldest to newest, matching the original transaction order.
    while ( pHistory->iCursor < pHistory->nCount ) {
        owned_undo_operation_t &operation =
            pHistory->pOperations[pHistory->iCursor];
        if ( operation.nGroup != nGroup ) {
            break;
        }
        const error_code_t error = operation.pfnRedo(
            { operation.pPayload, operation.cbPayload },
            operation.pUserData );
        if ( Cy_ErrorFailed( error ) ) {
            pHistory->bApplying = CY_FALSE;
            return error;
        }
        ++pHistory->iCursor;
    }
    pHistory->bApplying = CY_FALSE;
    return CY_ERROR_OK;
}

string_view_t UndoRedo_UndoLabel( const undo_history_t *pHistory ) noexcept
{
    return UndoRedo_CanUndo( pHistory )
        ? GroupLabel( pHistory, pHistory->iCursor - 1u )
        : string_view_t{};
}

string_view_t UndoRedo_RedoLabel( const undo_history_t *pHistory ) noexcept
{
    return UndoRedo_CanRedo( pHistory )
        ? GroupLabel( pHistory, pHistory->iCursor )
        : string_view_t{};
}

usize UndoRedo_OperationCount( const undo_history_t *pHistory ) noexcept
{
    return HistoryIsValid( pHistory ) ? pHistory->nCount : 0u;
}

bool_t UndoRedo_IsTransactionOpen( const undo_history_t *pHistory ) noexcept
{
    return HistoryIsValid( pHistory ) && pHistory->bTransactionOpen;
}

} // namespace cypher::common
