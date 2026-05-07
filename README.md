# Quick Start "Control" Example

A simple DDS controller/target example using
RTI Connext DDS 7.x, implemented in both C++ and Python.

A **controller** discovers targets, sends commands
(start, stop, pause), and monitors alerts.
A **target** receives commands, simulates movement
toward a goal pose, and publishes
state/position/alerts.

## Prerequisites

- RTI Connext 7.3+
- CMake 3.17+
- gcc 8.5.0+
- Python 3.10+ with packages:
  - rti.connext>=7.3

## Environment

Create and source python virtual environment:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Source RTI Connext environment:

```bash
source /opt/rti.com/rti_connext_dds-7.7.0/resource/scripts/rtisetenv_x64Linux4gcc8.5.0.bash
```

## Build

```bash
cmake -B build && cmake --build build
```

## Run

*C++ and Python apps are interoperable — you can*
*mix them (e.g. C++ target with Python controller).*

*Run apps from the project root dir to load `USER_QOS_PROFILES.xml` by default.*

### C++

Open two terminals from the project root:

*You may need to source `rtisetenv_<arch>` script before running to load*
*RTI Connext shared libraries on `LD_LIBRARY_PATH`.*

```bash
# Terminal 1 — start a target
./build/src/cpp/target --uid target-1

# Terminal 2 — start the controller
./build/src/cpp/controller --priority 5
```

### Python

Open two terminals from the project root:

*You will need to activate the python virtual environment before running.*

```bash
# Terminal 1 — start a target
python src/py/target.py --uid target-1

# Terminal 2 — start the controller
python src/py/controller.py --priority 5
```

## Controller Commands

Once the controller is running, type commands
at the prompt:

| Command | Description |
|---|---|
| `list` | Show discovered targets |
| `positions` | Show current positions |
| `send <target> <action> [x y z]` | Send command |
| `send all <action> [x y z]` | Command all targets |
| `priority <value>` | Change controller priority |
| `help` | Show available commands |
| `quit` | Exit |

Actions: `start`, `stop`, `pause`.

## CLI Options

Target:

| Option | Default | Description |
|---|---|---|
| `--uid <id>` | `controller-1` / `target-1` | ID |
| `--domain <id>` | `0` | DDS domain ID |

Controller:

| Option | Default | Description |
|---|---|---|
| `--priority <id>` | `0` | Controller (command) priority |
| `--domain <id>` | `0` | DDS domain ID |

## Project Structure

```none
CMakeLists.txt
src/
  interface
    /control
        Control.idl      # DDS type definitions
  cpp/
    controller.cpp       # C++ controller
    target.cpp           # C++ target
  py/
    controller.py        # Python controller
    target.py            # Python target
```
