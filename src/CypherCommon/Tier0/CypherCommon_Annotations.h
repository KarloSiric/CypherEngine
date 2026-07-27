//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Annotations.h
//  Purpose: Declares CypherCommon Tier0 Annotations support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ANNOTATIONS_H
#define CYPHER_COMMON_TIER0_ANNOTATIONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Annotations

Portable API annotation surface for static analysis and compiler diagnostics.
Annotations never change runtime behavior.
================
*/

#include "CypherCommon_Platform.h"

/*
================
Pointer And Buffer Contracts
================
*/
#if CYPHER_COMPILER_MSVC_ABI
    #include <sal.h>

    #define CY_IN                           _In_
    #define CY_OUT                          _Out_
    #define CY_INOUT                        _Inout_
    #define CY_IN_OPTIONAL                  _In_opt_
    #define CY_OUT_OPTIONAL                 _Out_opt_
    #define CY_INOUT_OPTIONAL               _Inout_opt_
    #define CY_IN_READS( count )            _In_reads_( count )
    #define CY_OUT_WRITES( count )          _Out_writes_( count )
    #define CY_INOUT_UPDATES( count )       _Inout_updates_( count )
    #define CY_IN_READS_BYTES( count )      _In_reads_bytes_( count )
    #define CY_OUT_WRITES_BYTES( count )    _Out_writes_bytes_( count )
    #define CY_INOUT_UPDATES_BYTES( count ) _Inout_updates_bytes_( count )
    #define CY_IN_Z                         _In_z_
    #define CY_OUT_Z                        _Out_z_
    #define CY_PRINTF_FORMAT_STRING         _Printf_format_string_
    #define CY_SCANF_FORMAT_STRING          _Scanf_format_string_
#else
    #define CY_IN
    #define CY_OUT
    #define CY_INOUT
    #define CY_IN_OPTIONAL
    #define CY_OUT_OPTIONAL
    #define CY_INOUT_OPTIONAL
    #define CY_IN_READS( count )
    #define CY_OUT_WRITES( count )
    #define CY_INOUT_UPDATES( count )
    #define CY_IN_READS_BYTES( count )
    #define CY_OUT_WRITES_BYTES( count )
    #define CY_INOUT_UPDATES_BYTES( count )
    #define CY_IN_Z
    #define CY_OUT_Z
    #define CY_PRINTF_FORMAT_STRING
    #define CY_SCANF_FORMAT_STRING
#endif

// Compatibility vocabulary retained for existing declarations.
#define CY_OPTIONAL
#define CY_CAP( count )
#define CY_Z

/*
================
Function Contracts
================
*/
#if CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    #define CY_PRINTF_LIKE( formatIndex, firstArgumentIndex ) \
        __attribute__(( format( printf, formatIndex, firstArgumentIndex ) ))
    #define CY_SCANF_LIKE( formatIndex, firstArgumentIndex ) \
        __attribute__(( format( scanf, formatIndex, firstArgumentIndex ) ))
    #define CY_NONNULL_ARGS( ... ) __attribute__(( nonnull( __VA_ARGS__ ) ))
    #define CY_RETURNS_NONNULL __attribute__(( returns_nonnull ))
#else
    #define CY_PRINTF_LIKE( formatIndex, firstArgumentIndex )
    #define CY_SCANF_LIKE( formatIndex, firstArgumentIndex )
    #define CY_NONNULL_ARGS( ... )
    #define CY_RETURNS_NONNULL
#endif

#endif // CYPHER_COMMON_TIER0_ANNOTATIONS_H
