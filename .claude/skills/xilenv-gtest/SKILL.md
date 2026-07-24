---
name: xilenv-gtest
description: >-
  Convention for C-core unit and integration tests in OpenXilEnv using GoogleTest + GMock and CTest
  — how to test the C core (extern "C" wrapping) from C++, the test target/layout (test/unit/,
  xilenv_core_tests), characterization / golden-master tests for existing behaviour, GMock for the
  MainWinowSyncWithOtherThreads seam, and priority coverage (Blackboard, ini façade, sync-ABI,
  Scheduler, A2L, Script). Use when creating, scaffolding, or fixing GTest tests for the core, or
  wiring the CTest stage.
---

# OpenXilEnv GTest / CTest Convention

Purpose: build a regression net **before** the Qt→ImGui migration so that no functionality is lost and
no reimplementation drifts. Builds on the Kärcher unit-testing standards (AAA pattern,
`Method_Scenario_Expected` naming, `pre_/test_/expect_/result_` variable prefixes, "specification is
sacred") — see the global `karcher-embedded:karcher-unit-testing` skill. This skill adds the OpenXilEnv
specifics.

## Framework & layout

- **GoogleTest + GMock via `FetchContent`, pinned by SHA/tag.** `enable_testing()` + CTest at top level.
- New **`test/unit/`** (C++) beside the existing `test/functional/` (pytest stays as end-to-end).
- Test target **`xilenv_core_tests`**:
  - **Before M0** (no core lib yet): the target compiles the specific core `.c` sources under test
    directly (mirror the include dirs the exes use in the root `CMakeLists.txt`).
  - **After M0**: `target_link_libraries(xilenv_core_tests PRIVATE xilenv_core gtest gmock gtest_main)`.
- One test file per subsystem area, e.g. `test/unit/blackboard/test_blackboard_access.cpp`.

## Testing C from C++

The core is C with `extern "C"` headers. Include them normally from `.cpp` test files:
```cpp
extern "C" {
#include "Blackboard/BlackboardAccess.h"
}
```
Many core functions rely on global state and `StartupInit()`-style bring-up. Use a **fixture** that sets
up / tears down the minimal core state (blackboard init, ini descriptor) per test; document any global
state that cannot be reset so tests stay independent.

## Test kinds

1. **Characterization / golden-master** — capture the *current* behaviour of the Qt/legacy build as the
   reference, so refactors (M0) and reimplementations (M1 TOML, M2 command queue) can be proven equivalent.
   For parsers (A2L, Script) store golden output files under `test/unit/<area>/golden/` and diff.
2. **Contract tests** — pin the documented behaviour of a public API (the ini façade's ~40 functions, the
   blackboard read/write/convert contract, the sync-ABI request/ack semantics).
3. **Unit tests** — isolated logic (hash index, conversion formulas, equation parser, scheduler barrier).

## GMock for the GUI seam

`Src/GUI/Qt/MainWinowSyncWithOtherThreads.h` is a C-ABI. To test core code that calls it without a GUI,
provide a **mock/fake implementation** of the `*FromOtherThread()` symbols (link a test double instead of
the Qt/Console impl) that records calls and returns programmed acks. This same contract test is the
**reference for verifying the ImGui command-queue implementation** (M2): both must satisfy the identical
request/ack/blocking/timeout behaviour.

## Priority coverage (matches plan P1)

1. **Blackboard** (`Src/Blackboard/BlackboardAccess.h`): add/attach/remove, typed read/write, conversion &
   formulas, observation callbacks, name→VID index. **Highest priority — central data model.**
2. **Ini façade** (`Src/IniFileDataBase/IniDataBase.h`): contract tests of all ~40 functions incl.
   `…ReadByteImage`/`…WriteByteImage` (binary blobs). **The baseline to prove the M1 TOML reimplementation
   behaves identically.**
3. **Sync-ABI** (`MainWinowSyncWithOtherThreads.h`): request/ack, blocking, timeouts. **The template the M2
   command-queue impl is checked against.**
4. **Scheduler** (cycles/barrier), **A2L parser** (parse golden-master), **Script** (compile/execute small
   scripts).

## CTest & CI

- Register tests with `gtest_discover_tests(xilenv_core_tests)`.
- Extend `.github/workflows/build.yml` with a **CTest stage** on Linux + Windows, running before the
  existing pytest stage (`ctest --output-on-failure`).
- Keep the pytest functional suite (`test/functional/`) as a second, GUI-independent guard; reactivate the
  `skip`-marked tests where feasible.

## Style checklist

- AAA layout; `TEST_F(BlackboardAccessTest, WriteThenRead_Double_ReturnsSameValue)`.
- Variable prefixes `pre_`, `test_`, `expect_`, `result_`.
- Yoda comparisons in assertions where a literal is compared.
- No test depends on another; fixtures reset shared state.
- When a test fails, treat the **specification / documented behaviour as sacred** — fix the code or the
  test's understanding of the contract, never weaken the assertion to make it pass.
