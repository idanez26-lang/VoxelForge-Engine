# Constraint Engine

`ConstraintEngine::Solve()` is the sole public transformation-constraint
entry point. A caller supplies a tool-neutral transform and settings; the
result contains the constrained transform and change flags.

The V1 pipeline is deterministic, allocation-free, and O(1). Grid constraints
affect position only, rotation constraints affect Euler degrees only, and scale
passes through unchanged. Individual constraint implementations remain private
to the engine so future surface, voxel, vertex, edge, plane, collision, or smart
constraints can be added without changing the public `Solve()` signature.

Voxel transforms currently apply integral cell moves and quarter-turn
rotations. Their internal settings therefore use a 1-cell grid and 90-degree
rotation step; finer supported steps are available to future transform clients.
