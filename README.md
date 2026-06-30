# BDO Ping Monitor Plugin for MSI Afterburner

A high-performance, passive network latency (RTT) monitoring plugin for MSI Afterburner, specifically optimized for **Black Desert Online (BDO)**.

## Overview

Unlike traditional ping monitors that send active ICMP echo requests, this plugin uses **Event Tracing for Windows (ETW)** to passively capture RTT data directly from the Windows TCP/IP stack. This approach provides several advantages:
- **Anti-Cheat Safe**: No active network requests or process memory reading.
- **Accurate**: Reflects the actual latency experienced by the game's TCP connections.
- **Low Overhead**: Minimal CPU usage by leveraging native Windows kernel events.

## Features

- **Passive Monitoring**: Captures RTT from `Microsoft-Windows-TCPIP` events.
- **Peak Hold Strategy**: Captures the maximum RTT spike between polling intervals, ensuring lag spikes are never missed in your MSI Afterburner graphs.
- **Activity-Based Tracking**: Automatically identifies and follows the primary game connection based on packet activity.
- **Auto-Recovery**: Automatically detects game restarts and re-attaches to the new process.
- **MSI Afterburner Integration**: Displays as a native "Ping" source in the Monitoring tab and On-Screen Display (OSD).

## Installation

1. Download the latest `BDO_Ping_x86.dll` from the [Releases](https://github.com/getfullz/BDO-PING-DLL/releases) page.
2. Copy the DLL to your MSI Afterburner plugins directory:
   `C:\Program Files (x86)\MSI Afterburner\Plugins\Monitoring\`
3. Restart MSI Afterburner and go to **Settings -> Monitoring**.
4. Next to **Active hardware monitoring graphs**, click the button with three dots (`...`).
5. In the opened **Hardware monitoring plugins** window, find and check **BDO_Ping_x86.dll** under **Active plugin modules**, then click **OK**.
6. Now find **BDO Ping (RTT)** (or **Ping**) in the active hardware monitoring graphs list, enable it, and configure its OSD display.

## Building from Source

### Prerequisites
- Windows 10/11
- Visual Studio 2022 (with C++ Desktop Development workload)
- CMake 3.15+

### Build Steps
1. Clone the repository:
   ```bash
   git clone https://github.com/getfullz/BDO-PING-DLL.git
   cd BDO-PING-DLL
   ```
2. Create a build directory and configure:
   ```bash
   mkdir build
   cd build
   cmake .. -A Win32
   ```
   *Note: MSI Afterburner is a 32-bit application, so the plugin MUST be compiled as x86 (`-A Win32`).*
3. Build the project:
   ```bash
   cmake --build . --config Release
   ```

## Technical Details

The monitor filters for `BlackDesert64.exe` and tracks TCP activity. It specifically looks for connections on the game's ports and uses the TCB (Transmission Control Block) identifier to correlate TCP events with the correct connection.

The "Peak Hold" logic ensures that if your ping spikes to 200ms for even a single packet between Afterburner's 1000ms polling intervals, that 200ms value is captured and reported, rather than being averaged out or lost.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
