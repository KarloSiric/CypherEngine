# Cook / SourceMapping

## Owns

Cooked triangle and product records back to geometry root/component source IDs
and source representation kinds.

## Does not own

Mason/CypherTileEditor object IDs, live handles, human-readable diagnostic
presentation, or the host adapter that joins a geometry root to a scene object.

## First acceptance gate

Resolve every cooked primitive in canonical fixtures back to its authored origin.
