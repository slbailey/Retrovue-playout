_Metadata: Status=Complete; Scope=Milestone; Owner=@runtime-platform_

_Related: [Development Standards](../developer/DevelopmentStandards.md); [Phase 2 - Decode and frame bus complete](Phase2_Complete.md)_

# Code structure refactoring - complete

## Purpose

Capture the outcomes of the refactoring cycle that aligned `retrovue-air` with RetroVue’s C++ layout and naming standards.

## Delivered

- Moved public headers into `include/retrovue/<module>/` with namespaces mirroring directory structure.
- Updated include directives across `src/` and `tests/` to use canonical paths (`retrovue/buffer/FrameRingBuffer.h`, etc.).
- Modernized nested namespaces to C++17 syntax and paired headers with sources.
- Adjusted `CMakeLists.txt` to export includes and build test targets cleanly.

## Validation

- Full build succeeds with updated include paths on supported toolchains.
- Unit tests (`test_buffer`, `test_decode`) compile and link using the new structure.
- CI lint checks confirm no orphan headers or include path regressions remain.

## Follow-ups

- Keep generated proto artefacts under `build/generated/` and exclude from manual edits.
- Ensure new modules adopt the same header/source placement without exception.
- Audit downstream repos for the same pattern and schedule rollout where needed.

## See also

- `_standards/repository-conventions.md`
- `_standards/documentation-standards.md`

