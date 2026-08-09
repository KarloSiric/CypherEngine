#!/usr/bin/env bash

# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: build.sh
# //  Purpose: Runs the build developer workflow.
# //  Details: This file is part of the CypherEngine owned source tree. Keep its
# //           responsibility narrow and update this header when the file purpose
# //           changes.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-04-21
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

set -euo pipefail

cmake --preset debug
cmake --build --preset debug --parallel
