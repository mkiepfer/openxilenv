# Changelog

All notable changes to OpenXilEnv are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to adhere to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Detailed, per-release notes are generated automatically from the merged pull
> requests and are published with each
> [GitHub Release](https://github.com/eclipse-openxilenv/openxilenv/releases).
> This file provides the curated, human-readable high-level history.

## [Unreleased]

### Added
- **New Dear ImGui front-end `XilEnvImGui`** (GLFW + OpenGL3, docking) — a full port of the
  Qt Widgets GUI: all 15 window types (Text, Slider, Enum, Bargraph, Lamps, Knob, Tacho,
  VariableList, Oscilloscope, CANMessage, A2LCalSingle, CalibrationTree, A2LCalMap, UserDraw,
  UserControl) and the complete dialog set (settings, CAN/XCP/CCP + DBC, process/scheduler/
  barrier, conversion/blackboard/import-export/debug dialogs, CAN variant/object/signal editor).
  Config files are interchangeable with the Qt GUI. Now built and installed as the primary GUI;
  the Qt `XilEnvGui` is retained in parallel for verification.
- **`xilenv_core` static library**: the core (scheduler, blackboard, CAN, A2L, script, RPC, ini)
  is built once and linked by `XilEnv`, `XilEnvGui`, `XilEnvImGui` and the unit tests instead of
  being compiled per front-end.
- **C-core unit tests** (GoogleTest + CTest under `test/unit/`, target `xilenv_core_tests`) as a
  regression net — blackboard, ini façade, TOML round-trip, equation/A2L/CAN/S19/ramps helpers.
- Automated release pipeline: pushing a version tag (`v*`) builds self-contained
  Linux and Windows packages via CPack and publishes them together as a GitHub Release.
- CPack packaging support (`.tar.gz` on Linux, `.zip` on Windows), reproducible
  manually with `cpack`.

### Changed
- **Configuration format is now TOML** (behind the unchanged `IniDataBase` façade), split into an
  engine config (`<name>.toml`) shared by all executables and a GUI layout file
  (`<name>.layout.toml`). An `ini`→TOML converter tool is provided; legacy `.ini` files are still
  read via a fallback parser.

### Fixed
- `IniFileDataBaseReadByteImage` no longer leaks the ini critical section on its size-query/error
  paths (previously deadlocked any caller using the query-size-then-read pattern).

## [0.9.2] - 2026-01-22

Current baseline release of OpenXilEnv (the open X-in-the-loop simulation
environment), including the GUI (`XilEnvGui`), the console variant (`XilEnv`),
the RPC interface, the external-process interface, the Python API, and CAN
message decoding.

[Unreleased]: https://github.com/eclipse-openxilenv/openxilenv/compare/v0.9.2...HEAD
[0.9.2]: https://github.com/eclipse-openxilenv/openxilenv/releases/tag/v0.9.2
