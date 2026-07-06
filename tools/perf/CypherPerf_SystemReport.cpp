//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tools/perf/CypherPerf_SystemReport.cpp
//  Purpose: Provides performance tooling for Perf SystemReport.
//  Details: This tool supports repeatable performance inspection and benchmark
//           reporting. It should stay scriptable so CI and local development can use
//           the same path.
//
//  History:
//  - Created by Karlo Siric on 2026-07-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Tier0.h"

#include <cstdio>

using namespace cypher::common;

int main()
{
    char szReport[CY_SYSTEMINFO_REPORT_MAX] = {};
    Cy_SystemInfoFormatReport( szReport, sizeof( szReport ) );

    std::printf( "%s", szReport );

    return 0;
}
