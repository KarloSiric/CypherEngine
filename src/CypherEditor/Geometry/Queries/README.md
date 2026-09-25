# Queries

Read-only questions about geometry - brush and face queries, ray casts, picking - and component selection sets with topology-aware selection tools.

All headers here are included by basename; the folder is only for people. See the [Geometry index](../README.md) for the whole layout.

## Files

| Header | Purpose |
| --- | --- |
| `CypherGeometry_BrushFaceQueries` | Face queries that span several brushes - the coplanar flood fill that selects a whole surface made of many brushes' faces (a floor built from several blocks) |
| `CypherGeometry_BrushQueries` | Derived spatial and component queries on brushes |
| `CypherGeometry_MeshSelection` | Component selection on mesh sources (vertices, edges, faces by persistent ID), topological selection tools (grow, shrink, connected, edge loop/ring, mode conversion), and remap of... |
| `CypherGeometry_MeshSelectionQueries` | Attribute, geometric, and spatial selection queries on mesh sources (select by material, coplanar, facing, sharp edges, boundary, face size, invert, inside a convex volume) and... |
| `CypherGeometry_PickingQueries` | Allocation-free component and planar-region picking |
| `CypherGeometry_RaycastQueries` | Brute-force authoring ray casts for brushes and meshes |
| `CypherGeometry_SelectionSet` | Typed geometry component selection sets |

## Contracts

Ownership contracts carried over from the module folders that were merged into this one.

### Queries

#### Owns

Read-only adjacency, incidence, boundary, connected-component, containment, ray, closest-point, measurement, feature, and self-intersection queries.

#### Does not own

Mutation, caching policy owned by Spatial, or selection state.

#### First acceptance gate

Return deterministic area, volume, centroid, boundary, and connected-component results for canonical fixtures.

### Selection

#### Owns

Representation-qualified geometry component references, selectable-target views,
sets, grow/shrink, connected, loop/ring, and mutation-remap behavior.

#### Does not own

Internal topology-handle identity, global selection routing, Qt models, gizmo
state, TileEditor cells, or non-geometry scene objects.

#### First acceptance gate

Keep face, edge, and vertex selections stable across a split/merge remap with explicit ambiguity reporting.
