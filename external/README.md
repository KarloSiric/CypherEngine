<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: external/README.md
//  Purpose: Documents locally installed SDK dependencies that are not redistributable.
//  Details: This directory is an optional local discovery point for SDKs whose size,
//           installer model, or license makes repository vendoring inappropriate.
//           Everything except this policy file is ignored by Git.
//
//  History:
//  - Created by Karlo Siric on 2026-08-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# External SDKs

Do not commit SDK files to this directory.

| SDK | Acquisition and discovery policy |
| --- | --- |
| Qt 6 | Install through the official Qt installer or an approved system package. Mason discovers it through `CMAKE_PREFIX_PATH` and `find_package(Qt6)`. |
| FMOD | Download manually under the applicable FMOD agreement. Enable only an optional adapter after its license is approved for the intended product. |
| Vulkan SDK | Install through LunarG or the platform vendor when Vulkan backend development begins. Discover it through `find_package(Vulkan)`. |

Local SDK paths belong in `CMakeUserPresets.json` or environment variables, never
in tracked project files.
