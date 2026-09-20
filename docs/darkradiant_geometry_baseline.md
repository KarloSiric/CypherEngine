<!--
CypherEngine Research Note
File: docs/darkradiant_geometry_baseline.md
Purpose: Empirical size baseline for a mature Radiant-class geometry foundation,
         used to calibrate CypherGeometry scope estimates.
Provenance: DarkRadiant figures were produced externally by enumerating and
            counting files at the pinned revision below. NetRadiant figures were
            measured locally against a working copy on 2026-09-21 as an
            independent cross-check.
            This note records file names and line counts only. It contains no
            algorithms, no source excerpts, and no derived implementation
            guidance, so it carries a much lighter provenance burden than
            docs/trenchbroom_geometry_algorithms.md. DarkRadiant is GPL; confirm
            the exact version before any code-derived work.
            Original analysis (c) 2026 Karlo Siric.
-->

# DarkRadiant Geometry Size Baseline

**Reference repository:** `codereader/DarkRadiant`
**Reference revision:** tag `3.9.0`, commit `f84caabf23cce90dbf71d93c8cb21995fecfa951`
**Cross-check repository:** NetRadiant (GtkRadiant 1.5 descendant), local working copy
**Date:** 2026-09-21

**Metric:** *physical* lines — including comments and blank lines — not `cloc`
logical SLOC. The two are not interchangeable and are never mixed below.

## Why this note exists

Scope estimates for `CypherEditorGeometry` were being made top-down from the
module tree, which has 71 directories and invites inflation. This note supplies
a bottom-up anchor: what a mature, shipping, Radiant-class geometry foundation
actually costs in practice.

The headline result corrects an over-estimate:

> A mature Radiant-class geometry foundation is on the order of
> **~25k physical lines**, not 100k+.

## Measured baseline

DarkRadiant 3.9.0 overall is roughly **264,743 C++ SLOC + 10,659 C SLOC**. Only a
fraction is the geometry foundation comparable to what Cypher is building.

| Area | Files | Physical lines |
| --- | ---: | ---: |
| Core math | 25 | 4,687 |
| Brush geometry, CSG, texturing, export | 36 | 7,465 |
| Patch geometry and tessellation | 19 | 6,202 |
| Geometry-facing selection and manipulation | 32 | 5,297 |
| **Total** | **112** | **23,651** |

Notable individual files: `Patch.cpp` 2,594, `Brush.cpp` 1,381,
`PatchTesselation.cpp` 893, `Face.cpp` 793, `Matrix4.h` 603, `csg/CSG.cpp` 508.

## Independent cross-check

DarkRadiant and NetRadiant are both GtkRadiant descendants, so comparable areas
should land in the same range. Measured locally against NetRadiant:

| Area | DarkRadiant (external) | NetRadiant (measured here) |
| --- | ---: | ---: |
| Math library | 4,687 | 4,417 |
| Brush + CSG + winding | 7,465 | ~8,538 |
| Patch | 6,202 | 7,956 |

Math agreeing within 6% across two independently maintained forks is the
strongest signal here; the methodology holds. Brush and patch differ by the
amount expected from divergent feature sets.

Selection was **not** cross-checked usefully: a broad file-name match on
NetRadiant returns 9,309 lines, but that sweeps in the whole selection system
rather than the geometry-facing subset the DarkRadiant figure isolates. This is
a classification difference, not a contradiction.

## What the baseline does and does not cover

DarkRadiant's geometry model is **brushes plus bezier patches**. It does not
contain:

- a half-edge editable mesh with Euler operators
- general arbitrary-mesh boolean (corefinement, coplanar overlay, reconstruction)
- subdivision surfaces, sweeps, or displacement as authored sources
- curve networks or height fields as canonical representations
- transactional mutation with provenance and rollback
- exact-sign geometric predicates

Those are precisely the areas where Cypher intends to exceed the baseline, and
they are why Cypher's number will be larger. The baseline is a **reference
point, not a target**.

## Calibration for CypherEditorGeometry

Reading the baseline against the module tree:

| Stage | Scope | Physical lines |
| --- | --- | ---: |
| Radiant-class capability | brush + patch + math + geometry-facing manipulation | ~25k |
| Cypher additions | editable mesh, mesh CSG, procedural, modifiers, transactions, validation, cook | ~90–180k |
| **CypherEditorGeometry complete** | all planned representations and operations | **~115–205k** |

Two independent estimates now converge on this band: a bottom-up per-module
estimate produced on 2026-09-21 gave ~140–290k, and the external study gave
~115–205k. The overlap is real; the upper tails differ because CSG is the
least predictable component in both.

### Corrections this baseline forces

- **Patches were under-estimated.** The per-module estimate allowed 2–4k for
  `Representations/Patch`. DarkRadiant spends 6,202 lines on patches, with
  `Patch.cpp` alone at 2,594 and tessellation at 893. Revise to **5–9k**.
  Curved-surface authoring is substantial before arbitrary topology is involved.
- **71 modules is over-decomposed for a first version.** A mature editor reaches
  Radiant-class capability in 112 files. Expect a meaningful number of the
  planned directories to collapse into neighbours or never be built, consistent
  with the existing rule that files enter the build only when a responsibility
  has an implemented contract and focused tests.

## Scope boundary: this is CypherGeometry, not Mason

**Everything above sizes `CypherEditorGeometry` alone.** Mason is a separate and
substantially larger product, and its numbers must not be conflated with these.

DarkRadiant illustrates the ratio: ~23.6k physical lines of geometry foundation
inside an application of ~275k SLOC. The other ~250k is GUI, map management,
entities, renderer, materials, models, scripting, module system, commands, and
filesystem integration.

Cypher's own projection in `map_authoring_and_mason.md` says the same thing from
the other direction: complete mature mapping stack `210k–500k`, complete
long-term Mason suite `500k–1M+`. Geometry is a component of that, not the whole.

When quoting a number, state which layer it refers to.

## Implication for build order

The baseline supports the sequencing already in the implementation plan: build
the headless kernel first and do not begin Mason's Qt surface early.

```text
Core -> Kernel -> BrushSolid -> Planar -> EditableMesh -> Operations
     -> Transactions -> Validation -> CSG -> Procedural -> Cook
```

The milestone that matters is not a line count. It is being able to create a
room, manipulate its vertices, edges and faces, run CSG on it, validate it, undo
it, serialize it and cook it — entirely from tests and a headless harness, with
no editor UI in existence.

## References

- DarkRadiant 3.9.0 source listing, Debian Sources:
  `https://sources.debian.org/src/darkradiant/3.9.0-1/`
- DarkRadiant repository: `https://github.com/codereader/DarkRadiant`
- Related Cypher notes: `docs/trenchbroom_geometry_algorithms.md`,
  `docs/map_authoring_and_mason.md`, `docs/reference_policy.md`
