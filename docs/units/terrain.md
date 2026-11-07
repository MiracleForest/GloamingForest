# U2 `terrain`: implementation notes

Spec: [docs/spec/terrain.md](../spec/terrain.md).

## Audit round 2

- **`terrain.timing_world` flake** (the p95 of the world chunks against the 50 ms budget: 15 ms alone, 44 ms and over
  in full runs beside builds and other servers). The chunk times were wall-clock times of the stages (noise, surface,
  blankets, carvers), so whatever else the machine ran was counted as the generator's. They are now the generating
  thread's own CPU time (`threadCpuMs()`, `src/feature/terrain/Terrain.cpp`: QueryThreadCycleTime at the time-stamp
  counter's rate, calibrated once against the steady clock), in the stage timers, the carvers and `terrain.timing`'s
  synthetic chunks. The budget (config `terrain.chunkBudgetMs`, 50) is unchanged.
- No round-1 audit items were recorded for this unit.
