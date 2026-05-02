# WAid — Wireless Nurse Call System

A wireless sensor network for hospital nurse-call and patient monitoring, built on **Contiki-NG** for the **Nordic nRF52840 DK**. The firmware implements a lightweight RPL routing stack from scratch, reliable application-layer messaging with end-to-end acknowledgments, and a Qt/C++ desktop GUI that visualizes the live DODAG topology and patient status.

Developed as the WSN Lab project at TU Munich (Group 6) by **Anne Dröge**, **Leopold Lüthe**, and **Lars Husemann**.

---

## Motivation

Staff shortages in healthcare make timely response to patient requests increasingly difficult. Wired call systems are rigid, expensive to install, and hard to scale. WAid is a wireless alternative that lets every bed in a facility submit prioritized requests (emergency, toilet, water), continuously monitors patient heart rate, and routes the highest-priority task to the mobile nurse device — all over a self-organizing low-power mesh.

---

## System Overview

The network is structured as an RPL DODAG with four node roles:

| Role | Function |
|------|----------|
| **Root (Gateway)** | Initializes the network, owns the DODAG, maintains downward routing state (Non-Storing mode), prioritizes emergencies, dispatches tasks to the nurse device, and forwards data to the GUI over a serial link. |
| **Access Point** | Forwards DIO messages and relays application traffic to extend coverage beyond the root's radio range. |
| **Bed Node** | Patient interface: four buttons (emergency / toilet / water / reset) plus a pulse sensor reporting BPM heartbeats. |
| **Mobile Nurse** | Highly mobile staff device with a buzzer that signals the next task to attend to. |

Roles are selected at compile time via the `MY_ROLE` macro (`ROLE_ROOT`, `ROLE_AP`, `ROLE_NODE`, `ROLE_NURSE`) — the same firmware is flashed to every board, with the role chosen at build time.

---

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                     Application Layer                       │
│   Buttons · Pulse sensor (BPM) · Buzzer · Emergency table   │
│              MSG_UP (events, heartbeats, ACKs)              │
│              MSG_DOWN (task dispatch to nurse)              │
├────────────────────────────────────────────────────────────┤
│                       RPL Routing                           │
│   DIO · DIS · DAO  ·  Trickle timer  ·  Parent selection    │
│        Non-Storing mode · Source-routed downward paths      │
├────────────────────────────────────────────────────────────┤
│              Contiki-NG · NullNet · CSMA · 802.15.4         │
│                  Nordic nRF52840 DK (2.4 GHz)               │
└────────────────────────────────────────────────────────────┘
```

---

## Implemented Features

### Custom RPL stack (RFC 6550, lightweight)
- **DIO** — DODAG construction, broadcast by Root, relayed by Access Points.
- **DIS** — Topology re-join when all parents become unreachable.
- **DAO** — Periodic upward parent advertisement; Root assembles the global routing table from DAO chains.
- **Trickle timer** — Adaptive DIO pacing: short interval `Imin` after topology change, exponential doubling up to `Imax = Imin · 2^d` during steady state, with a uniform random transmission point in the second half of each interval to avoid synchronized collisions.

### Composite-score parent selection with hysteresis
Each candidate parent is scored from two normalized metrics on `[0, 100]`:

- **Rank score** — Smaller advertised rank → higher score.
- **RSSI score** — Smoothed via an exponential moving average (`α = 2^-s`, role-dependent step), then linearly mapped between `RSSI_MIN_DBM` and `RSSI_MAX_DBM`.

The composite score is a weighted sum (default: 5% rank, 95% RSSI). A parent switch only occurs if a candidate beats the current preferred parent by a hysteresis margin `H`, configurable separately for stationary nodes (10 dB) and the mobile nurse (4 dB) so the nurse adapts faster while bed nodes stay stable. A second-best parent is kept hot in the candidate list for instant fail-over without sending a DIS.

### Reliable application-layer messaging
- **MSG_UP** — Bed-node events and periodic heartbeats are forwarded hop-by-hop along preferred parents until they reach the Root. ACK-with-retransmit (`MSG_UP_ACK_TIMEOUT`, bounded retries). When no parent is currently reachable, messages are buffered in a bounded FIFO queue and flushed on reconnection.
- **MSG_DOWN** — Root-to-nurse control messages. Because intermediate nodes do not hold downward routing state (Non-Storing mode), the Root encodes an explicit hop sequence in the payload; each relay forwards to the next listed hop. ACKs travel back upward via preferred parents.

### Emergency prioritization at the Root
The Root maintains an ordered table of active cases sorted by priority (button type, BPM out of bounds, etc.) and dispatches the top one to the nurse via `MSG_DOWN`. BPM thresholds automatically generate emergencies even without a button press.

### Sensor integration on Bed Nodes
- **Pulse sensor** — Sampled at 500 Hz on a SAADC channel, filtered, peak-detected to derive BPM, transmitted every 11 s. Falls back to a fake-BPM mode if no real signal is detected for 10 s (useful for demos and bench testing).
- **Buttons** — Polled at 50 Hz with debouncing.
- **Buzzer** — Driven on the nurse device whenever the highest-priority task changes.

### Qt/C++ Desktop GUI (`gui/Waid/`)
- USB-serial selection and connection control.
- Live, scrolling, read-only log of all firmware output.
- Force-directed topology view (`QGraphicsScene` / `QGraphicsView`): `NodeItem` for devices, `EdgeItem` for parent links. The Root is anchored, Access Points align by name, child nodes settle via spring forces and stay put when no topology change occurs.
- Color coding: gateway (gray), access points (green), beds (orange), nurse (red).
- Per-bed status tiles `BED1`…`BED4` show real-time BPM, with the tile background reflecting current emergency priority.
- Parses a simple line-based protocol from the Root over serial: `HEARTBEAT BEDx BPM: y`, `TOPO_BEGIN` … `LINK <a> <b>` … `TOPO_END`, `EMERG_BEGIN` … `EMERG_END`.

---

## Repository Layout

```
.
├── firsttry.c              # Application entry point, RX dispatcher, role logic
├── rpl_config.h            # All tunable RPL/timing/scoring constants
├── rpl_net.h / rpl_msg.*   # NullNet TX wrapper and common message helpers
├── rpl_state.*             # Per-node RPL state (rank, parent list, version)
├── rpl_route.*             # Root-side global routing table + path lookup
├── trickle.*               # Trickle timer for DIO pacing
├── dio.* / dis.* / dao.*   # RPL control message handlers
├── msg_up.* / msg_down.*   # Application-layer reliable messaging
├── external_sensors.*      # Buttons, buzzer, pulse sensor / BPM
│
├── gui/Waid/               # Qt6 (C++17) desktop GUI
│   ├── mainwindow.*        # Window, serial parsing, log
│   ├── topologyview.*      # QGraphicsView force-directed layout
│   ├── nodeitem.*          # Drawable network nodes
│   ├── edgeitem.*          # Drawable links
│   ├── devicetype.h        # Role-string parsing helpers
│   └── Waid.pro            # qmake project file (uses qextserialport)
│
├── Makefile                # Contiki-NG build (target: firsttry)
├── Doxyfile                # Doxygen configuration
├── mainpage.md             # Doxygen landing page
└── WSN_WAid_project_report.pdf   # Full project report
```

---

## Build & Flash (Firmware)

**Prerequisites**
- [Contiki-NG](https://github.com/contiki-ng/contiki-ng) checked out at `~/contiki-ng` (or update `CONTIKI=` in the `Makefile`).
- ARM GCC toolchain (`arm-none-eabi-gcc`) and the Nordic nRF52840 build dependencies as per the Contiki-NG documentation.
- Nordic nRF52840 DK board.

**Build**

The role of each board is set at compile time through `MY_ROLE`. A typical build flashes one Root, two or more Access Points, several Bed Nodes, and one Nurse:

```bash
# Root
make TARGET=nrf52840 BOARD=dk DEFINES=MY_ROLE=1

