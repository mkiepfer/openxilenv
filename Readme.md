# OpenXilEnv

OpenXilEnv is a lightweight SiL/HiL environment. At the moment, OpenXilEnv focuses on SiL environments.

## SiL

With a **S**oftware **I**n the **L**oop system, it is possible to run and test embedded software without a target platform or compiler.
XilEnv provides an environment to set up a SiL system on Windows or Linux hosts. It clearly separates its own components from the embedded software under test through a network layer. Each component can run in its own executable, providing memory protection between them.

Communication between the software under test, models, and XilEnv is done via sockets or (on Windows) named pipes / (on Linux) local sockets. These are used for:
- Signal transfers
- Virtual network (CAN and CAN FD) transfers

![XilEnv SiL system](OpenXilEnv_Sil.png)

OpenXilEnv ships **three interchangeable front-ends** over the same simulation core:

**XilEnvImGui** is the **primary** graphical user interface, built on Dear ImGui (GLFW + OpenGL). It includes display/change widgets, oscilloscopes, calibration tools, sliders, knobs, and other interaction elements, with a dockable window/workspace layout.

**XilEnvGui** is the legacy Qt-based GUI. It offers the same widget set and is retained in parallel for side-by-side verification during the ongoing Qt→ImGui migration.

**XilEnv** (CLI) has no graphical user interface and can be used for automation without user interaction.

All three share the `xilenv_core` library and read the same configuration files, so a project can be opened in any of them.

Main features:
- Supports current GCC compilers with DWARF debug information. Visual Studio compiler is supported but without debug information
- FMUs with FMI 2.0 interface are supported via **ExtProc_FMU2Extract(.exe)**, **ExtProc_FMU2Loader32(.exe)**, and **ExtProc_FMU2Loader64(.exe)**
- Partial FMI 3.0 support (additional datatypes only) via **ExtProc_FMU3Extract(.exe)**, **ExtProc_FMU3Loader32(.exe)**, and **ExtProc_FMU3Loader64(.exe)**
- Includes a small A2L parser for calibration
- Supports XCP over Ethernet for connection to a calibration system
- Multicore support with synchronization barriers
- Automatable via **XilEnvRpc.dll/.so** or the built-in script interpreter
- Supports 32- and 64-bit executables (mixed allowed). XilEnv itself must be 64-bit
- Time simulation with fixed, configurable period and 1 ns resolution
- Residual bus simulation for missing CAN (FD) members
- Recording and stimulation supported via text or MDF3 files

The DLL/shared library **XilEnvExtProc64.dll/.so** or **XilEnvExtProc32.dll/.so** provides the interface for the embedded test software or model. This DLL/shared library must be loaded dynamically. The main module **XilEnvExtProcMain.c** handles this automatically.

Interface functions are declared in **XilEnvRtProc.h**.

An example can be found in `Samples/ExternalProcesses/ExtProc_Simple`.

## Getting Started

1. Install dependencies ([Windows](docs/WINDOWS_DEPENDENCIES.md) | [Linux](docs/LINUX_DEPENDENCIES.md))
2. Build the project ([Windows](docs/WINDOWS_BUILD.md) | [Linux](docs/LINUX_BUILD.md))

## Architecture

For an overview of the software components and how the **XilEnvImGui**, **XilEnvGui** and **XilEnv** executables are built up from the shared `xilenv_core` library and the modules under `Src/`, including component diagrams and the GUI/core seam, see the [Architecture documentation](docs/ARCHITECTURE.md).

## Installation

The build installs **XilEnvImGui**, **XilEnvGui** and **XilEnv** (CLI) together
with all associated shared libraries (`XilEnvRpc`, `XilEnvExtProc64`, …), the
external process headers and the user guide into a single directory. The target
directory is controlled by `CMAKE_INSTALL_PREFIX`, so you can install
everything into a local, user-writable location such as `~/.local` without root
privileges.

### Linux

Configure the build with a local install prefix, build, and install:

```bash
# Configure with a local install prefix (no root required)
cmake -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local/openxilenv" -S . -B build

# Build
cmake --build build --parallel $(nproc)

# Install XilEnvImGui, XilEnvGui, XilEnv and all shared libraries into the prefix
cmake --install build
```

