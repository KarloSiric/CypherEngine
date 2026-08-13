//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolFramework.h
//  Purpose: Provides the public umbrella include for shared tool contracts.
//  Details: This header exposes UI-neutral application, invocation, diagnostics,
//           progress, metadata, and authoring contracts used across Cypher tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLFRAMEWORK_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLFRAMEWORK_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"
#include "CypherCommon_ToolArtifact.h"
#include "CypherCommon_ToolArtifactWriter.h"
#include "CypherCommon_ToolCancellation.h"
#include "CypherCommon_ToolCliArgumentParser.h"
#include "CypherCommon_ToolCliDisplay.h"
#include "CypherCommon_ToolCliHelp.h"
#include "CypherCommon_ToolCliResponseFile.h"
#include "CypherCommon_ToolCliRunner.h"
#include "CypherCommon_ToolCliSignal.h"
#include "CypherCommon_ToolCliTerminal.h"
#include "CypherCommon_ToolCommand.h"
#include "CypherCommon_ToolCompiler.h"
#include "CypherCommon_ToolCompilerRegistry.h"
#include "CypherCommon_ToolContext.h"
#include "CypherCommon_ToolDependency.h"
#include "CypherCommon_ToolDiagnostic.h"
#include "CypherCommon_ToolDocument.h"
#include "CypherCommon_ToolEvent.h"
#include "CypherCommon_ToolHost.h"
#include "CypherCommon_ToolInvocation.h"
#include "CypherCommon_ToolInputSet.h"
#include "CypherCommon_ToolOption.h"
#include "CypherCommon_ToolOptionSet.h"
#include "CypherCommon_ToolOutput.h"
#include "CypherCommon_ToolProgress.h"
#include "CypherCommon_ToolRegistry.h"
#include "CypherCommon_ToolReport.h"
#include "CypherCommon_ToolReportWriter.h"
#include "CypherCommon_ToolSession.h"
#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTarget.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_ToolWorkspace.h"

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLFRAMEWORK_H