# Access Point
make TARGET=nrf52840 BOARD=dk DEFINES=MY_ROLE=2

# Bed Node
make TARGET=nrf52840 BOARD=dk DEFINES=MY_ROLE=3

# Nurse
make TARGET=nrf52840 BOARD=dk DEFINES=MY_ROLE=4
```

Adjust the radio channel and TX power in `rpl_config.h` (`RF_CHANNEL`, `RF_TXPOWER`) if needed.

**Flash & monitor**

Flash the produced `firsttry.nrf52840` image with the standard Contiki-NG `make … TARGET=nrf52840 PORT=/dev/ttyACM0 firsttry.upload` flow, then open a serial console at the Contiki-NG default baud rate to see logs.

---

## Build & Run (GUI)

The GUI is a standard Qt 6 / C++17 application that depends on `qextserialport`:

```bash
cd gui/Waid
qmake Waid.pro
make
./Waid
```

Connect the Root board via USB, select its serial port from the drop-down in the left pane, and the topology view and bed status tiles will populate as DAO and HEARTBEAT messages flow in.

---

## Configuration Reference

All protocol-level tunables live in `rpl_config.h`. The most useful knobs:

| Constant | Default | Purpose |
|----------|---------|---------|
| `DIO_TRICKLE_IMIN` | 0.5 s | Initial Trickle interval for DIO. |
| `DIO_TRICKLE_DOUBLINGS` | 4 | Caps `Imax` at `Imin · 2^4`. |
| `DAO_PERIOD` | 2 s | Upward parent-advertisement period. |
| `MSG_UP_ACK_TIMEOUT` | 3 s | Application-layer ACK timeout. |
| `PARENT_TIMEOUT` | 16 s | Drop a candidate parent if no DIO in this window. |
| `RANK_WEIGHT` / `RSSI_WEIGHT` | 5 / 95 | Composite parent-score weighting. |
| `RSSI_HYST_NODE` / `RSSI_HYST_NURSE` | 10 dB / 4 dB | Parent-switch hysteresis (lower for the mobile nurse). |
| `MAX_PARENT_CANDIDATES` | 2 | Hot-standby parents tracked per node. |
| `RF_CHANNEL` | 26 | IEEE 802.15.4 channel. |

---

## Documentation

- **Project report** — `WSN_WAid_project_report.pdf` covers the application, the routing protocol, the objective function, Trickle dissemination, both messaging layers, and the GUI in detail.
- **Source-level docs** — Generate Doxygen output with:
  ```bash
  doxygen Doxyfile
  ```
  Output appears under `html/`. `mainpage.md` is used as the landing page; class/file relation graphs are rendered with Graphviz (`HAVE_DOT = YES`).

---

## Authors

WSN Lab Group 6, Technische Universität München:

- Anne Dröge
- Leopold Lüthe
- Lars Husemann

## References

1. Alexander et al., *RPL: IPv6 Routing Protocol for Low-Power and Lossy Networks*, RFC 6550, March 2012.
2. T. Tsvetkov, *RPL: IPv6 Routing Protocol for Low Power and Lossy Networks*, 2011.
