<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/CYPHER_RESOURCE_COMPILER.md
//  Purpose: Defines the CypherResourceCompiler command-line contract.
//  Details: This document separates executable version 1 behavior from the
//           dependency-aware build coordinator planned for later versions.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherResourceCompiler 1.0.0

`CypherResourceCompiler` is the command-line coordinator for Cypher's offline
asset pipeline. It owns process concerns such as argument parsing, terminal
presentation, output policy, cancellation, compiler discovery, and exit codes.
Format-specific work remains in reusable compiler libraries so Mason, tests, and
future build workers can call the same implementation without launching a child
process.

```text
authored resource
    -> CypherResourceCompiler
        -> compiler registry
            -> type-specific compiler library
                -> diagnostics / dependencies / progress / artifacts / report
                    -> transactional cooked output
```

## Current Resource Support

| Source | Cooked | Compiler ID | Status |
| --- | --- | --- | --- |
| `.cyshader` | `.cyshader_c` | `cypher.shader` | Implemented |

The shader pipeline parses CYKV, validates the exact `cypher.shader` schema,
decodes semantic values, preprocesses GLSL, validates each stage, links graphics
stages, and writes deterministic `CYSH` data inside the `CYRS` cooked envelope.

Version 1 mounts one loose native directory selected by `--source-root` through
the shared read-only Common VFS contract. Inputs and dependencies remain
canonical virtual paths below that root; compiler libraries never reopen native
paths. Read failures report the virtual identity and, when the provider permits
it, a resolved native diagnostic path. Package providers can implement the same
contract later without changing compiler APIs.

## Commands

| Command | Purpose |
| --- | --- |
| `compile` | Validate, cook, and transactionally publish one or more inputs. |
| `validate` | Run the same compiler checks without creating output artifacts. |
| `list-compilers` | List compiler modules registered in this executable. |
| `describe-compiler` | Print one compiler's identity, extensions, and capabilities. |
| `list-formats` | List source-to-cooked mappings backed by live compilers. |
| `completion zsh` | Generate the zsh completion definition. |
| `--help`, `-h` | Show product or command-specific generated help. |
| `--version`, `-V` | Print the stable product version. |

Examples:

```sh
CypherResourceCompiler --help
CypherResourceCompiler compile --help
CypherResourceCompiler list-compilers --color never
CypherResourceCompiler describe-compiler cypher.shader
CypherResourceCompiler list-formats --output-format json
CypherResourceCompiler validate -s assets shaders/world.cyshader
CypherResourceCompiler compile -s assets -o cooked shaders/world.cyshader
CypherResourceCompiler compile -s assets -o cooked -r shaders
CypherResourceCompiler compile -s assets -o cooked 'shaders/ui/*.cyshader'
```

## Compile And Validate Options

| Option | Default | Version 1 meaning |
| --- | --- | --- |
| `-i, --input PATH` | none | Add a repeatable file, directory, or quoted wildcard input. |
| `-r, --recursive` | `false` | Traverse directory and wildcard roots recursively. |
| `-s, --source-root PATH` | `.` | Native root used to resolve authored virtual paths. |
| `-o, --output-root PATH` | `.` | Native root receiving cooked virtual paths. |
| `-t, --target TARGET` | `host` | Explicit target identity passed to compiler context. |
| `-p, --profile PROFILE` | `development` | Select `development`, `release`, or `shipping` policy. |
| `-j, --jobs COUNT` | `1` | Record requested worker capacity. Version 1 executes sequentially. |
| `--output-format FORMAT` | `text` | Select human-readable `text` or newline-delimited `json`. |
| `--progress MODE` | `auto` | Select `auto`, `plain`, `json`, or `none`. |
| `--color WHEN` | `auto` | Select `auto`, `always`, or `never` for text output. |
| `--verbosity LEVEL` | `normal` | Select `quiet`, `normal`, `verbose`, or `trace`. |
| `-W, --warnings-as-errors` | `false` | Promote compiler warnings to failed validation. |
| `-v, --verbose` | `false` | Alias for `--verbosity verbose`. |
| `-k, --keep-going` | `true` | Continue with independent discovered inputs after a failure. |

Supported explicit targets are `windows-x86`, `windows-x64`,
`windows-arm64`, `linux-x86`, `linux-x64`, `linux-arm32`, `linux-arm64`,
`macos-x64`, and `macos-arm64`. `host` resolves the current platform and CPU
architecture.

Inspection commands support `--output-format`, `--color`, and `--verbosity`.
They query the live registry, so planned or unavailable compiler modules are not
reported as if they worked.

## Terminal Presentation

Root help contains the Cypher Engine ASCII identity, product version, copyright,
and proprietary-license notice. Normal compilation omits the banner so repeated
build logs begin directly with useful work. Help also contains commands,
capabilities, the current format pipeline, examples, and the exit-code table.
Execution uses a fixed 32-cell progress bar with completed/total item counts,
linked diagnostics, artifact paths, and one aggregate result summary with total
time, succeeded, failed, skipped, diagnostic, cache, artifact, and I/O counters.

