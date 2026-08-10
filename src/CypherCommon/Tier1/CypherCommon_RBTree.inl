//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RBTree.inl
//  Purpose: Implements allocator-backed red-black ordered trees.
//  Details: Rotations and recoloring preserve logarithmic tree height. Nodes retain
//           stable addresses until erased, and duplicate insertion keeps old values.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RBTREE_INL
#define CYPHER_COMMON_TIER1_RBTREE_INL

#ifndef CYPHER_COMMON_TIER1_RBTREE_H
    #include "CypherCommon_RBTree.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename key_t, typename value_t, typename compare_t>
bool_t RBTree_IsCanonicalEmpty(
    const rb_tree_t<key_t, value_t, compare_t> &tree ) noexcept
{
    return tree.pRoot == nullptr &&
           tree.nCount == 0u &&
           tree.pAllocator == nullptr;
}

template <typename key_t, typename value_t>
rb_tree_color_t RBTree_Color(
    const rb_tree_node_t<key_t, value_t> *pNode ) noexcept
{
    return pNode != nullptr ? pNode->color : rb_tree_color_t::BLACK;
}

template <typename key_t, typename value_t>
void RBTree_SetColor(
    rb_tree_node_t<key_t, value_t> *pNode,
    rb_tree_color_t color ) noexcept
{
    if ( pNode != nullptr ) {
        pNode->color = color;
    }
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_RotateLeft(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pPivot ) noexcept
{
    rb_tree_node_t<key_t, value_t> *pRight = pPivot->pRight;
    CY_ASSERT_MSG( pRight != nullptr, "RBTree left rotation requires a right child." );
    if ( pRight == nullptr ) {
        return;
    }

    pPivot->pRight = pRight->pLeft;
    if ( pRight->pLeft != nullptr ) {
        pRight->pLeft->pParent = pPivot;
    }
    pRight->pParent = pPivot->pParent;
    if ( pPivot->pParent == nullptr ) {
        pTree->pRoot = pRight;
    } else if ( pPivot == pPivot->pParent->pLeft ) {
        pPivot->pParent->pLeft = pRight;
    } else {
        pPivot->pParent->pRight = pRight;
    }
    pRight->pLeft = pPivot;
    pPivot->pParent = pRight;
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_RotateRight(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pPivot ) noexcept
{
    rb_tree_node_t<key_t, value_t> *pLeft = pPivot->pLeft;
    CY_ASSERT_MSG( pLeft != nullptr, "RBTree right rotation requires a left child." );
    if ( pLeft == nullptr ) {
        return;
    }

    pPivot->pLeft = pLeft->pRight;
    if ( pLeft->pRight != nullptr ) {
        pLeft->pRight->pParent = pPivot;
    }
    pLeft->pParent = pPivot->pParent;
    if ( pPivot->pParent == nullptr ) {
        pTree->pRoot = pLeft;
    } else if ( pPivot == pPivot->pParent->pRight ) {
        pPivot->pParent->pRight = pLeft;
    } else {
        pPivot->pParent->pLeft = pLeft;
    }
    pLeft->pRight = pPivot;
    pPivot->pParent = pLeft;
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_FixInsertion(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pNode ) noexcept
{
    while ( pNode->pParent != nullptr &&
            pNode->pParent->color == rb_tree_color_t::RED ) {
        rb_tree_node_t<key_t, value_t> *pParent = pNode->pParent;
        rb_tree_node_t<key_t, value_t> *pGrandparent = pParent->pParent;
        CY_ASSERT_MSG(
            pGrandparent != nullptr,
            "A red parent must have a grandparent during insertion fix-up." );
        if ( pGrandparent == nullptr ) {
            break;
        }

        if ( pParent == pGrandparent->pLeft ) {
            rb_tree_node_t<key_t, value_t> *pUncle = pGrandparent->pRight;
            if ( RBTree_Color( pUncle ) == rb_tree_color_t::RED ) {
                pParent->color = rb_tree_color_t::BLACK;
                pUncle->color = rb_tree_color_t::BLACK;
                pGrandparent->color = rb_tree_color_t::RED;
                pNode = pGrandparent;
                continue;
            }
            if ( pNode == pParent->pRight ) {
                pNode = pParent;
                RBTree_RotateLeft( pTree, pNode );
                pParent = pNode->pParent;
                pGrandparent = pParent->pParent;
            }
            pParent->color = rb_tree_color_t::BLACK;
            pGrandparent->color = rb_tree_color_t::RED;
            RBTree_RotateRight( pTree, pGrandparent );
        } else {
            rb_tree_node_t<key_t, value_t> *pUncle = pGrandparent->pLeft;
            if ( RBTree_Color( pUncle ) == rb_tree_color_t::RED ) {
                pParent->color = rb_tree_color_t::BLACK;
                pUncle->color = rb_tree_color_t::BLACK;
                pGrandparent->color = rb_tree_color_t::RED;
                pNode = pGrandparent;
                continue;
            }
            if ( pNode == pParent->pLeft ) {
                pNode = pParent;
                RBTree_RotateRight( pTree, pNode );
                pParent = pNode->pParent;
                pGrandparent = pParent->pParent;
            }
            pParent->color = rb_tree_color_t::BLACK;
            pGrandparent->color = rb_tree_color_t::RED;
            RBTree_RotateLeft( pTree, pGrandparent );
        }
    }
    if ( pTree->pRoot != nullptr ) {
        pTree->pRoot->color = rb_tree_color_t::BLACK;
    }
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_Transplant(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pOld,
    rb_tree_node_t<key_t, value_t> *pReplacement ) noexcept
{
    if ( pOld->pParent == nullptr ) {
        pTree->pRoot = pReplacement;
    } else if ( pOld == pOld->pParent->pLeft ) {
        pOld->pParent->pLeft = pReplacement;
    } else {
        pOld->pParent->pRight = pReplacement;
    }
    if ( pReplacement != nullptr ) {
        pReplacement->pParent = pOld->pParent;
    }
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_FixErasure(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pNode,
    rb_tree_node_t<key_t, value_t> *pParent ) noexcept
{
    while ( pNode != pTree->pRoot &&
            RBTree_Color( pNode ) == rb_tree_color_t::BLACK ) {
        if ( pParent == nullptr ) {
            break;
        }

        if ( pNode == pParent->pLeft ) {
            rb_tree_node_t<key_t, value_t> *pSibling = pParent->pRight;
            if ( RBTree_Color( pSibling ) == rb_tree_color_t::RED ) {
                pSibling->color = rb_tree_color_t::BLACK;
                pParent->color = rb_tree_color_t::RED;
                RBTree_RotateLeft( pTree, pParent );
                pSibling = pParent->pRight;
            }

            if ( RBTree_Color( pSibling != nullptr ? pSibling->pLeft : nullptr ) ==
                     rb_tree_color_t::BLACK &&
                 RBTree_Color( pSibling != nullptr ? pSibling->pRight : nullptr ) ==
                     rb_tree_color_t::BLACK ) {
                RBTree_SetColor( pSibling, rb_tree_color_t::RED );
                pNode = pParent;
                pParent = pNode->pParent;
                continue;
            }

            if ( RBTree_Color( pSibling != nullptr ? pSibling->pRight : nullptr ) ==
                 rb_tree_color_t::BLACK ) {
                RBTree_SetColor(
                    pSibling != nullptr ? pSibling->pLeft : nullptr,
                    rb_tree_color_t::BLACK );
                RBTree_SetColor( pSibling, rb_tree_color_t::RED );
                if ( pSibling != nullptr ) {
                    RBTree_RotateRight( pTree, pSibling );
                }
                pSibling = pParent->pRight;
            }

            RBTree_SetColor( pSibling, pParent->color );
            pParent->color = rb_tree_color_t::BLACK;
            RBTree_SetColor(
                pSibling != nullptr ? pSibling->pRight : nullptr,
                rb_tree_color_t::BLACK );
            RBTree_RotateLeft( pTree, pParent );
            pNode = pTree->pRoot;
            pParent = nullptr;
        } else {
            rb_tree_node_t<key_t, value_t> *pSibling = pParent->pLeft;
            if ( RBTree_Color( pSibling ) == rb_tree_color_t::RED ) {
                pSibling->color = rb_tree_color_t::BLACK;
                pParent->color = rb_tree_color_t::RED;
                RBTree_RotateRight( pTree, pParent );
                pSibling = pParent->pLeft;
            }

            if ( RBTree_Color( pSibling != nullptr ? pSibling->pRight : nullptr ) ==
                     rb_tree_color_t::BLACK &&
                 RBTree_Color( pSibling != nullptr ? pSibling->pLeft : nullptr ) ==
                     rb_tree_color_t::BLACK ) {
                RBTree_SetColor( pSibling, rb_tree_color_t::RED );
                pNode = pParent;
                pParent = pNode->pParent;
                continue;
            }

            if ( RBTree_Color( pSibling != nullptr ? pSibling->pLeft : nullptr ) ==
                 rb_tree_color_t::BLACK ) {
                RBTree_SetColor(
                    pSibling != nullptr ? pSibling->pRight : nullptr,
                    rb_tree_color_t::BLACK );
                RBTree_SetColor( pSibling, rb_tree_color_t::RED );
                if ( pSibling != nullptr ) {
                    RBTree_RotateLeft( pTree, pSibling );
                }
                pSibling = pParent->pLeft;
            }

            RBTree_SetColor( pSibling, pParent->color );
            pParent->color = rb_tree_color_t::BLACK;
            RBTree_SetColor(
                pSibling != nullptr ? pSibling->pLeft : nullptr,
                rb_tree_color_t::BLACK );
            RBTree_RotateRight( pTree, pParent );
            pNode = pTree->pRoot;
            pParent = nullptr;
        }
    }
    RBTree_SetColor( pNode, rb_tree_color_t::BLACK );
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_DestroySubtree(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    rb_tree_node_t<key_t, value_t> *pNode ) noexcept
{
    if ( pNode == nullptr ) {
        return;
    }
    RBTree_DestroySubtree( pTree, pNode->pLeft );
    RBTree_DestroySubtree( pTree, pNode->pRight );
    pNode->~rb_tree_node_t<key_t, value_t>();
    Allocator_Free(
        pTree->pAllocator,
        pNode,
        sizeof( rb_tree_node_t<key_t, value_t> ),
        alignof( rb_tree_node_t<key_t, value_t> ) );
}

} // namespace detail

template <typename key_t, typename value_t, typename compare_t>
rb_tree_t<key_t, value_t, compare_t>::~rb_tree_t() noexcept
{
    RBTree_Shutdown( this );
}

template <typename key_t, typename value_t, typename compare_t>
bool_t RBTree_Init(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const allocator_t *pAllocator,
    compare_t compare ) noexcept
{
    const bool_t bValidDestination =
        pTree != nullptr && detail::RBTree_IsCanonicalEmpty( *pTree );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "RBTree_Init requires a canonical empty destination." );
    CY_ASSERT_MSG( bValidAllocator, "RBTree_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    pTree->pAllocator = pAllocator;
    pTree->compare = static_cast<compare_t &&>( compare );
    return CY_TRUE;
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_Shutdown(
    rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept
{
    if ( pTree == nullptr || detail::RBTree_IsCanonicalEmpty( *pTree ) ) {
        return;
    }
    const bool_t bValidTree = RBTree_IsValid( pTree );
    CY_ASSERT_MSG( bValidTree, "RBTree_Shutdown requires a valid tree." );
    if ( !bValidTree ) {
        return;
    }

    RBTree_Clear( pTree );
    pTree->pAllocator = nullptr;
}

template <typename key_t, typename value_t, typename compare_t>
void RBTree_Clear(
    rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept
{
    const bool_t bValidTree = RBTree_IsValid( pTree );
    CY_ASSERT_MSG( bValidTree, "RBTree_Clear requires a valid tree." );
    if ( !bValidTree ) {
        return;
    }

    detail::RBTree_DestroySubtree( pTree, pTree->pRoot );
    pTree->pRoot = nullptr;
    pTree->nCount = 0u;
}

template <typename key_t, typename value_t, typename compare_t>
bool_t RBTree_IsValid(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept
{
    if ( pTree == nullptr ) {
        return CY_FALSE;
    }
    if ( pTree->pAllocator == nullptr ) {
        return detail::RBTree_IsCanonicalEmpty( *pTree );
    }
    if ( !Allocator_IsValid( pTree->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( pTree->pRoot == nullptr ) {
        return pTree->nCount == 0u;
    }
    return pTree->nCount > 0u &&
           pTree->pRoot->pParent == nullptr &&
           pTree->pRoot->color == rb_tree_color_t::BLACK;
}

template <typename key_t, typename value_t, typename compare_t>
rb_tree_insert_result_t<key_t, value_t> RBTree_Insert(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key,
    const value_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<key_t> &&
        std::is_nothrow_copy_constructible_v<value_t>,
        "RBTree insertion requires nothrow copy construction." );
    static_assert(
        std::is_nothrow_destructible_v<key_t> &&
        std::is_nothrow_destructible_v<value_t>,
        "RBTree values must support nothrow destruction." );

    const bool_t bValidTree = RBTree_IsValid( pTree ) &&
                              pTree->pAllocator != nullptr;
    CY_ASSERT_MSG( bValidTree, "RBTree_Insert requires an initialized tree." );
    if ( !bValidTree ) {
        return {};
    }

    rb_tree_node_t<key_t, value_t> *pParent = nullptr;
    rb_tree_node_t<key_t, value_t> *pNode = pTree->pRoot;
    while ( pNode != nullptr ) {
        pParent = pNode;
        if ( pTree->compare( key, pNode->key ) ) {
            pNode = pNode->pLeft;
        } else if ( pTree->compare( pNode->key, key ) ) {
            pNode = pNode->pRight;
        } else {
            return { pNode, CY_FALSE };
        }
    }

    void *pStorage = Allocator_Allocate(
        pTree->pAllocator,
        sizeof( rb_tree_node_t<key_t, value_t> ),
        alignof( rb_tree_node_t<key_t, value_t> ) );
    if ( pStorage == nullptr ) {
        return {};
    }
    rb_tree_node_t<key_t, value_t> *pInserted =
        ::new ( pStorage ) rb_tree_node_t<key_t, value_t>( key, value );
    pInserted->pParent = pParent;
    if ( pParent == nullptr ) {
        pTree->pRoot = pInserted;
    } else if ( pTree->compare( key, pParent->key ) ) {
        pParent->pLeft = pInserted;
    } else {
        pParent->pRight = pInserted;
    }

    ++pTree->nCount;
    detail::RBTree_FixInsertion( pTree, pInserted );
    return { pInserted, CY_TRUE };
}

template <typename key_t, typename value_t, typename compare_t>
rb_tree_node_t<key_t, value_t> *RBTree_Find(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept
{
    return const_cast<rb_tree_node_t<key_t, value_t> *>( RBTree_Find(
        static_cast<const rb_tree_t<key_t, value_t, compare_t> *>( pTree ),
        key ) );
}

template <typename key_t, typename value_t, typename compare_t>
const rb_tree_node_t<key_t, value_t> *RBTree_Find(
    const rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept
{
    if ( !RBTree_IsValid( pTree ) ) {
        return nullptr;
    }
    const rb_tree_node_t<key_t, value_t> *pNode = pTree->pRoot;
    while ( pNode != nullptr ) {
        if ( pTree->compare( key, pNode->key ) ) {
            pNode = pNode->pLeft;
        } else if ( pTree->compare( pNode->key, key ) ) {
            pNode = pNode->pRight;
        } else {
            return pNode;
        }
    }
    return nullptr;
}

template <typename key_t, typename value_t, typename compare_t>
rb_tree_node_t<key_t, value_t> *RBTree_LowerBound(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept
{
    return const_cast<rb_tree_node_t<key_t, value_t> *>( RBTree_LowerBound(
        static_cast<const rb_tree_t<key_t, value_t, compare_t> *>( pTree ),
        key ) );
}

template <typename key_t, typename value_t, typename compare_t>
const rb_tree_node_t<key_t, value_t> *RBTree_LowerBound(
    const rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept
{
    if ( !RBTree_IsValid( pTree ) ) {
        return nullptr;
    }

    const rb_tree_node_t<key_t, value_t> *pCandidate = nullptr;
    const rb_tree_node_t<key_t, value_t> *pNode = pTree->pRoot;
    while ( pNode != nullptr ) {
        if ( !pTree->compare( pNode->key, key ) ) {
            pCandidate = pNode;
            pNode = pNode->pLeft;
        } else {
            pNode = pNode->pRight;
        }
    }
    return pCandidate;
}

template <typename key_t, typename value_t, typename compare_t>
bool_t RBTree_Erase(
    rb_tree_t<key_t, value_t, compare_t> *pTree,
    const key_t &key ) noexcept
{
    rb_tree_node_t<key_t, value_t> *pRemoved = RBTree_Find( pTree, key );
    if ( pRemoved == nullptr ) {
        return CY_FALSE;
    }

    rb_tree_node_t<key_t, value_t> *pMoved = pRemoved;
    rb_tree_color_t removedColor = pMoved->color;
    rb_tree_node_t<key_t, value_t> *pFixNode = nullptr;
    rb_tree_node_t<key_t, value_t> *pFixParent = nullptr;

    if ( pRemoved->pLeft == nullptr ) {
        pFixNode = pRemoved->pRight;
        pFixParent = pRemoved->pParent;
        detail::RBTree_Transplant( pTree, pRemoved, pRemoved->pRight );
    } else if ( pRemoved->pRight == nullptr ) {
        pFixNode = pRemoved->pLeft;
        pFixParent = pRemoved->pParent;
        detail::RBTree_Transplant( pTree, pRemoved, pRemoved->pLeft );
    } else {
        pMoved = RBTree_First( pRemoved->pRight );
        removedColor = pMoved->color;
        pFixNode = pMoved->pRight;
        if ( pMoved->pParent == pRemoved ) {
            pFixParent = pMoved;
            if ( pFixNode != nullptr ) {
                pFixNode->pParent = pMoved;
            }
        } else {
            pFixParent = pMoved->pParent;
            detail::RBTree_Transplant( pTree, pMoved, pMoved->pRight );
            pMoved->pRight = pRemoved->pRight;
            pMoved->pRight->pParent = pMoved;
        }
        detail::RBTree_Transplant( pTree, pRemoved, pMoved );
        pMoved->pLeft = pRemoved->pLeft;
        pMoved->pLeft->pParent = pMoved;
        pMoved->color = pRemoved->color;
    }

    pRemoved->~rb_tree_node_t<key_t, value_t>();
    Allocator_Free(
        pTree->pAllocator,
        pRemoved,
        sizeof( rb_tree_node_t<key_t, value_t> ),
        alignof( rb_tree_node_t<key_t, value_t> ) );
    --pTree->nCount;

    if ( removedColor == rb_tree_color_t::BLACK ) {
        if ( pFixNode != nullptr ) {
            pFixParent = pFixNode->pParent;
        }
        detail::RBTree_FixErasure( pTree, pFixNode, pFixParent );
    }
    return CY_TRUE;
}

template <typename key_t, typename value_t>
rb_tree_node_t<key_t, value_t> *RBTree_First(
    rb_tree_node_t<key_t, value_t> *pRoot ) noexcept
{
    return const_cast<rb_tree_node_t<key_t, value_t> *>( RBTree_First(
        static_cast<const rb_tree_node_t<key_t, value_t> *>( pRoot ) ) );
}

template <typename key_t, typename value_t>
const rb_tree_node_t<key_t, value_t> *RBTree_First(
    const rb_tree_node_t<key_t, value_t> *pRoot ) noexcept
{
    const rb_tree_node_t<key_t, value_t> *pNode = pRoot;
    while ( pNode != nullptr && pNode->pLeft != nullptr ) {
        pNode = pNode->pLeft;
    }
    return pNode;
}

template <typename key_t, typename value_t>
rb_tree_node_t<key_t, value_t> *RBTree_Next(
    rb_tree_node_t<key_t, value_t> *pNode ) noexcept
{
    return const_cast<rb_tree_node_t<key_t, value_t> *>( RBTree_Next(
        static_cast<const rb_tree_node_t<key_t, value_t> *>( pNode ) ) );
}

template <typename key_t, typename value_t>
const rb_tree_node_t<key_t, value_t> *RBTree_Next(
    const rb_tree_node_t<key_t, value_t> *pNode ) noexcept
{
    if ( pNode == nullptr ) {
        return nullptr;
    }
    if ( pNode->pRight != nullptr ) {
        return RBTree_First( pNode->pRight );
    }

    const rb_tree_node_t<key_t, value_t> *pCurrent = pNode;
    const rb_tree_node_t<key_t, value_t> *pParent = pCurrent->pParent;
    while ( pParent != nullptr && pCurrent == pParent->pRight ) {
        pCurrent = pParent;
        pParent = pParent->pParent;
    }
    return pParent;
}

template <typename key_t, typename value_t, typename compare_t>
usize RBTree_Count(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept
{
    return RBTree_IsValid( pTree ) ? pTree->nCount : 0u;
}

template <typename key_t, typename value_t, typename compare_t>
bool_t RBTree_IsEmpty(
    const rb_tree_t<key_t, value_t, compare_t> *pTree ) noexcept
{
    return RBTree_Count( pTree ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RBTREE_INL
