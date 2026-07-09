<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Job/README.md
//  Purpose: Documents the CypherCommon Job folder.
//  Details: Job contains shared job IDs, priorities, dependency handles, and
//           function signatures used by future async/job systems.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Job

`Job` is for job-system public contracts.

The actual scheduler implementation should live in the owning job/runtime
subsystem. Common holds the IDs, descriptors, priorities, and callback shapes.
