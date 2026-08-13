<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/README.md
//  Purpose: Documents the checked-in ResourceCompiler shader stress corpus.
//  Details: One response file addresses exactly 100 valid CYKV shader recipes
//           spanning five linked GLSL pipelines and schema-boundary variants.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Shader Compiler Corpus

This fixture contains exactly 100 valid `.cyshader` recipes and five shared
vertex/fragment stage pairs:

- world surface rendering
- skinned character rendering
- full-screen post-processing
- two-dimensional UI rendering
- debug visualization

Every recipe has a distinct define set. Recipe `debug_100.cyshader` contains
exactly 64 unique defines, exercising the version-1 schema limit. The corpus
tests CYKV parsing, exact schema validation, semantic decoding, canonical path
resolution, GLSL preprocessing and cross-stage linking, dependency reporting,
batch dispatch, deterministic cooked identities, and output path derivation.

From the repository root, validate without writing output:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler validate \
  --source-root tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/source \
  --target host --profile development --progress plain \
  @tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/shader_inputs.rsp
```

Cook all 100 resources:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/source \
  --output-root out/generated/shader-corpus \
  --target host --profile development --progress plain \
  @tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/shader_inputs.rsp
```

The cook must produce 100 `.cyshader_c` files under
`out/generated/shader-corpus/shaders/corpus`.
