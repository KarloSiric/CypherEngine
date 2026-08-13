//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherTools/CypherResourceCompiler/main.cpp
//  Purpose: Defines the process entry point for CypherResourceCompiler.
//  Details: Process ownership remains intentionally thin; all command parsing,
//           terminal behavior, dispatch, and compilation live in testable modules.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResourceCompiler.h"

int main( int argc, char **pArgv )
{
    return static_cast<int>(
        cypher::tools::CypherResourceCompiler_Run( argc, pArgv ) );
}