`--color auto` writes ANSI sequences only to a capable terminal. `always` is for
forced-color logs and tests; `never` produces plain text. Redirected progress is
append-only so build logs preserve every state. Interactive `auto` mode redraws
the current line instead.

## Machine Output

`--output-format json` emits newline-delimited JSON objects. Records can be
consumed incrementally by CI, Mason, or another process and may include:

- `progress`
- `diagnostic`
- `event`
- `dependency`
- `artifact`
- `cypher.tool-report.v1`

JSON mode never emits the ASCII banner, startup prose, or ANSI escape sequences.
Each nonempty line is one complete JSON object; the stream is not one enclosing
JSON array.

## Input Discovery

Compile and validate accept any mixture of:

- positional canonical virtual paths
- repeatable `-i, --input` values
- virtual directories, with direct children by default
- recursive virtual directories when `-r` is present
- `*`, `?`, and character-class wildcard patterns
- `@response` files

The VFS expands, sorts, and deduplicates discovered resources before dispatch.
Quote wildcard arguments in zsh so the tool receives the pattern instead of the
shell trying to expand it relative to the current native directory:

```sh
CypherResourceCompiler compile -s assets -o cooked 'shaders/*.cyshader'
```

The concise command for the checked-in 100-shader corpus is:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  -s tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/source \
  -o out/generated/shader-corpus \
  -r shaders/corpus
```

`host`, `development`, and terminal-aware progress are defaults, so they do not
belong in routine commands.

## Shell Completion

Load zsh completion for the current shell with:

```sh
source <(./out/build/shader-tools-debug/bin/CypherResourceCompiler completion zsh)
```

The generated definition completes commands, options, targets, profiles, modes,
and paths relative to the selected source root. It can be written into a normal
zsh completion directory for persistent use.

## Response Files

Prefix a path with `@` to expand arguments from a UTF-8 response file:

```text
compile
--source-root "assets"
--output-root "cooked"
--profile development
shaders/world.cyshader
```

Response files support whitespace-separated arguments, quoted text, escaped
quotes and backslashes, `#` comments, `//` comments, bounded recursive includes,
and cycle detection. Expansion is limited to 16 nested files, 65,536 arguments,
and 16 MiB of copied argument text by default.

Response files remain useful when a build must name an exact, reviewed input set:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler validate \
  --source-root tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/source \
  @tests/fixtures/CypherTools/CypherResourceCompiler/ShaderCorpus/shader_inputs.rsp
```

Replace `validate` with `compile` and add
`--output-root out/generated/shader-corpus` to publish all 100 cooked shaders.

## Exit Codes

| Code | Class |
| --- | --- |
| `0` | Success |
| `1` | Validation or compilation operation failed |
| `2` | Invalid command-line usage |
| `3` | Invalid project or configuration |
| `4` | Filesystem, cache, or infrastructure failure |
| `5` | Internal error or out-of-memory failure |
| `6` | Cancelled by Ctrl+C or host cancellation |

Detailed diagnostics remain authoritative; scripts should branch on the stable
coarse exit class and retain output records for the exact reason.

## Publication And Failure Rules

- Input paths are virtual resource paths below `--source-root`.
- Output paths preserve the virtual path below `--output-root` and replace the
  source extension with the compiler's cooked extension.
- `validate` never creates the output tree.
- `compile` writes a temporary artifact, validates completion, and atomically
  replaces the destination so a failed cook does not damage prior output.
- Unsupported extensions and ambiguous compiler ownership fail explicitly.
- Ctrl+C is cooperative: compilers observe the cancellation token at bounded
  pipeline checkpoints and return exit code `6`.

## Deliberately Deferred

The following are part of the ResourceCompiler direction but are not version 1
commands or switches. They must not be added as no-op options:

1. project-aware default roots and target profiles from `.cyproject`
2. dependency closure, reverse dependency queries, and rebuild explanations
3. content-addressed incremental cache and explicit `--force` semantics
4. true parallel scheduling behind `--jobs`
5. watch mode and hot-reload notifications
6. depfile, build-manifest, and standalone report-file emission
7. clean, cache inspection, and cache pruning commands
8. package update and signed shipping-manifest integration
9. remote/distributed worker dispatch
10. Qt frontend using the same compiler registry and host callbacks
11. texture, material, mesh, animation, audio, map, scene, and other compiler modules

Each capability enters the executable only with a concrete data contract,
implementation, unit coverage, and process-level integration tests.

## Verification

The process smoke suite verifies branded and licensed root help, banner-free
execution, exact version output, strict target values, live compiler and format
discovery, VFS directory and wildcard expansion, repeatable inputs, generated zsh
completion, clean JSON records, plain and forced-color text, aggregate progress
and summaries, dry-run behavior, deterministic repeated output, transactional
cleanup, exact schema locations, invalid GLSL diagnostics, and stable failure
exit codes. A separate integration test recursively validates all 100 checked-in
shader recipes through the real executable.

```sh
cmake --build --preset shader-tools-debug --parallel
ctest --preset shader-tools-debug --output-on-failure

cmake --build --preset shader-tools-release --parallel
ctest --preset shader-tools-release --output-on-failure
```
