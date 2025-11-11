_Metadata: Status=Canonical; Scope=Documentation index_

# RetroVue Playout Engine - Documentation Index

## Purpose

Guide contributors and operators to the correct documentation based on their goal or audience. This index mirrors the structure defined in `_standards/documentation-index-template.md`.

## Architecture

- [Architecture Overview](architecture/ArchitectureOverview.md) - System context and design drivers.
- [Project Overview](PROJECT_OVERVIEW.md) - Program-level goals and phase summaries.

## Domain models

- [Playout Engine Domain](domain/PlayoutEngineDomain.md) - Core entities and invariants.
- [Renderer Domain](domain/RendererDomain.md) - Renderer responsibilities and relationships.
- [Metrics and Timing Domain](domain/MetricsAndTimingDomain.md) - Timing and telemetry model.

## Contracts

- [Playout Engine Contract](contracts/PlayoutEngineContract.md) - Control plane guarantees and rule IDs.
- [Renderer Contract](contracts/RendererContract.md) - Renderer expectations and coverage.
- [Metrics and Timing Contract](contracts/MetricsAndTimingContract.md) - Metrics schema and enforcement.

## Runtime

- [Playout Runtime](runtime/PlayoutRuntime.md) - Execution model, threading, and failure handling.

## Developer guides

- [Quick Start](developer/QuickStart.md) - Build and run instructions.
- [Build and Debug](developer/BuildAndDebug.md) - Toolchain setup and troubleshooting.
- [Development Standards](developer/DevelopmentStandards.md) - C++ layout and naming rules.

## Infrastructure

- [Integration](infra/Integration.md) - Deployment topology and external dependencies.

## Testing

- [Contract Testing](tests/ContractTesting.md) - Harness structure, fixtures, and registry expectations.

## Milestones

- [Roadmap](milestones/Roadmap.md) - Upcoming phases and scope.
- [Phase 1 Complete](milestones/Phase1_Complete.md) - Bring-up summary.
- [Phase 2 Plan](milestones/Phase2_Plan.md) - Decode and frame bus objectives.
- [Phase 2 Complete](milestones/Phase2_Complete.md) - Deliverables and validation.
- [Phase 3 Plan](milestones/Phase3_Plan.md) - Real decode and renderer plan.
- [Phase 3 Complete](milestones/Phase3_Complete.md) - Outcomes and follow-ups.
- [Phase 3 Complete - Refactoring](milestones/Refactoring_Complete.md) - Cleanup milestones.

## Documentation principles

- Operators focus on runtime and infrastructure sections.
- Developers rely on architecture, domain, contracts, and testing content before writing code.
- Architects maintain domain and contract docs first, then guide implementation updates.

## Quick links

- Understand the architecture → [Architecture Overview](architecture/ArchitectureOverview.md)
- Build and run the engine → [Quick Start](developer/QuickStart.md)
- Inspect the gRPC API → [Playout Engine Contract](contracts/PlayoutEngineContract.md)
- Review current roadmap → [Roadmap](milestones/Roadmap.md)
- Contribute code → [Development Standards](developer/DevelopmentStandards.md)
- Look up terminology → [Glossary](GLOSSARY.md)

## See also

- [Main README](../README.md) - Repository overview.
- [Project Overview](PROJECT_OVERVIEW.md) - Cross-phase narrative.
