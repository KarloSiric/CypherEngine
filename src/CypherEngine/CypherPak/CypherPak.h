//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak.h
//  Purpose: Declares the CypherPak Pak module.
//  Details: This file participates in the CypherPak archive format and package access
//           path. Keep binary layout, endian rules, and validation stable so shipped
//           content remains readable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_PAK_H
#define CYPHER_ENGINE_PAK_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Compression.h"
#include "CypherPak_Error.h"
#include "CypherPak_Format.h"
#include "CypherPak_Reader.h"
#include "CypherPak_Types.h"
#include "CypherPak_Writer.h"

namespace cypher::engine::pak
{

/*
================
CypherPak

Runtime package archive backend used by the filesystem for .cypak mounts.
Tools will use the writer API later to build deterministic content packages.
================
*/

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_H
