

###################################### from here read the full project description

# Submarine Monitoring System — Final Project

Source document: `SW-FD-LNC-001 | Software Functional Definition`.
This file is the working spec for Claude Code. If code and this file disagree, ask before changing either.

---

## 1. What we are building

Three programs plus one C++ exercise:

| Part | Name | Language | Runs on |
|---|---|---|---|
| A | LNC End Unit (firmware) | C | STM32 Nucleo-L476RG |
| B | Central Computer | C / C++ | Linux (WSL Ubuntu) |
| C | Ground Station — includes the Submarine Fleet Management (OOP part) | C++ | Linux (WSL Ubuntu) |

Three programs, one system. The "OOP part" of the document is **not** a separate exercise —
it is the internal design of the Ground Station. The Ground Station is the thing that manages
several submarines, so the fleet, the missions, and the messages live there.

### Data flow

```
Ground Station  <--Ethernet-->  Central Computer  <--UART / Ethernet-->  LNC End Unit
                                        |
                                        +--> Motor Unit (other end unit, not in scope)
                                        +--> Navigation Unit (other end unit, not in scope)
```

- The LNC is one end unit inside the submarine.
- The Central Computer manages many end units and decides how the mission continues.
- The Ground Station asks the Central Computer for stored data and events by time range.
- All messages in the system use **TLV** (Tag, Length, Value).
- The Ground Station must be able to manage **several types of submarines**, not only this one.

### Transport rule (important)

UART vs Ethernet is a **config detail inside the communication module only**.
No other module may know which transport is used. Every other module talks to a
transport-independent interface. Keep this rule when writing code — it is stated
twice in the spec, so it is likely a grading point.

### Planned: Ethernet-simulation gateway (not built yet)

Decided but deferred — build this when asked, don't start unprompted:

```
STM32 (real UART) <--UART-->  gateway (new, separate process)  <--TCP, localhost-->  Central Computer
```

The board only ever has UART; there is no real Ethernet hardware. To still
exercise the "Ethernet" transport path end-to-end, a small standalone gateway
process will own the real serial port (reusing `SerialPort` from
`CentralComputer/src/uartTransport/serial.h`/`.cpp` — no new UART code) and
expose it as a plain TCP socket on localhost. It is a dumb byte pipe: no TLV
parsing, no framing, both directions.

The Central Computer's Communication module then only ever opens a TCP client
socket — it never includes `termios.h` or touches a serial device directly.
As far as its code is concerned, it is talking Ethernet to the LNC, which
satisfies the transport rule above literally, not just as a simulation. Plain
TCP over loopback is the deliberate choice over a Unix-domain socket or pipe,
specifically so this is the same BSD-sockets code the real GroundStation ↔
CentralComputer Ethernet link needs later — pointing at a different address
is the only change.