The executables and shared libraries are placed flat inside the prefix
(e.g. `~/.local/openxilenv/XilEnvImGui`, `~/.local/openxilenv/XilEnvGui`,
`~/.local/openxilenv/XilEnv`, `~/.local/openxilenv/XilEnvRpc.so`, …).

To run the installed binaries from anywhere, add the install directory to your
`PATH` and `LD_LIBRARY_PATH`:

```bash
export PATH="$HOME/.local/openxilenv:$PATH"
export LD_LIBRARY_PATH="$HOME/.local/openxilenv:$LD_LIBRARY_PATH"
```

To make this permanent, append the two lines above to your `~/.bashrc`.

> **Note:** The install layout is flat (all executables and libraries live
> directly in the prefix, there is no `bin/` or `lib/` split). Using a dedicated
> subdirectory such as `~/.local/openxilenv` therefore keeps `~/.local` tidy and
> is the recommended approach.

### Windows

```cmd
cmake -G Ninja -DCMAKE_INSTALL_PREFIX="%LOCALAPPDATA%\openxilenv" -S . -B build
cmake --build build
cmake --install build
```

Add `%LOCALAPPDATA%\openxilenv` to your `PATH` to run the tools from anywhere.

For more build options (FMU support, examples, 32-bit libraries, …) see the
platform build guides: [Windows](docs/WINDOWS_BUILD.md) | [Linux](docs/LINUX_BUILD.md).

## Setting up Your Project

- [Setup an External Process](docs/EXTERNAL_PROCESS_SETUP.md)

## HiL Option

To use OpenXilEnv as a HiL system, a second Linux PC is required. Only **SocketCAN FD** interfaces are supported.

For <1 ms response times, install the **RT-Preempt** patch.

OpenXilEnv is split into:
- **LinuxRemoteMasterCore.so** – real-time (Linux with RT-Preempt)
- **XilEnv executable** – non–real-time (Windows or Linux)

Use a direct Ethernet connection between PCs.

If a model is needed, compile it for Linux and link with **LinuxRemoteMasterCore.so**.  
If not, use **LinuxRemoteMaster.out**.

Ensure **RemoteStartServer** is installed and running on the Linux PC to allow remote execution.

## Python API

OpenXilEnv is distributed with a python bindings wrapper that allows full control over a runninx openXilEnv instance via python.

The [functional tests](test/functional/) of openXilEnv are implemented in [pytest](https://docs.pytest.org/en/stable/) using the openXilEnv **python API** and provide an example usage.

For a detailed description take a look at the [openXilenv python API refrence](docs/PYTHON_API_REFERENCE.md).

## Test

OpenXilEnv has two test layers:

- **C-core unit tests** (GoogleTest / CTest, target `xilenv_core_tests`) under
  [`test/unit/`](test/unit/) cover the shared core (Blackboard, the ini/TOML
  configuration façade, and equation / A2L / CAN helpers). Run them from the
  build directory with `ctest --output-on-failure`.
- **Functional tests** (pytest) validate a full **openXilEnv** installation
  through the Python API.

OpenXilEnv provides functionality tests, that validate the **openXilEnv** installation using pytest.

The tests depend on the **openXilEnv Installation** path and the **openXilEnv Source** path.
To make sure the paths are available, set the following variables

on windows
```cmd
set OPENXILENV_INSTALLATION_PATH="%SystemDrive%\dev\tools"
set OPENXILENV_SOURCE_PATH="%SystemDrive%\dev\src"
```

on linux
```bash
export OPENXILENV_SOURCE_PATH="$HOME/dev/src/openXilEnv"
export OPENXILENV_INSTALLATION_PATH="$HOME/dev/tools/openXilEnv"
```

To run all tests, simply execute

```bash
pytest -vv
```

## License
This project is part of the Eclipse Foundation and licensed under the [Apache License 2.0](LICENSE.txt).

## Contributing
Contributions are welcome! Please see the [CONTRIBUTING.md](CONTRIBUTING.md) file for details.