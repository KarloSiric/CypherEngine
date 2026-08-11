<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/DataModel/README.md
//  Purpose: Documents the CypherCommon DataModel folder.
//  Details: DataModel owns shared schema, serialization, reflection, and typed
//           document contracts used across runtime systems and tools.
//
//  History:
//  - Created by Karlo Siric on 2026-07-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# DataModel

`DataModel` is for shared structured-data contracts above Tier1.

Schema declarations, reflected-property metadata, archive descriptors, and
versioned document types can live here. CYKV parsing remains in Tier1, while
asset compilers and editor property widgets remain outside Common.
