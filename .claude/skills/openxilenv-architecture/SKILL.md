---
name: openxilenv-architecture
description: >-
  Architecture and data-flow reference for the OpenXilEnv SiL/HiL simulation environment — the
  core subsystems (Scheduler, Blackboard, CAN, A2L, Script, RPC, ExternalProcess), the GUI/core
  seam (MainWinowSyncWithOtherThreads C-ABI), the blackboard signal model, the ini config façade,
  the threading model, and the Qt widget-plugin/windowing model. Use when navigating OpenXilEnv
  code, planning changes across the core/GUI boundary, or onboarding onto the codebase.
---

# OpenXilEnv Architecture Reference

OpenXilEnv is a lightweight **SiL/HiL simulation environment**. Deterministic fixed-period scheduler
(1 ns resolution) hosting software-under-test / models as connected processes, exchanging signals and
virtual CAN(-FD) over sockets / named pipes. Adds recording, stimulation, A2L/XCP calibration, a custom
scripting language, and RPC automation (drives the Python API).

## Process & thread model

- **Single process, GUI = main thread.** `Src/GUI/Qt/Main.cpp` `main()` runs `StartupInit()`, builds the
  main window, calls `SchedulersStartingShot()`, enters the event loop. The headless build
  (`Src/GUI/Console/Main.cpp`) blocks on a condition variable until `terminate_main_loop()`.
- **Scheduler thread(s)** are created in `Src/Scheduler/Scheduler.c` (`SchedulerThreadProc`, one per
  scheduler/core). `IsGuiThread()` compares the stored `GuiThreadid`.
- **RPC thread**: `StartRemoteProcedureCallThread(...)` in `Src/RpcInterface/RpcSocketServer.c`.
- **External processes** run as separate OS processes, talking to the core over pipe/socket message
  threads (`Src/Scheduler/{PipeMessages,SocketMessages,UnixDomainSocketMessages}.c`).

## Core subsystems (all under `Src/`)

| Subsystem | Dir | Language | Notes |
| :-- | :-- | :-- | :-- |
| Scheduler / process handling | `Src/Scheduler/` | C | `Scheduler.c`, `tcb.h`, `SchedBarrier.c` (multicore), IPC message transports |
| Blackboard / signal DB | `Src/Blackboard/` | C | central data model — see below |
| External-process interface | `Src/ExternalProcess/` | C (+1 cpp) | client side linked into SUT; `XilEnvExtProc.h` public API |
| Ini config database | `Src/IniFileDataBase/` | C | in-memory INI DB — see below |
| Script interpreter | `Src/Script/` | C++ | lexer/parser/compiler/executor + `Commands/` |
| RPC automation | `Src/RpcInterface/` | C | `RpcSocketServer.c`, `RpcServerFunc*.c`; `XilEnvRpc.h` client lib |
| Calibration / debug | `Src/A2lParser/`, `Src/DebugInfos/`, `Src/XcpOverEthernet/` | C / C++ | |
| CAN | `Src/CanDataBase/`, `Src/CanServer/` (+ J1939) | C | |
| Data I/O | `Src/Ramps/`, `Src/StimulusPlayer/`, `Src/TraceRecorder/` | C | text / MDF |
| HiL remote master | `Src/RemoteMaster/` | C | Linux real-time side, `RealtimeScheduler.c` |
| Global glue / config | `Src/Global/` | C | `MainValues.c/.h`, `ReadConfig.c`, `StartupInit.c`, `ParseCommandLine.c` |

Language split: core/backend is **C** (~126k lines), GUI + Script are **C++**. C headers use `extern "C"`.

## Blackboard (signal database)

`Src/Blackboard/` — every signal ("bbvari") has a numeric **VID**, a name, a `BB_DATA_TYPES` type,
unit, optional conversion (`BB_VARIABLE_CONVERSION`: formula or value/text table). Structures in
`Src/Blackboard/Blackboard.h`; access API in **`Src/Blackboard/BlackboardAccess.h`**:
- Registration: `add_bbvari*`, `attach_bbvari*`, `remove_bbvari*`.
- Typed read/write: `read_bbvari_<type>` / `write_bbvari_<type>`, `_x(pid,...)` process-scoped, min/max-checked.
- Observation: `SetBbvariObservation`, `SetObserationCallbackFunction`.
- Support: `BlackboardHashIndex.c` (name→VID), `BlackboardConversion.c`, `EquationParser.c`,
  `BlackboardIniCache.c` (variable props persisted to ini), `ExportA2L.c`.
