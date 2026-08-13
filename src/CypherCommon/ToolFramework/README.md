<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/README.md
//  Purpose: Documents the CypherCommon ToolFramework folder.
//  Details: ToolFramework contains editor-neutral application, command-line,
//           progress, compiler, and editor/runtime bridge contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# ToolFramework

`ToolFramework` contains contracts shared by Mason, command-line frontends,
focused Qt applications, automation, and tests. It is not a widget toolkit and
it does not own format-specific compiler algorithms.

Tool executables, importers, compilers, cookers, and GUI tool windows belong in
tool modules outside Common. Qt widgets, dock panels, and Mason implementation
must not enter this shared contract layer.

## Responsibility Groups

| Group | Files | Responsibility |
| --- | --- | --- |
| Identity and discovery | `ToolApplication`, `ToolRegistry`, `ToolTypes` | Stable product metadata and IDs. |
| Invocation contract | `ToolCommand`, `ToolOption`, `ToolOptionSet`, `ToolContext`, `ToolInvocation`, `ToolTarget` | Describe one validated operation independently of its frontend. |
| Host communication | `ToolHost`, `ToolDiagnostic`, `ToolProgress`, `ToolEvent`, `ToolDependency`, `ToolArtifact`, `ToolReport` | Deliver structured synchronous records to CLI, Mason, CI, or tests. |
| Compiler dispatch | `ToolCompiler`, `ToolCompilerRegistry`, `ToolInputSet` | Register and invoke reusable type-specific compiler modules. |
| Artifact publication | `ToolArtifactWriter` | Create output parents and transactionally publish completed native files. |
| Output serialization | `ToolOutput`, `ToolReportWriter` | Stable text/JSON policy and report output. |
| CLI frontend | `ToolCliArgumentParser`, `ToolCliResponseFile`, `ToolCliHelp`, `ToolCliTerminal`, `ToolCliDisplay`, `ToolCliSignal`, `ToolCliRunner` | Process arguments, terminal presentation, cancellation, and exit status. |
| Authoring state | `ToolDocument`, `ToolWorkspace`, `ToolSession`, `ToolCancellation` | Shared non-Qt document, workspace, execution, and cancellation state. |

## Data Flow

```text
main() -> ToolCliRunner -> command callback -> ToolCompiler
                                      |             |
                                      +-> ToolHost <-+

Mason action --------------------------> ToolCompiler
                                      |             |
                                      +-> ToolHost <-+
```

The CLI runner is deliberately bypassed by Mason. Both frontends invoke the
same compiler contract and receive the same diagnostics, dependencies,
artifacts, progress events, and final report.
