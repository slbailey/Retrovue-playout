_Metadata: Status=Adopted; Scope=Contract testing; Domains=MetricsAndTiming, PlayoutEngine, Renderer_

# Contract testing scaffolding

## Purpose

Define the shared harness, naming rules, and regression expectations for contract suites in `retrovue-air`. This mirrors the canonical pattern documented in `_standards/test-methodology.md`.

## Scope

- Applies to tests under `tests/contracts/` and supporting fixtures.
- Covers GoogleTest suite structure, rule ID mapping, registry expectations, and CMake targets.
- Includes guidance for adding new rule coverage and keeping documentation in sync.

## Folder layout

```
tests/
├── BaseContractTest.h          # abstract contract harness
├── ContractRegistry.h/.cpp     # central test registration
├── fixtures/                   # shared stubs (MasterClockStub, etc.)
└── contracts/
    ├── MetricsAndTiming/
    │   └── MetricsAndTimingContractTests.cpp
    ├── PlayoutEngine/
    │   └── PlayoutEngineContractTests.cpp
    └── Renderer/
        └── RendererContractTests.cpp
```

- No other domains are recognised. If additional domains arise later, update this document first.

## Naming and traceability

- **One contract rule => one `TEST_F` or `TEST_P`.**
  - Every GoogleTest test name must begin with the rule ID exactly as listed in the corresponding DomainContract (e.g. `TEST_F(MetricsAndTimingContractTest, MT_001_MasterClockAuthority)`).
  - Add a comment header `// Rule: MT-001 MasterClock Authority (MetricsAndTimingDomain.md §Core Invariants)` for quick cross-reference.
- ✅ Corrected example
  ```cpp
  TEST_F(MetricsAndTimingContractTest, MT_001_MasterClockAuthority) {
    // Ensures MasterClock remains single authoritative source
    EXPECT_TRUE(clock.isMaster());
  }
  ```
- File names mirror the domain (e.g. `PlayoutEngineContractTests.cpp`).
- Fixtures live in `tests/fixtures/` and use `Stub` suffix (`MasterClockStub`, `ScheduleServiceStub`, etc.).

## Base harness

`BaseContractTest` defines shared behaviour:

```cpp
class BaseContractTest : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;

  // Every derived suite must override to register the rule IDs it covers.
  virtual std::vector<std::string> CoveredRuleIds() const = 0;
};
```

Responsibilities:

1. Provides deterministic test clock and logger hooks.
2. Exposes helper methods (`AssertWithinTolerance`, `LoadFixtureFrame`, etc.).
3. Registers covered rule IDs with `ContractRegistry` during `SetUp()`.

## Contract registry

`ContractRegistry` keeps bookkeeping centralised:

- Singleton with `RegisterSuite(domain, rule_ids)` and `ExpectAllRulesCovered(domain, all_rule_ids)` API.
- Each domain test suite calls `RegisterSuite` in `SetUp()`.
- A dedicated `ContractRegistrySanityTest` asserts every known rule is covered at least once.
- The authoritative rule lists are declared in `ContractRegistry.cpp` using data derived from:
  - `docs/domain/MetricsAndTimingDomain.md`
  - `docs/domain/PlayoutEngineDomain.md`
  - `docs/domain/RendererDomain.md`

## Fixtures and stubs

All contract tests must use the shared fixtures:

| Fixture                | Purpose                                                                         |
| ---------------------- | ------------------------------------------------------------------------------- |
| `MasterClockStub`      | Deterministic timestamps with controllable drift.                               |
| `ScheduleServiceStub`  | Provides static playout plans when the test needs ChannelManager context.       |
| `ChannelManagerStub`   | Simulates join/leave lifecycle; used by Playout Engine tests.                   |
| `MockProducer`         | Lightweight FFmpeg pipeline replacement returning synthetic frames and timings. |
| `MetricsCollectorStub` | Captures Prometheus metrics for assertions without hitting the HTTP layer.      |

Guidelines:

- Avoid mocks when the stub already covers the scenario.
- Stubs must be header-only or expose clear setup/teardown methods.
- Keep fixtures thread-safe; multiple tests may run in parallel.

## CMake integration

- A dedicated `tests/CMakeLists.txt` defines:
  - `contracts_metricsandtiming_tests`, `contracts_playoutengine_tests`, and `contracts_renderer_tests` targets linking against GTest, project libraries, and fixtures.
  - Additional per-domain executables follow the same naming (`contracts_<lowercasedomainname>_tests`) when new domains are added.
  - `enable_testing()` invoked once at the top.
- Top-level `CMakeLists.txt` includes `add_subdirectory(tests)` once GTest is present.
- Each executable exports a unique `ctest` name using the pattern `contracts_<lowercasedomainname>`, where `<lowercasedomainname>` is the domain file name without the `Domain` suffix (e.g. `ChannelRuntimeDomain` -> `contracts_channelruntime`).

**Example invocation**

```powershell
cmake --build build --target contracts_metricsandtiming_tests
ctest --tests-regex contracts_metricsandtiming --output-on-failure
```

## Adding a new rule test

1. **Identify rule** in one of the domain documents and note its identifier.
2. **Add helper logic** to fixtures if the scenario is not already supported.
3. **Create test case** under the correct domain file:
   ```cpp
   // Rule: BC-006 Monotonic PTS (PlayoutEngineDomain.md §BC-006)
   TEST_F(PlayoutEngineContractTest, BC_006_FramePtsRemainMonotonic) {
     // Arrange via stubs
     // Act with producer/renderer loop
     // Assert per contract expectations
   }
   ```
4. **List the rule ID** inside `CoveredRuleIds()` implementation.
5. **Update registry** if this is the first appearance of the rule ID.
6. **Run domain suite** via `ctest --tests-regex contracts_playoutengine`.

## Docs index automation

- `docs/tests/INDEX.md` is generated by a small script (to be added) that walks `tests/contracts/` and emits:
  - Rule ID
  - Test name
  - Source file path
  - Command to run the suite
- Regenerate the index whenever new contract tests are added.

## Review checklist

- ✔ Tests compile in both stub (`RETROVUE_STUB_DECODE`) and real decode modes.
- ✔ No direct dependency on external services (pure in-process stubs).
- ✔ Assertions map to explicit contract wording (no behavioural drift).
- ✔ Metrics assertions use `MetricsCollectorStub`.
- ✔ Control-plane tests instantiate the service with stubs only and never spin up real servers unless required by the rule.
- ✔ Coverage gap? Update the registry or file a TODO against the relevant domain doc before merging.

## Future enhancements

- Optional Python script to validate rule coverage vs. docs (parses headings, cross-checks registry).
- CI step to ensure `ContractRegistrySanityTest` runs in all configurations.
- Additional fixtures as the three domains evolve (e.g. Renderer preview harness, latency profilers).

## See also

- `_standards/test-methodology.md`
- `docs/contracts/PlayoutEngineContract.md`
- `docs/contracts/MetricsAndTimingContract.md`
- `docs/contracts/RendererContract.md`
