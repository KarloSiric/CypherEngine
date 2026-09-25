# Csg

Regularized solid Booleans. Mesh CSG runs as explicit stages (input normalization, broad phase, predicates, intersections, corefinement, cells, coplanar overlay, classification, expression, boundary extraction, attribute transfer, stitching, reconstruction, cleanup); the brush path and the document commands sit on top.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_BrushCsgAdoption` | Document-ready brush subtraction result preparation |
| `CypherGeometry_CsgAttributes` | Mesh-CSG attribute transfer: materials and smoothing from the source face, corner UVs and colours interpolated at the new corners, crease/flags along source edges, the cut-face... |
| `CypherGeometry_CsgBoundary` | CSG boundary extraction: the refined triangles the region decision keeps, oriented outward, minus any closed component that encloses no volume |
| `CypherGeometry_CsgBroadPhase` | CSG candidate generation: the pairs (triangle of A, triangle of B) whose bounding boxes touch or overlap |
| `CypherGeometry_CsgBrush` | Booleans over sets of convex brushes that keep the result as convex brushes - the form a brush-based map (and the BSP-style compile after it) needs |
| `CypherGeometry_CsgCells` | The CSG cell complex: the refined surfaces cut into patches (cells) along the curves where the operands meet, each cell one operand's connected piece of surface that is entirely... |
| `CypherGeometry_CsgClassify` | CSG classification: every cell is labelled inside, outside, or shared (same / opposite facing) with respect to the other operand |
| `CypherGeometry_CsgCleanup` | CSG cleanup: removing constructed vertices that no longer shape anything, and the final structural validation of a result before it is published |
| `CypherGeometry_CsgCoplanar` | Coplanar overlay resolution: refined triangles of A and B that are the same piece of surface (the overlap of two coplanar faces) are paired and marked as facing the same way or... |
| `CypherGeometry_CsgCorefine` | Corefinement: every input triangle of both operands is split along the intersection segments and points recorded against it, so the two refined surfaces share every vertex and... |
| `CypherGeometry_CsgExpression` | The CSG region decision: for an operator, which labelled cells of which operand survive into the result, and which survive turned inside out |
| `CypherGeometry_CsgInput` | CSG input preparation: an authored mesh becomes a triangulated operand with source mapping, validated, optionally quantised, and put in a canonical triangle order |
| `CypherGeometry_CsgIntersections` | CSG intersection construction: for every candidate triangle pair, the points and segments where the two surfaces meet, recorded against the triangles that must be split there |
| `CypherGeometry_CsgMesh` | The general mesh Boolean: union, intersection, difference, symmetric difference and clip of two authored meshes of any shape (convex or not, several shells, holes), as a pipeline... |
| `CypherGeometry_CsgOperations` | The document-level Boolean commands the editor calls |
| `CypherGeometry_CsgPredicates` | The exact decisions the CSG pipeline takes on input coordinates |
| `CypherGeometry_CsgReconstruct` | CSG reconstruction: the kept boundary triangles become a polygon mesh description again - one polygon per surviving piece of each source face where possible - with transferred... |
| `CypherGeometry_CsgStitch` | CSG stitching: the points the result boundary uses become its vertex table, joined by identity only |
| `CypherGeometry_CsgTypes` | The records shared by the mesh CSG pipeline stages (InputNormalization -> BroadPhase -> Intersections -> Corefinement -> CellComplex/Classification/CoplanarOverlay -> Expression... |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### CSG

#### Owns

Regularized solid Boolean pipelines, with a fast brush path and a later arbitrary closed-mesh path.

#### Does not own

A single monolithic Boolean function or retention of dangling zero-volume results as solids.

#### First acceptance gate

Deliver brush intersection/subtraction before general mesh Booleans; each stage returns bounded diagnostics and provenance.

### CSG / InputNormalization

#### Owns

Canonical validation, orientation, quantization, and representation-specific CSG input preparation.

#### Does not own

Repair of authored source without an explicit transaction.

#### First acceptance gate

Produce canonical equivalent input ordering for permuted valid operands.

### CSG / BroadPhase

#### Owns

Deterministic candidate generation using bounds and spatial indexes.

#### Does not own

Exact intersection classification.

#### First acceptance gate

Produce the same ordered candidate pairs for stable equivalent input.

### CSG / Predicates

#### Owns

Topology-aware robust side, overlap, incidence, and coplanarity decisions.

#### Does not own

Generic scalar predicate implementations that belong in Cypher::Math.

#### First acceptance gate

Classify adversarial near-coplanar fixtures consistently or report indeterminate.

### CSG / Intersections

#### Owns

Controlled curve, segment, and point construction with witness and provenance records.

#### Does not own

Final topology reconstruction.

#### First acceptance gate

Construct bounded intersection records without duplicate canonical events.

### CSG / Corefinement

#### Owns

Splitting operand faces/regions along a shared intersection graph.

#### Does not own

Inside/outside expression evaluation.

#### First acceptance gate

Produce mutually conforming boundaries with source remaps.

### CSG / CellComplex

#### Owns

Optional bounded region/cell records required for expression evaluation and boundary extraction.

#### Does not own

A runtime BSP or visibility structure.

#### First acceptance gate

Represent classified CSG regions without losing operand provenance.

### CSG / CoplanarOverlay

#### Owns

Planar arrangement and region overlay for coincident and partially coincident faces.

#### Does not own

3D broad-phase traversal.

#### First acceptance gate

Resolve shared-face and partial coplanar overlap fixtures deterministically.

### CSG / Classification

#### Owns

Inside, outside, boundary, winding, containment, and ambiguity classification.

#### Does not own

Boolean expression syntax or reconstruction.

#### First acceptance gate

Classify disjoint, contained, touching, nested-shell, and multiple-component corpora.

### CSG / Expression

#### Owns

Evaluation of union, intersection, difference, symmetric difference, clip, and slice region predicates.

#### Does not own

Geometric intersection construction.

#### First acceptance gate

Evaluate equivalent Boolean expression inputs with canonical decisions.

### CSG / BoundaryExtraction

#### Owns

Selecting oriented result boundaries from classified regions.

#### Does not own

Stitching invalid adjacency silently.

#### First acceptance gate

Extract regularized boundaries with no isolated zero-volume components.

### CSG / AttributeTransfer

#### Owns

Material, UV, normal, smoothing, crease, and generated-cut-face policy during CSG.

#### Does not own

Asset loading or shader semantics.

#### First acceptance gate

Transfer original face attributes and apply an explicit cut-face material policy.

### CSG / Stitching

#### Owns

Canonical joining of reconstructed boundaries under explicit tolerance and identity rules.

#### Does not own

General repair of unrelated imported meshes.

#### First acceptance gate

Join matching boundaries without accidental proximity welding.

### CSG / Reconstruction

#### Owns

Building checked result representations from extracted boundaries and source maps.

#### Does not own

Attribute invention or host object creation.

#### First acceptance gate

Reconstruct valid outward shells or fail atomically with witnesses.

### CSG / Cleanup

#### Owns

Regularization, redundant element removal, deterministic simplification, and final validation.

#### Does not own

Unbounded heuristic healing.

#### First acceptance gate

Remove zero-measure artifacts while preserving valid authored boundaries and provenance.

### CSG / Mesh

#### Owns

Regularized Boolean evaluation over validated arbitrary closed editable meshes through intersection graphs, corefinement, coplanar overlay, classification, and reconstruction.

#### Does not own

Brush shortcuts, malformed import sanitation, or non-manifold source publication.

#### First acceptance gate

Begin only after planar arrangements and mesh topology operations pass their adversarial corpus; then prove union, intersection, and difference with complete source and attribute provenance.

### CSG / Brush

#### Owns

Regularized Boolean evaluation over validated plane-defined convex brush operands, including clipping, region selection, boundary reconstruction, and side provenance.

#### Does not own

Arbitrary closed-mesh corefinement or conversion of concave results back into one convex brush. A result may be a deterministic set of convex brushes or an explicit mesh conversion.

#### First acceptance gate

Deliver contained, disjoint, touching, shared-plane, intersection, and subtraction fixtures before brush union decomposition.

### CSG / Operations

#### Owns

Public Boolean descriptors and brush/mesh operation entry points.

#### Does not own

Internal stage storage or UI commands.

#### First acceptance gate

Expose union, intersection, difference, symmetric difference, clip, and slice as transactional operations.
