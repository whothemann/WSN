\mainpage WAID Firmware

Welcome to documentation of our WSNLab Project WAID (group6, Anne Dröge, Leopold Lüthe, Lars Husemann). This project implements a lightweight RPL stack with upward/downward messaging and sensor integration on Contiki-NG. Use this page as a starting point to navigate the codebase.

## Overview
- **Protocols**: RPL (DIO/DIS/DAO), custom MSG_UP/MSG_DOWN, Trickle timer control
- **Platforms**: Nordic nRF52840 (Contiki-NG)
- **Roles**: ROOT, AP, NODE (bed), NURSE
- **Sensors**: Button, buzzer, pulse sensor for BPM

## Key Components
- `firsttry.c`: Entry point, RX dispatcher, trickle scheduling
- `dio.c` / `dis.c` / `dao.c`: RPL control messages
- `msg_up.c` / `msg_down.c`: Application-layer up/down messages
- `external_sensors.c`: Button/buzzer/pulse handling and heartbeat reporting
- `rpl_route.c` / `rpl_state.c`: Routing table and node state management
- `trickle.c`: Trickle timer implementation for DIO pacing

## Building & Running
1. Configure your target and toolchain for Contiki-NG.
2. Build the project: `make` (or your platform-specific build target).
3. Flash the firmware to the nRF52840 DK and open a serial console for logs.

## Generating Docs
Run Doxygen from the project root:
```
doxygen
```