//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Tier1.h
//  Purpose: Declares CypherCommon Tier1 Tier1 support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_H
#define CYPHER_COMMON_TIER1_H
#pragma once

/*
================
CypherCommon Tier1

Reusable utility layer built on Tier0.
================
*/

#include "CypherCommon_Tier0.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_String.h"
#include "CypherCommon_StringSplit.h"
#include "CypherCommon_StringMatch.h"
#include "CypherCommon_StringEscape.h"
#include "CypherCommon_StringParse.h"
#include "CypherCommon_StringConvert.h"
#include "CypherCommon_StringFormat.h"
#include "CypherCommon_StringHtml.h"
#include "CypherCommon_StringPath.h"
#include "CypherCommon_StringUrl.h"
#include "CypherCommon_Unicode.h"
#include "CypherCommon_StringView.h"
#include "CypherCommon_FixedString.h"
#include "CypherCommon_StringBuilder.h"
#include "CypherCommon_StringPool.h"
#include "CypherCommon_StringToken.h"
#include "CypherCommon_Symbol.h"
#include "CypherCommon_PathMatch.h"
#include "CypherCommon_CharacterSet.h"
#include "CypherCommon_TokenReader.h"
#include "CypherCommon_Lexer.h"
#include "CypherCommon_Span.h"
#include "CypherCommon_ArrayView.h"
#include "CypherCommon_Pair.h"
#include "CypherCommon_Range.h"
#include "CypherCommon_Array.h"
#include "CypherCommon_FixedArray.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_SmallVector.h"
#include "CypherCommon_Stack.h"
#include "CypherCommon_Queue.h"
#include "CypherCommon_RingBuffer.h"
#include "CypherCommon_LinkedList.h"
#include "CypherCommon_IntrusiveList.h"
#include "CypherCommon_Map.h"
#include "CypherCommon_Set.h"
#include "CypherCommon_Dictionary.h"
#include "CypherCommon_HashTable.h"
#include "CypherCommon_HashMap.h"
#include "CypherCommon_HashSet.h"
#include "CypherCommon_RBTree.h"
#include "CypherCommon_PriorityQueue.h"
#include "CypherCommon_HandleTable.h"
#include "CypherCommon_SparseSet.h"
#include "CypherCommon_SoaContainer.h"
#include "CypherCommon_Buffer.h"
#include "CypherCommon_Blob.h"
#include "CypherCommon_TextBuffer.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"
#include "CypherCommon_BitReader.h"
#include "CypherCommon_BitWriter.h"
#include "CypherCommon_BitBuffer.h"
#include "CypherCommon_Stream.h"
#include "CypherCommon_MemoryStream.h"
#include "CypherCommon_FileIo.h"
#include "CypherCommon_Hash.h"
#include "CypherCommon_HashFNV.h"
#include "CypherCommon_HashXXH.h"
#include "CypherCommon_ChecksumCRC32.h"
#include "CypherCommon_ChecksumCRC64.h"
#include "CypherCommon_ContentHash.h"
#include "CypherCommon_ResourceId.h"
#include "CypherCommon_PasswordHash.h"
#include "CypherCommon_UniqueId.h"
#include "CypherCommon_MemoryPool.h"
#include "CypherCommon_MemoryStack.h"
#include "CypherCommon_FixedMemory.h"
#include "CypherCommon_BlockMemory.h"
#include "CypherCommon_ObjectPool.h"
#include "CypherCommon_ScratchBuffer.h"
#include "CypherCommon_KeyValue.h"
#include "CypherCommon_KeyValueParser.h"
#include "CypherCommon_KeyValueWriter.h"
#include "CypherCommon_KeyValueJson.h"
#include "CypherCommon_KeyValuePack.h"
#include "CypherCommon_DataManager.h"
#include "CypherCommon_InstanceLog.h"
#include "CypherCommon_Interface.h"
#include "CypherCommon_RefCount.h"
#include "CypherCommon_SmartPtr.h"
#include "CypherCommon_Delegate.h"
#include "CypherCommon_Function.h"
#include "CypherCommon_Functor.h"
#include "CypherCommon_CallQueue.h"
#include "CypherCommon_ConVar.h"
#include "CypherCommon_ConCommand.h"
#include "CypherCommon_CommandSystem.h"
#include "CypherCommon_Config.h"
#include "CypherCommon_CommandLine.h"
#include "CypherCommon_CommandBuffer.h"
#include "CypherCommon_ExpressionEvaluator.h"
#include "CypherCommon_RangeCheckedVar.h"
#include "CypherCommon_NetAddress.h"
#include "CypherCommon_NameServiceAddress.h"
#include "CypherCommon_SequenceNumber.h"
#include "CypherCommon_PacketBuffer.h"
#include "CypherCommon_Compression.h"
#include "CypherCommon_CompressionLZ.h"
#include "CypherCommon_CompressionLZ4.h"
#include "CypherCommon_CompressionZstd.h"
#include "CypherCommon_Sort.h"
#include "CypherCommon_HeapSort.h"
#include "CypherCommon_Search.h"
#include "CypherCommon_Diff.h"
#include "CypherCommon_ReliableTimer.h"
#include "CypherCommon_ProcessorDetect.h"
#include "CypherCommon_Event.h"
#include "CypherCommon_UndoRedo.h"
#include "CypherCommon_Localization.h"
#include "CypherCommon_Optional.h"
#include "CypherCommon_Result.h"
#include "CypherCommon_Variant.h"
#include "CypherCommon_Color.h"

#endif // CYPHER_COMMON_TIER1_H
