_Metadata: Status=Stable; Scope=Developer standards; Owner=@runtime-platform_

# Development standards

## Purpose

Document the local expectations for C++ module layout, matching the canonical guidance in `_standards/repository-conventions.md`.

## Scope

- Applies to native RetroVue modules such as `retrovue-air`.
- Covers header/source placement, namespace structure, and CMake exposure.
- Supplements shared standards; defer to `_standards/` when conflicts arise.

## Project layout

```
project-root/
├─ include/
│  └─ retrovue/
│     └─ <module>/
│        └─ PublicHeader.h
├─ src/
│  └─ <module>/
│     └─ PrivateImplementation.cpp
├─ tests/
│  └─ ...
├─ CMakeLists.txt
```

## Rules

1. **Public headers** live under `include/retrovue/<module>/` and mirror namespaces.

   ```cpp
   #include "retrovue/buffer/FrameRingBuffer.h"
   namespace retrovue::buffer { class FrameRingBuffer { ... }; }
   ```

2. **Private headers** stay beside their `.cpp` implementations within `src/<module>/`.
3. **CMake exposure** - export the include directory to dependents:

   ```cmake
   target_include_directories(${PROJECT_NAME}
       PUBLIC ${PROJECT_SOURCE_DIR}/include
   )
   ```

4. **One-to-one pairing** - every public header has a matching source file (`FrameRingBuffer.h` <-> `FrameRingBuffer.cpp`) unless the implementation is header-only.
5. **No flat headers** - avoid placing headers directly under `include/`. Every file belongs to a module directory.
6. **Generated code** (protobuf stubs) stays under `build/generated/retrovue/` and is never edited directly.
7. **Telemetry helpers** live under `src/telemetry/` and expose Prometheus-friendly metrics ids (`retrovue_playout_*`).

## See also

- `_standards/repository-conventions.md`
- `_standards/documentation-standards.md`
