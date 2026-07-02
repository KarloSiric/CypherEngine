//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Tier0.h
//  Purpose: Declares CypherCommon Tier0 Tier0 support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_H
#define CYPHER_COMMON_TIER0_H
#pragma once

/*
================
CypherCommon Tier0

Lowest-level foundation shared by engine runtime, game code, tools and editor.
This header is allowed to know the target platform and primitive scalar types,
but it must not depend on any CypherEngine subsystem.
================
*/

#include "CypherCommon_Platform.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_BuildConfig.h"
#include "CypherCommon_BuildId.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_Compiler.h"
#include "CypherCommon_Warnings.h"
#include "CypherCommon_API.h"
#include "CypherCommon_Annotations.h"
#include "CypherCommon_StaticAnalysis.h"
#include "CypherCommon_SourceLocation.h"
#include "CypherCommon_Error.h"
#include "CypherCommon_Handle.h"
#include "CypherCommon_Debug.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_Crash.h"
#include "CypherCommon_Minidump.h"
#include "CypherCommon_Validator.h"
#include "CypherCommon_MemoryOps.h"
#include "CypherCommon_MemoryDebug.h"
#include "CypherCommon_MemoryTracker.h"
#include "CypherCommon_PlatformMemory.h"
#include "CypherCommon_PageAllocator.h"
#include "CypherCommon_CacheHints.h"
#include "CypherCommon_Align.h"
#include "CypherCommon_Bits.h"
#include "CypherCommon_Endian.h"
#include "CypherCommon_TypeTraits.h"
#include "CypherCommon_WideChar.h"
#include "CypherCommon_Atomic.h"
#include "CypherCommon_Thread.h"
#include "CypherCommon_Mutex.h"
#include "CypherCommon_TLS.h"
#include "CypherCommon_TsList.h"
#include "CypherCommon_TestThread.h"
#include "CypherCommon_Timer.h"
#include "CypherCommon_PerformanceCounter.h"
#include "CypherCommon_Profile.h"
#include "CypherCommon_Stats.h"
#include "CypherCommon_CPUDetect.h"
#include "CypherCommon_CPUMonitoring.h"
#include "CypherCommon_Simd.h"
#include "CypherCommon_StackTrace.h"
#include "CypherCommon_SystemInfo.h"
#include "CypherCommon_CommandLineBase.h"
#include "CypherCommon_DynamicLibrary.h"
#include "CypherCommon_Process.h"
#include "CypherCommon_Environment.h"
#include "CypherCommon_ProgressBar.h"
#include "CypherCommon_LogToggle.h"
#include "CypherCommon_Log.h"
#include "CypherCommon_Module.h"

#endif // CYPHER_COMMON_TIER0_H