- External-process mirror: `Src/ExternalProcess/ExtpBlackboard*.c` syncs copy-lists across the socket/pipe
  boundary each cycle.

## GUI ↔ core seam (the key boundary)

The core never touches Qt. It calls a fixed set of **`extern "C"` `*FromOtherThread()`** functions declared
in **`Src/GUI/Qt/MainWinowSyncWithOtherThreads.h`** (~79 symbols: open/close window by name, add/select/
rename/delete sheet, progress bar, script dialogs, hotkey import/export, scheduler-state notifications).
Each posts a request to the GUI thread and blocks on a per-thread condition-variable slot until `*Ack()`.

Three implementations of the **same header**:
- `Src/GUI/Qt/MainWinowSyncWithOtherThreads.cpp` (~726 lines) — real Qt.
- `Src/GUI/Console/MainWinowSyncWithOtherThreads.cpp` (~241 lines) — no-op (`-DNO_GUI`).
- *(planned)* `Src/GUI/ImGui/…` — thread-safe command queue drained per frame.

Change notification for live values: `Src/GUI/Qt/BlackboardObserver.h` (QObject re-emits
`variableChanged(vid,flags)`) + `Src/GUI/Qt/WindowUpdateTimers.h` (Fast/Medium/Slow QTimers drive each
widget's `CyclicUpdate()`).

## Qt widget-plugin & windowing model

- **Plugin contract**: `Src/GUI/Qt/Interfaces/InterfaceWidgetPlugin.h` (metadata + factory:
  `GetWindowTypeInfos`, `newElement`, `openElement`, `openElementDialog`, `UpdateRate`) and
  `InterfaceWidgetElement.h` (instance: `writeToIni`, `readFromIni`, `CyclicUpdate`, `setDefaultFocus`,
  drag&drop). Base classes `Widgets/MdiWindowType.h` / `MdiWindowWidget.h`.
- **14 window types**, statically registered in `MainWindow::loadAllPlugins()` (`Src/GUI/Qt/MainWindow.cpp`):
  Text, Oscilloscope, Lamps, Slider, Enum, Tacho, Bargraph, Knob, CalibrationTree, UserDraw, UserControl,
  A2LCalSingle, CANMessageWindow, A2LCalMap.
- **Windowing**: `Src/GUI/Qt/Sheets.h` — `Sheets : QMdiArea` inside a `QTabWidget`; tabbed workspaces, each
  holding MDI subwindows. Layout persisted to ini (`Sheets.cpp writeToIni/readFromIni`, `QtIniFile.cpp`).
- **Custom-painted (highest migration effort):** Oscilloscope (real-time, C sampler `OscilloscopeCyclic.c`),
  Knob/Tacho gauges, A2LCalMap 3D view (software `QPainter` projection), Lamps (`QGraphicsView`),
  UserDraw/UserControl (scriptable ~30-primitive vector engine).

## Config (ini) façade

`Src/IniFileDataBase/IniDataBase.h` (~40 functions) is an **in-memory INI database**: files open into
descriptors (`int` fd), read/written in RAM, flushed with `IniFileDataBaseSave`. Read/Write of
String/Int/Float/YesNo/Section, `…ReadByteImage`/`…WriteByteImage` (binary blobs), section enumeration,
regex section search, per-descriptor critical sections. Section/entry name constants partly centralized in
`IniSectionEntryDefines.h`, mostly scattered in consumers. `Src/Global/ReadConfig.c` decodes the ini into
the global struct **`s_main_ini_val`** (`Src/Global/MainValues.h`, ~132 fields). GUI layout is persisted to
the **same** ini (`GUI/…` sections) — engine config and GUI state are commingled. ~73 files include the
façade directly.

## Automation

Two paths, both GUI-independent: the custom **Script** language (`Src/Script/`, runs as a scheduled
internal process, reaches the GUI via `*FromOtherThread` dialog/progress calls) and **RPC**
(`Src/RpcInterface/`, exposed through `python-api/openxilenv`, tested by `test/functional/`).

## Build (current, pre-M0)

Monolithic: top-level `CMakeLists.txt` declares all targets; each `Src/*/CMakeLists.txt` appends its
sources with `target_sources(XilEnv …)` **and** `target_sources(XilEnvGui …)` (core compiled twice).
Include dirs enumerated centrally per target. Only the Linux RemoteMaster trio has real target-to-target
linkage. Qt: `find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets)` (+ optional Svg); AUTOUIC/AUTOMOC/AUTORCC.
