<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/Debug/README.md
//  Purpose: Defines world diagnostics, counters, and visualization data.
//
//////////////////////////////////////////////////////////////////////////
-->

# Debug

This folder will expose bounded diagnostics for object counts, query counts,
culling reasons, portal traversal, terrain LODs, resident cells, pending
streaming requests, and submission sizes.

World debug code produces plain lines, bounds, labels, and counters. The
renderer decides how to draw them, while the console and profiling tools decide
how to display or record them. Diagnostics must not change world results.
