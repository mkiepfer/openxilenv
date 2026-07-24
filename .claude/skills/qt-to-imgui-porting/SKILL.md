---
name: qt-to-imgui-porting
description: >-
  Playbook for porting the OpenXilEnv GUI from Qt Widgets to Dear ImGui (docking branch) — the
  IWindowType/IWindowInstance plugin model, the command-queue replacement for the
  MainWinowSyncWithOtherThreads C-ABI, the per-frame blackboard live-data flow, data-driven
  dialogs instead of .ui files, ImDrawList/ImPlot recipes for custom widgets, blackboard-variable
  drag&drop, and TOML layout persistence. Use when porting any Qt window type, dialog, or custom
  painter to ImGui, or when writing new XilEnvImGui code.
---

# Qt → Dear ImGui Porting Playbook (OpenXilEnv)

Follow this when porting any Qt widget/dialog to the new `XilEnvImGui` target. Read the
`openxilenv-architecture` skill first for the seam and blackboard details. Code style = Kärcher C++
(PascalCase methods, `m_`/`m_p` members, `enum class`, brace-init, Yoda comparisons, Doxygen in headers).
Dynamic allocation is **allowed** here (desktop tool, not embedded).

## Stack

- **GLFW + OpenGL3 (3.3 core)**, ImGui **docking branch** (docking + multi-viewport).
- **ImPlot** (plots/oscilloscope), **ImGuiFileDialog** (file dialogs), **toml++** (config),
  IconFontCppHeaders + merged icon font (menu/toolbar icons).
- Vendor under `third_party/` or `FetchContent` pinned by SHA. ImGui + backends compiled as a small
  `imgui` static-lib target.

## Golden rule: the core does not change

The C core talks to the GUI only through `Src/GUI/Qt/MainWinowSyncWithOtherThreads.h` and the blackboard
C-API. Keep that **exact header/ABI**. The ImGui GUI provides a new *implementation* of the same symbols.
Never edit core `.c` files to accommodate the GUI.

## 1. Window-type plugin model (replaces InterfaceWidgetPlugin/Element)

No moc, no `Q_OBJECT`. Two abstractions + a registry:

- **`IWindowType`** (was `InterfaceWidgetPlugin`/`MdiWindowType`): `GetUpdateRate()`, `GetTypeName()`,
  `GetIniListName()`, `GetMenuEntryName()`, `HasStorage()`, factories `CreateNew(title)` /
  `CreateFromConfig(node)` returning `std::unique_ptr<IWindowInstance>`.
- **`IWindowInstance`** (was `InterfaceWidgetElement`/`MdiWindowWidget`): `RenderContent(ctx)` (called
  between `ImGui::Begin/End` each frame), `OnUpdateTick(ctx)` (throttled value-cache refresh),
  `RenderSettingsPopup()`, `WriteConfig(node)` / `ReadConfig(node)`, `OnAcceptVariableDrop(varName)`.
  Holds cached `VID`s in `m_Signals`.
- **`WindowTypeRegistry`** (singleton, owning) + `RegisterBuiltinWindowTypes()` — replaces the hard-coded
  `loadAllPlugins()`. All 14 types registered statically: `Registry.Register(std::make_unique<TextType>())`.
- `UpdateRate` → `enum class UpdateRate { Fast, Medium, Slow, None }`.

## 2. Command queue (replaces the ~79 `*FromOtherThread()` calls)

New impl file (e.g. `Src/GUI/ImGui/ImGuiSyncWithOtherThreads.cpp`) implementing the same C symbols:
- Each call packages a tagged command (`enum class GuiCommandType` + POD args) and pushes onto a
  thread-safe MPSC queue.
- The render thread calls `DrainCommandQueue()` at frame start, executes each command against the ImGui
  window model, and for **blocking** calls (those returning `int`) calls the matching `*Ack(threadId,ret)`
  using the **same per-thread wait/wake slot pattern** as the Qt class (`m_Wait[32]`, condition variables).
- **Fire-and-forget** calls (void returns: progress bar, realtime-state) just enqueue and return.
- Keep `SetSyncWithGuiThreadIsInitAndRunning()`, timeouts, per-thread slots — behavioral drop-in.

## 3. Live data (replaces BlackboardObserver + QTimer + signals/slots)

- **Immediate mode**: in `RenderContent`, read signals directly via the blackboard C-API from cached VIDs
  (`read_bbvari_convert_double(vid)` etc.). No observer/signal machinery for the display path.
- **`UpdateScheduler`** replaces the 3 QTimers: wall-clock accumulators call `OnUpdateTick` on Fast windows
  every frame, Medium every N ms, Slow every M ms. `RenderContent` always draws from cached values → render
  at display FPS, sample at configured rate.
- Blackboard C-API is internally synchronized; reads from the render thread need no new locking (same as Qt).

## 4. Windowing (replaces QMdiArea + Sheets)

- One GLFW window, `ImGui::DockSpaceOverViewport`, main menu bar, status child, Control Panel as a docked
  window.
