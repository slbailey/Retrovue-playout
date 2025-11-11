_Metadata: Status=Active; Scope=Roadmap; Owner=@runtime-platform_

_Related: [Architecture Overview](../architecture/ArchitectureOverview.md); [Project Overview](../PROJECT_OVERVIEW.md)_

# RetroVue Playout Engine - Roadmap

## Purpose

Track phased delivery for the RetroVue playout engine and link to the supporting plan/complete documents for each milestone.

## Phase status

| Phase   | State    | Description                                       | Plan doc                      | Completion doc                        |
| ------- | -------- | ------------------------------------------------- | ----------------------------- | ------------------------------------- |
| Phase 1 | Complete | gRPC skeleton and contract bring-up               | -                             | [Phase1_Complete](Phase1_Complete.md) |
| Phase 2 | Complete | Frame buffer, stub decode, telemetry foundation   | [Phase2_Plan](Phase2_Plan.md) | [Phase2_Complete](Phase2_Complete.md) |
| Phase 3 | Complete | FFmpeg decode, Renderer integration, HTTP metrics | [Phase3_Plan](Phase3_Plan.md) | [Phase3_Complete](Phase3_Complete.md) |
| Phase 4 | Planned  | Production hardening and multi-channel support    | -                             | -                                     |

## Phase snapshots

### Phase 1 - Skeleton

- Delivered the control plane (`PlayoutControlImpl`), build system, and Python smoke test.
- Ready for downstream integration with RetroVue Core.
- See also: [Phase1_Skeleton](Phase1_Skeleton.md)

### Phase 2 - Decode and frame bus

- Implemented lock-free `FrameRingBuffer`, stub `FrameProducer`, and Prometheus metrics exporter.
- Added unit tests and standards-compliant header structure.
- See also: [Refactoring_Complete](Refactoring_Complete.md)

### Phase 3 - Real decode and renderer

- Brought FFmpeg decoding online, wired Renderer implementations, and exposed `/metrics`.
- Validated end-to-end pipeline with real media assets and multi-codec coverage.
- Detailed notes in `archive/Phase3_FFmpeg_Details.md` and `archive/Phase3_Renderer_Details.md`.

### Phase 4 - Production readiness (planned)

- Goals:
  - Integrate MasterClock for deterministic timing.
  - Support multi-channel playout with error recovery and slate injection.
  - Harden telemetry and alerting for 24/7 operation.
  - Automate performance benchmarking and regression detection.
- Dependencies: completion of Phase 3 follow-ups and RetroVue Core scheduling enhancements.

## Documentation pattern

- Every phase publishes `PhaseN_Plan.md` (scope) and `PhaseN_Complete.md` (results).
- Deep-dives and supplemental notes live in `docs/milestones/archive/`.
- Refactoring milestones reuse the same Purpose → Delivered → Validation → Follow-ups structure.

## See also

- `docs/README.md` - documentation index.
- `docs/runtime/PlayoutRuntime.md` - runtime behavior.
- `docs/contracts/PlayoutEngineContract.md` - authoritative contract for channel lifecycle.
