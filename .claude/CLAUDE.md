# OpenXilEnv — Projekt-Ground-Truth für Claude

OpenXilEnv ist ein leichtgewichtiges **SiL/HiL-Simulationsenvironment** (ursprünglich ZF, jetzt
Eclipse/Apache-2.0). Es hostet Software-under-Test / Modelle als verbundene Prozesse, treibt sie mit
einem deterministischen, fest getakteten Zeitscheduler (1 ns Auflösung), tauscht Signale und virtuellen
CAN(-FD)-Verkehr über Sockets/Named Pipes aus und bietet Recording, Stimulation, Kalibrierung (A2L/XCP),
Scripting und RPC-Automatisierung.

> **Aktuelles Vorhaben:** Migration des Frontends von **Qt Widgets → Dear ImGui** und Aufteilung des
> monolithischen Builds in echte Build-Einheiten. Der ausführliche Plan liegt unter
> `~/.claude/plans/die-grundstruktur-von-xilenvgui-linear-llama.md`. Diese Datei ist die kompakte
> Ground-Truth-Referenz dazu; Detail-Skills siehe unten.

## Coding-Standards

Es gelten die **globalen Kärcher-Style-Guides** (C, C++, CMake, Python, Doxygen) aus
`~/.claude/CLAUDE.md`. Wichtig für dieses Repo:
- **Neue GUI-Code (C++)** folgt dem Kärcher-C++-Style (PascalCase-Methoden, `m_`/`m_p`-Member,
  `enum class`, Brace-Init, keine Exceptions, Yoda-Vergleiche, Doxygen in Headern).
- **Ausnahme zur Embedded-Regel:** Dies ist ein **Desktop-Tool**, kein Embedded-Target. Dynamische
  Allokation (`std::unique_ptr`, `std::vector`, `std::string`) ist im GUI-Code **erlaubt**.
- Der **C-Core bleibt C** und wird per `extern "C"` genutzt.

## Architektur in Kürze

- **Ein Prozess, zwei Threads.** GUI = Main-Thread; Scheduler läuft auf eigenem Thread (mehrere bei
  Multicore). Geteiltes Datenmodell = **Blackboard**.
- **GUI/Core-Naht = C-ABI**: `Src/GUI/Qt/MainWinowSyncWithOtherThreads.h` (~79 `*FromOtherThread()`-
  Funktionen). Der Core ruft diese; die GUI-Impl bearbeitet sie im GUI-Thread und quittiert per `*Ack()`.
  Drei Implementierungen desselben Headers: Qt (real), Console (`-DNO_GUI`, no-op), künftig ImGui (Queue).
- **Blackboard-C-API**: `Src/Blackboard/BlackboardAccess.h` (`add/attach/remove_bbvari*`,
  `read/write_bbvari_*`). Jedes Signal hat eine `VID`, Typ, Einheit, optionale Konvertierung.
- **Widget-Plugin-Modell**: `Src/GUI/Qt/Interfaces/InterfaceWidgetPlugin.h` (Factory + Metadaten) und
  `InterfaceWidgetElement.h` (`writeToIni`/`readFromIni`/`CyclicUpdate`/Drag&Drop). 14 Fenstertypen,
  statisch registriert in `MainWindow::loadAllPlugins()`.
- **Windowing**: `Src/GUI/Qt/Sheets.h` — tabbed Workspaces (`Sheets : QMdiArea` in `QTabWidget`), je mit
  MDI-Subwindows.
- **Config-Fassade**: `Src/IniFileDataBase/IniDataBase.h` (~40 Funktionen, deskriptorbasiert, 73+ Nutzer).
  Engine-Config (`s_main_ini_val` in `Src/Global/MainValues.h`) und GUI-Layout heute in derselben ini.

## Build-Targets (Ziel-Struktur)

| Target | Typ | Inhalt |
| :-- | :-- | :-- |
| `xilenv_core` | STATIC lib | Gesamter `Src/*` außer `GUI/*` (Scheduler, Blackboard, CAN, A2L, RemoteMaster, Script, Ini, Utilities, Global, RpcInterface) |
| `XilEnv` | exe | Konsole/headless (`-DNO_GUI`) = `xilenv_core` + `Src/GUI/Console` |
| `XilEnvGui` | exe (Qt) | Legacy = `xilenv_core` + `Src/GUI/Qt` |
| `XilEnvImGui` | exe (neu) | `xilenv_core` + `Src/GUI/ImGui` + imgui/implot/glfw |
| `XilEnvRpc` | SHARED | RPC-Client-Interface (Python-API lädt diese DLL) |
| `XilEnvExtProc64/32` | SHARED | External-Process-Anbindung (SUT ↔ Core) |

Heute (vor M0) ist der Build **monolithisch**: jedes `Src/*/CMakeLists.txt` hängt seine Quellen per
`target_sources(XilEnv …)` **und** `target_sources(XilEnvGui …)` an — der Core wird doppelt kompiliert.

## Tests

- **Bestand**: nur end-to-end via `pytest` (`test/functional/`), das die installierte Binary über die
  RPC-Library (`ctypes`) treibt. **Keine C/C++-Unit-Tests.**
- **Ziel** (Schritt P1): GTest/GMock + CTest unter `test/unit/`, Target `xilenv_core_tests`. Siehe Skill
  `xilenv-gtest`.

## Skills & Agents in diesem Repo

- Skill **`openxilenv-architecture`** — detaillierte Architektur-/Datenfluss-Referenz.
- Skill **`qt-to-imgui-porting`** — Portierungs-Playbook Qt-Widget/Dialog → ImGui.
- Skill **`xilenv-gtest`** — Konvention für C-Core-Unit-Tests.
- Agent **`imgui-gui-implementer`** — implementiert ImGui-Widgets/Dialoge nach dem Playbook.
- Agent **`parity-reviewer`** — prüft ImGui-Feature gegen Qt-Pendant auf Funktionsverlust.

Die globalen `karcher-embedded:*`-Skills/Agents (Test-Engineer, DevOps, Reviewer, CMake/CI/Doxygen)
werden wiederverwendet.