The gateway is TLV-agnostic: it never calls `tlv_encode()` or
`tlv_receiver_feed_byte()`, and has no dependency on `Shared/ProtocolTLV`.
Encode/decode happens only in the Communication module on each real endpoint
— the LNC firmware and the Central Computer — regardless of which transport
(UART or the gateway's TCP socket) sits underneath it. The bytes the gateway
forwards are the already-TLV-encoded frame, unchanged end to end.

---

## 2. Part A — LNC End Unit

Nine modules: Monitor, Object Detection, Event, Log, Communication, Configuration, Init,
Keep-Alive, Watchdog.

### 2.1 Monitor

- Every **5 seconds**: sample temperature, humidity, battery voltage (potentiometer), light.
- Compare each value against its configured limits.
- Send measured data + resulting mode to **Log**.
- On a **mode change only**, send a message with the measured values to **Event**.
- Limits are configurable and stored in **Flash**.

### 2.2 Object Detection (sonar)

- Listens continuously for an object in the direction of travel.
- Object found → message to **Event**.
- Object no longer found → message to **Event**.

### 2.3 Event

Waits for events. **Timestamps every event on arrival.** Behaviour depends on the source.

**From Monitor (mode transitions):**

| Transition | LED | Alarm | Other |
|---|---|---|---|
| Normal → Warning | yellow | — | — |
| Error → Warning | yellow | stop if active | resume full operation |
| any → Error | red | start | button press stops alarm; suppress non-essential ops |
| Warning → Normal | green | — | — |
| Error → Normal | green | stop if active | resume full operation |

In **every** Monitor case: write a timestamped message to the events file **and** send a
message to the Central Computer.

**From Configuration:** write a timestamped message to the events file. (No message to the
Central Computer.)

**From Init:** write a startup message to the events file, including whether this boot came
after a **watchdog reset**.

**From Object Detection:**

| Case | LED | Alarm | Events file | To Central Computer |
|---|---|---|---|---|
| Object detected | red | start (button stops it) | yes | yes |
| Object cleared | green | stop if active | yes | yes |

### 2.4 Log

- Builds a log line: timestamp + measurement data + mode.
- Writes to a file named by date (one file per day).
- Keeps **7 days / 7 files**. On day 8, delete the oldest file.

### 2.5 Communication

- Owns the UART interface (and Ethernet later).
- Sends and receives to/from the Central Computer.
- **Management commands received:**
  - temperature range for Normal
  - temperature range for Warning
  - humidity lower bound for Normal
  - humidity lower bound for Warning
  - light lower bound for Normal
  - light lower bound for Warning
  - battery (potentiometer) lower bound for Normal
  - battery (potentiometer) lower bound for Warning
  - set RTC date and time
  - get current system time
- **Instructions received:**
  - get measurement data for a time range
  - get events for a time range
- **Send priority:** keep-alive (high) > events (medium) > data reports (low).

### 2.6 Configuration

- Receives config changes from Communication, owns all config values.
- Persists them in Flash.
- Loads from Flash at startup. If Flash is empty (first boot), load defaults and write them
  to Flash.
- A config change also produces an Event (see 2.3).

### 2.7 Init

- Asks the Central Computer for date/time sync through Communication.
- After sync completes, sends a message to Event.
- Starts all system activities.

### 2.8 Keep-Alive

- Every **6 seconds**, sends a keep-alive to the Central Computer through Communication.
- Contains: timestamp + latest measurement data + current mode.

### 2.9 Watchdog

- Refreshes the hardware WD timer on schedule so no reset happens.
- Init must be able to report whether the last boot was caused by a WD reset.

### 2.10 Operating modes

| Mode | Condition |
|---|---|
| Normal | all measurements in Normal range |
| Warning | at least one in Warning range, none in Error range |
| Error | at least one in Error range |

---

## 3. Part B — Central Computer

Four modules.

1. **Communication (LNC-facing)** — protocol send/receive, plus a listener that routes
   incoming messages to the right module. Transport swappable, same rule as the LNC.
2. **Management Command** — builds and sends management commands to the LNC; handles
   command processing.
3. **Log** — prints logs and writes them to files.
4. **Data Collection & Analysis** — stores measurement data and events in a database,
   and builds reports broken down by different criteria.

Also keeps only **one week** of data, same as the LNC.

---

## 4. Part C — Ground Station (with Fleet Management)

The Ground Station has two sides that are really one program:

**Communication side**
- Talks to a submarine's Central Computer over Ethernet.
- Sends commands.
- Requests stored **log data** and **event data** for a given date/time range.
- Must be able to handle several submarine types.

**Fleet management side (the OOP part)**
- Manages the fleet: submarines, their missions, and messages between them.
- This is where the class hierarchy below lives.

The link between the two sides: a `CombatSubmarine` owns a `CentralComputer` object, and that
object is the same Central Computer from Part B. So "ask submarine 4213 for yesterday's
events" becomes: find the submarine in the fleet → go through its Central Computer → get the
data. The menu is the user interface of the Ground Station.

### Classes

**Submarine (base)**
- serial number
- name
- assigned to a mission / available
- received messages: each message keeps its content **and a reference to the sending
  submarine**

**ResearchSubmarine : Submarine**
- names of researchers on board
- current research topic

**CombatSubmarine : Submarine**
- current mission description
- commander name
- number of combat personnel for the mission
- **has a CentralComputer object** (composition — the Central Computer from Part A/B)
- other combat submarines in the same mission
- all the missions its fleet was managing

### Menu

1. Add a submarine (choose type, enter details)
2. Show all submarines + details + mission status
3. Find and show a submarine by serial number
4. Assign a mission (and mark the submarine as busy)
5. Update mission details (depends on type)
6. End a mission (mark as available again)
7. Combat only: link other combat submarines to the same mission
8. Send a message from one combat submarine to another in the same mission
9. Show messages a submarine received, with sender
10. Exit

### Course rules to follow (Keren Kalif style)

- A manager class holds all the data.
- No input inside classes — `main` reads input and passes parameters.
- `set` methods return `bool`.
- Strings allocated to the exact size; non-string arrays grow x2 with logical/physical size.
- Later stages: STL (`string`, `vector`) and smart pointers.

---

## 5. Hardware notes (LNC)

- Board: STM32 Nucleo-L476RG
- Data Logging Shield v1.0 — SD card + external RTC
- 9-in-1 multifunctional expansion board

Rough mapping (confirm against the actual board before wiring):

| Spec item | Hardware |
|---|---|
| temperature + humidity | DHT11 |
| light | LDR → ADC |
| battery voltage | potentiometer → ADC |
| object detection | ultrasonic / IR sensor |
| status LED | RGB LED |
| alarm | buzzer |
| stop alarm | push button (EXTI, debounced) |
| timestamps | RTC |
| log + event files | SD card (FatFS) |
| config storage | internal Flash |
| WD | IWDG |

---

## 6. Coding conventions

- C: opaque struct ADTs, one module per `.c/.h` pair, null checks on every public function.
- FreeRTOS: one task per module where it makes sense; queues between modules instead of
  direct calls, so the module boundaries in this document stay real in the code.
- ISRs do the minimum possible — clear the flag and signal a task (queue send /
  task notification / semaphore give from ISR). No module logic runs inside an ISR.
- No module calls the UART API directly except Communication.
- Keep TLV encode/decode in one shared place so the LNC and the Central Computer cannot
  drift apart.
- Before writing any LNC firmware code: do the peripheral/timer/clock allocation design
  in section 9 first (see "Peripheral & timer allocation" under Build order). Don't pick
  a timer ad hoc per module as you go — decide the whole map once, so nothing conflicts.

---2

## 7. Details not in the document

The document leaves these open. Several are **already decided in the code**.
Read the code first and follow what is there. Do not invent a new answer, and do not
change an existing one without asking.

- TLV tag numbers — check the shared header before adding a tag.
- Endianness of the Length and Value fields.
- Error range: the commands only set the Normal and Warning bounds, so Error is
  "everything else".
- Database used by the Central Computer.
- File name format for the daily log files and the events file.
- Whether the alarm restarts if a new event arrives after the button stopped it.

If a decision is found in the code, add it to this list as a short line so the next
session does not have to go looking for it.

---

## 8. Build & test commands

**Protocol unit tests (Linux/WSL) — the fast, no-hardware feedback loop:**
```bash
cd Shared/ProtocolTLV && make        # runs the C unit tests, then the C++ link check
cd Shared/ProtocolTLV && make test   # C unit tests only (test.c)
cd Shared/ProtocolTLV && make cpp    # C++ link check only (link_test.cpp) — proves tlv.c links cleanly into C++
cd Shared/ProtocolTLV && make clean
```
Binaries land in `Shared/ProtocolTLV/build/`. Validate protocol changes here
before opening CubeIDE — flashing takes the USB device away from Linux (see
below), so this loop has to work standalone. The makefile builds with
`-Werror -pedantic -Wconversion -Wshadow -Wstrict-prototypes`: a warning in
shared protocol code is treated as a bug, since it runs on both ends of the wire.

**CentralComputer / GroundStation (WSL Ubuntu):**
```bash
cd CentralComputer && make
cd GroundStation   && make
```
Binaries land in `build/`. Currently only `CentralComputer/src/uartTransport/`
exists (its own makefile), producing `sermon`, a raw serial monitor used to
prove the byte transport works before protocol logic sits on top of it.

**Firmware:** open `LNC/` in STM32CubeIDE on Windows, build, flash over
ST-LINK. `Shared/` is added to the CubeIDE project as an extra source/include
location, so the firmware compiles the exact same `Shared/ProtocolTLV` files
as the PC side — never fork or copy protocol code into `LNC/`.

**Running against real hardware:** the board's USB is claimed by either
Windows or WSL, never both (WSL2 can't see Windows COM ports directly —
`usbipd` hands the device across). From PowerShell (`Config/stm.ps1`):
`stm win` (CubeIDE can flash) / `stm linux` (`/dev/ttyACM0` appears in WSL,
with an auto-attach watcher for unplug/replug). Then:
`./build/central_computer /dev/ttyACM0` and `./build/ground_station`.

---

## 9. Build order

Where we are: the TLV protocol (`Shared/ProtocolTLV/`) is done and committed.
`CentralComputer/src/uartTransport/` has the raw serial transport (`SerialPort`
+ `sermon`) working over real UART. `LNC/Core/Src/main.c` already has USART2
configured by CubeMX at 115200 8N1 (`huart2`), matching `SERIAL_BAUD` on the
PC side — the peripheral is ready, no firmware logic uses it yet. Nothing
else is built. Follow this order; don't jump ahead to a later step while an
earlier one is unverified, since each step is what lets the next one be
tested against something real instead of assumption.

### 1. LNC Communication module

- Owns USART2 (`huart2`) exclusively — no other firmware module touches HAL
  UART calls or `huart2` directly.
- Uses `Shared/ProtocolTLV/tlv.h` for both directions: `tlv_encode()` to build
  outgoing frames, `tlv_receiver_t` + `tlv_receiver_feed_byte()` to decode
  incoming bytes (this is the byte-at-a-time streaming parser, built exactly
  for HAL UART RX arriving a few bytes at a time — do not use `tlv_decode()`
  here, that's for tests/whole-buffer input).
- Send side: a priority queue/ordering — keep-alive > events > data reports
  (section 2.5). A simple fixed-priority scheme (e.g. three queues, always
  drain highest non-empty first) is enough; nothing in the spec asks for
  more.
- Receive side: decoded frames get handed to whichever module they're for —
  Configuration for the `SET_*`/management-command tags, Init for
  `TIME_REPLY`, Log/Data-Collection paths for the two query tags. This
  routing can be a simple switch on `tag` for now.
- Everything else in the firmware (Monitor, Event, Log, Configuration, Init,
  Keep-Alive) calls into Communication to send; it never sees `huart2` or a
  raw byte.

### 2. CentralComputer Communication module

- Same shape, C++ side. Sits on top of the existing `SerialPort`
  (`CentralComputer/src/uartTransport/serial.h`/`.cpp`) — reuse it as-is,
  don't duplicate termios/fd handling.
- Same `tlv_encode()` / `tlv_receiver_t` pair from `Shared/ProtocolTLV`,
  compiled as C from C++ (already proven working — that's what
  `link_test.cpp` and the `cpp` makefile target check).
- Plus an incoming-message router/listener: reads off `SerialPort`, feeds the
  receiver, and dispatches decoded frames by tag to whichever module needs
  them (Management Command for the reply/ack tags, Log + Data Collection for
  reports and events).
- This is also the point where the transport this module talks to becomes
  swappable — see the "Planned: Ethernet-simulation gateway" note above.
  Build against `SerialPort` now; the gateway/TCP swap is deferred, but the
  module boundary should already make that swap a one-line change (pass in
  whatever satisfies the transport calls Communication needs, not
  `SerialPort` specifically).

### 3. Round-trip test on real hardware

- Before writing more code on either side: flash the LNC with just enough to
  send one hand-crafted frame (e.g. a fake `TLV_TAG_KEEP_ALIVE`) on boot or
  on a button press, and have the Central Computer's Communication module
  print whatever it decodes.
- This is the cheapest point to catch a protocol-agreement bug — endianness,
  CRC, tag values — before Monitor, Event, Log, or Management Command exist
  and have code depending on the wire format being right.
- Use `stm win`/`stm linux` (`Config/stm.ps1`) to flash then hand the board to
  WSL, per the "Running against real hardware" section above.

### 4. LNC business logic

Before writing any of the modules below: do a full peripheral/timer/clock
allocation design pass. Decide once, on paper, which hardware resource
drives what — don't assign a timer per module ad hoc as each one gets built,
that's how two modules end up fighting over the same peripheral. What needs
deciding, from what's already fixed by the spec and section 5's hardware
mapping:

- **Already fixed** — USART2 (`huart2`) at 115200 8N1 for Communication
  (configured in `main.c`); IWDG for Watchdog (dedicated peripheral, its own
  clock, doesn't compete for a general-purpose timer).
- **Periodic tasks needing a time base** — Monitor (5 s), Keep-Alive (6 s),
  Watchdog refresh (must be faster than the IWDG timeout). Under FreeRTOS
  these are most naturally `vTaskDelayUntil`/software timers on the RTOS tick
  rather than dedicated hardware timers each — decide whether any of them
  genuinely need a hardware TIMx (e.g. for jitter-free sampling) or whether
  the RTOS tick is enough; don't reach for a timer peripheral by default.
- **ADC** — light (LDR) and battery voltage (potentiometer) both need an ADC
  channel; decide single ADC with two channels (scanned or timer-triggered)
  vs. two separate reads, and whether conversions are triggered by a timer or
  read on demand from Monitor's task.
- **DHT11** (temperature + humidity) — bit-banged single-wire protocol with
  tight microsecond timing; decide which GPIO and whether it's driven from a
  timer-gated bit-bang routine or a dedicated peripheral trick. This is the
  one sensor that can't just be "read from a task with no care for timing."
- **Object Detection sensor** — depends on which of ultrasonic/IR is
  actually wired (section 5 says "confirm against the actual board"); an
  ultrasonic echo is naturally a timer input-capture, an IR/PIR digital
  output is naturally an EXTI. Pick based on what's actually on the 9-in-1
  board.
- **Button (stop-alarm)** — EXTI line, debounced (section 5). Debounce is
  either a short software timer started from the EXTI ISR, or a periodic
  poll from the owning task — decide one approach and reuse it if any other
  button/input needs debouncing.
- **RTC** — timestamps for Log/Event; external RTC on the Data Logging
  Shield (I2C or SPI, confirm which) vs. the STM32's internal RTC — decide
  which one is authoritative once Init's time-sync design is worked out, so
  there's exactly one source of truth for "now."
- **Buzzer / RGB LED** — plain GPIO toggles are enough per the spec (on/off
  per mode, no mention of intensity or tone control); don't reach for PWM
  timers here unless a real requirement shows up for it.
- **SD card (FatFS)** — SPI peripheral + chip-select GPIO; note which SPI
  instance so it doesn't collide with anything else on the same pins.

Write the resulting map (peripheral → owning module → FreeRTOS task) down
here once it's decided, before implementing, so the next session doesn't
have to reverse-engineer it from `main.c`.

In this order — each one is small and independently testable once
Communication exists to observe it through:

1. **Init** — time sync request over Communication, then starts everything
   else. Simplest module; nothing depends on sensor data.
2. **Watchdog** — refresh on schedule; Init needs to be able to report a WD
   reset (section 2.9), so build this early enough to test that report path.
3. **Keep-Alive** — 6 s timer sending timestamp + latest measurement + mode
   through Communication. Gives you a heartbeat visible on the CC side with
   minimal logic, useful as an ongoing smoke test for everything built after
   it.
4. **Monitor** — 5 s sampling loop, limit comparison, mode decision, sends to
   Log always and to Event only on a mode change (section 2.1).
5. **Event** — the transition/LED/alarm table in section 2.3. Depends on
   Monitor existing to drive it.
6. **Log** — daily file, 7-day rotation (section 2.4).
7. **Configuration** — Flash persistence, defaults on first boot, receives
   changes via Communication (section 2.6). Left for after Monitor/Event
   since it's what tunes their thresholds, not what they need to exist.
8. **Object Detection** — last: independent of the others, lowest priority
   per the "not in scope" neighbors (Motor/Navigation Units) in the data-flow
   diagram, and needs Event but nothing else.

### 5. CentralComputer business logic (parallel to step 4)

- **Management Command** — builds/sends the management commands listed in
  section 2.5, handles their replies/acks. Can be developed and unit-tested
  without the board using a canned-buffer replay transport (the mock
  described in the README's "Design decisions" — distinct from the
  Ethernet-gateway note above, which is about a real alternate transport,
  not a test fixture).
- **Log** — prints and writes CC-side logs.
- **Data Collection & Analysis** — stores measurement/event data (database
  choice is an open item, section 7), builds reports by criteria. Keeps one
  week of data, same as the LNC (section 3).

### 6. GroundStation

Last, and the most self-contained: it only needs a `CentralComputer` object
to query, so none of it blocks on the board.

- **Communication side** — talks to a submarine's Central Computer over
  Ethernet, requests log/event data by time range, must handle several
  submarine types.
- **Fleet management / OOP side** (section 4) — `Submarine` base class,
  `ResearchSubmarine`/`CombatSubmarine` derived classes, the menu (10
  options), the course rules (manager class owns data, no input inside
  classes, `set` returns `bool`, exact-size string allocation, x2-growth
  arrays). `CombatSubmarine` owns a `CentralComputer` — that's the link back
  to Part B.
 No newline at end of file