- **Disable ImGui auto-ini**: `ImGui::GetIO().IniFilename = nullptr;`. Own the layout: per sheet, store the
  dock layout via `SaveIniSettingsToMemory`/`LoadIniSettingsFromMemory` keyed by sheet name in
  `layout.toml`; use `DockBuilder*` for first-time layout. On sheet switch: save current, load target.
- **Window identity**: `"VisibleName###StableId"` so renames don't break docking. Keep a
  `std::unordered_map<std::string, IWindowInstance*>` name→instance map to preserve
  `OpenWindowByName`/`CloseWindowByName`/`WindowNameAlreadyInUse`.
- Drag a window out of the dock → floats as a real OS window (multi-viewport) for free.

## 5. Dialogs — do NOT hand-port 1:1

~80 dialogs / 101 `.ui` files. Kill the `.ui`/moc/model-view layer entirely.
- **Data-driven forms**: a `FormField` descriptor (label, kind = Int/Double/Text/Bool/Enum/FilePath,
  value pointer, min/max/tooltip) + `RenderForm(std::span<FormField>)`. Most dialogs (CAN/XCP/CCP, process,
  scheduler config) become field lists.
- **Reusable components once**: `DrawBlackboardSelector(...)`, `DrawPhysValueInput(...)`, file picker.
- **Modal** → `OpenPopup` + `BeginPopupModal`. **Modeless** (script debugging, message window, progress)
  → normal `Begin` windows tracked in a vector.
- Script-driven dialogs (`ScriptCreateDialog`/`…AddDialogItem`/`…ShowDialog`) append `FormField`s, then a
  visible-flag flip drained by the command queue.
- Genuinely hand-built (< 15): A2LCalMap config, Oscilloscope config, CalibrationTree.

## 6. Custom rendering recipes

| Widget | Approach |
| :-- | :-- |
| Oscilloscope | **ImPlot** line series fed from the existing C ring buffer. Keep `OscilloscopeCyclic.c` as-is (scheduler-thread sampling); read it per frame. |
| Knob / Tacho | `ImDrawList` in content rect: `AddCircle`, `PathArcTo`, `AddLine` (needle), `AddText` (ticks). Interaction via `InvisibleButton` + drag delta → `write_bbvari_*`. |
| Bargraph / Slider / Lamps / Enum / Text | Bargraph = filled rect / `ProgressBar`; Slider = `SliderScalar` + write-back; Lamps = `ImDrawList` colored shapes by value range; Enum = combo/colored label; Text = `BeginTable` name/value/unit. |
| A2LCalMap 3D | Port existing projection math to `ImDrawList::AddTriangleFilled` (per-vertex color). FBO-texture render as later optimization. 2D map/table editor = editable `BeginTable`. |
| UserDraw / UserControl | Thin `ICanvas` abstraction (~30 primitives) over `ImDrawList`; retarget the script interpreter's draw calls onto `ICanvas` instead of rewriting it. UserControl adds hit-testing via per-primitive rects + `IsItemHovered`/clicks. |
| CalibrationTree / CANMessageWindow | `BeginTable` with `ImGuiTreeNodeFlags` expandable rows / sortable columns. |

Give every custom painter a shared `DrawContext` (content rect, DPI scale, theme colors). Theming = one
`ImGuiStyle` + colors struct loaded from `layout.toml` (replaces `DarkMode.cpp`/stylesheets).

## 7. Drag & drop of blackboard variables

ImGui payload ≠ `QDropEvent`. Source: `BeginDragDropSource` + `SetDragDropPayload("BBVAR", name, len)`.
Target: `BeginDragDropTarget` + `AcceptDragDropPayload("BBVAR")` → `OnAcceptVariableDrop(name)`.
**Re-test per widget** — this is a known risk area.

## 8. Config / layout persistence (TOML)

- Two files: `<project>.engine.toml` (engine/model config, shared by all exes) and
  `<project>.layout.toml` (GUI state, ImGui only).
- The `Src/IniFileDataBase/IniDataBase.h` façade **stays as the interface**; a TOML-backed implementation
  sits behind it (`section` → TOML table, `entry` → key). No call-site churn. Engine vs layout = which
  descriptor is opened. Byte-image blobs → base64.
- Hierarchical GUI state (dock blobs, window vectors) uses toml++ directly via a typed `LayoutConfig`
  (`WriteConfig`/`ReadConfig` on each `IWindowInstance`).

## Porting checklist (per widget/dialog)

1. Identify the Qt source under `Src/GUI/Qt/Widgets/<X>/` or `Dialog/`.
2. For a window type: implement `IWindowType` + `IWindowInstance`; register in `RegisterBuiltinWindowTypes`.
3. Map painting to `ImDrawList`/ImPlot/`BeginTable`; map config to `WriteConfig`/`ReadConfig` (layout.toml).
4. Wire live values (cached VIDs + `OnUpdateTick`), drag&drop, settings popup.
5. Run it **side by side** with the Qt window against the same running sample simulation; verify values,
   update rate, config round-trip. Then hand to the `parity-reviewer` agent.
