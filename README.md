# UnrealVoxelSim.Simulation.FixedStep

Thread-affine deterministic fixed-step implementation of `UnrealVoxelSim.Simulation.Api`. The controller owns only tick
and pacing state and invokes a composition-provided `ITickPipeline`; it contains no domain phases or system registry.

Explicit stepping and paced advancement use the same tick path. Pacing applies a reduced rational global rate using
integer arithmetic, retains backlog when an interactive tick budget is exhausted, and never changes step duration or
skips simulation work.
