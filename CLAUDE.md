

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
- File name format: events file is a single flat `EVENTS.TXT`, never rotated
  (event.c). Daily log files — decided: `LOG1.TXT`..`LOG7.TXT`, one per
  weekday, Sunday-first (`1`=Sun..`7`=Sat, remapped in log.c from HAL's
  `RTC_WEEKDAY_MONDAY(1)..SUNDAY(7)` via `(WeekDay % 7) + 1`), constrained by
  FatFS running in 8.3 short-filename mode (`_USE_LFN 0` in `ffconf.h`).
  Reusing the same slot every 7 days gives "keep 7 days, delete the oldest on
  day 8" for free: log.c deletes whatever's in today's slot on the first
  write of each new day (harmless no-op via `FR_NO_FILE` for the first week,
  a real deletion of last week's file from day 8 onward) — no file listing
  or date parsing needed anywhere.
- **Known open issue, not yet fixed:** pulling the SD card out and reinserting
  it makes mounting keep failing afterward (observed on hardware while testing
  Log). Likely cause, not yet confirmed: `user_diskio.c`'s
  `static volatile DSTATUS Stat = STA_NOINIT;` caches "card is initialized"
  across calls — a card swap doesn't reset it, so FatFS can skip real
  re-initialization on the next mount and fail talking to the card. Needs a
  real fix (probably detecting the swap and clearing `Stat`, or forcing
  `disk_initialize()` again) before relying on hot-swapping the card during
  testing or operation.
- Whether the alarm restarts if a new event arrives after the button stopped it.
- Object Detection hardware: a VS1838B IR remote-control receiver used as a demo
  stand-in (point a remote at it and press buttons to simulate an object present) —
  see section 9 step 4's "Object Detection sensor" bullet for the full design.
- RTC authority: internal STM32 RTC is the working clock for every runtime
  timestamp; external DS1307 (I2C, `PC0`/`PC1`) is the durable source of truth,
  synced to the internal RTC at boot and on `SET_TIME` — see section 9 step 4's
  "RTC" bullet for the full design and why (no backup battery on `VBAT`).
- "Suppress non-essential ops" (section 2.3's any→Error row): not implemented
  yet, plan only. `event_is_essential_only()` already tracks the flag correctly
  (event.c), but nothing reads it. Planned home: the LNC Communication module's
  send path drops/refuses to queue `TLV_TAG_DATA_REPORT` frames while it's true,
  leaving keep-alive and events unaffected — matches Communication's own existing
  priority tiers (2.5: keep-alive > events > data reports), so "non-essential"
  == the lowest tier. Deferred until Communication is un-stubbed for real
  (`communication_create()` is still commented out in `main.c`) and something
  actually sends `TLV_TAG_DATA_REPORT`, so this has real traffic to gate instead
  of an untestable guess.

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
- **Periodic tasks needing a time base — decided: `osDelayUntil`, no
  hardware timer.** Monitor (5 s), Keep-Alive (6 s), Watchdog refresh
  (must be faster than the IWDG timeout) all use CMSIS-RTOS v2's
  `osDelayUntil` (the wrapper this project's Communication code already
  standardizes on, over raw FreeRTOS `vTaskDelayUntil`) inside each
  task's own loop — not a dedicated hardware `TIMx`. Reasoning: it's
  drift-free (computes the next wake point from the last *intended* one,
  so per-round jitter in the work itself never accumulates), costs zero
  peripherals (`TIM2`/`TIM3` are already spoken for elsewhere), and none
  of these three genuinely need sub-millisecond jitter-free precision —
  that's the only case that would justify a real hardware timer instead.
  Shape: `tick = osKernelGetTickCount(); for(;;) { ...work...; tick +=
  5000; osDelayUntil(tick); }` (5000 ticks == 5 s at this project's 1 ms
  tick rate).
- **ADC — decided: on-demand from Monitor's task, one dedicated ADC
  peripheral per sensor, no scan mode/DMA/timer trigger.** Battery voltage
  (`PA0`, potentiometer) reads through `ADC1` (12-bit); light (`PA1`, LDR)
  reads through `ADC2` (8-bit — resolution lowered in the `.ioc` to match).
  Superseded from an earlier plan to share both channels on `ADC1` with
  sequential reads — implemented and kept as two separate ADCs instead.
  Only read once every 5 s each (Monitor's own round), so there's no real
  throughput/continuous-sampling need that scan-mode or DMA would
  actually earn its complexity for. Monitor's task just does, per
  peripheral: `HAL_ADC_Start()` → `HAL_ADC_PollForConversion()` →
  `HAL_ADC_GetValue()` → `HAL_ADC_Stop()`. Nothing touches either ADC
  between Monitor's rounds.
- **DHT11 — decided: `PB5` (`DHT_Pin`), `TIM2` as a free-running
  microsecond counter.** Bit-banged single-wire protocol:
  `PB5` runs as plain push-pull output to drive the line, briefly
  reconfigured to input to read the sensor's response bits back. Timing
  comes from `TIM2` (32-bit — avoids the wraparound headaches a 16-bit
  timer would have for a free-running µs counter), `Prescaler = 79` for
  the same 1 MHz/1 µs-per-tick math as the buzzer's `TIM3`, left running
  continuously with no interrupt — the bit-bang routine just polls
  `TIM2->CNT` directly to measure each pulse width. No ADC involvement
  (DHT11 is fully digital); no conflict with `TIM3` (buzzer PWM), which
  stays dedicated to that.
- **Object Detection sensor — decided.** The 9-in-1 board's IR module is a
  VS1838B, a 38 kHz demodulating IR *remote-control receiver*, not a
  reflective proximity/obstacle sensor — it has no emitter and only reacts
  to an actively-transmitting IR remote pointed at it, not to a passive
  nearby object. Used as a **stand-in "object present" trigger for demo
  purposes**: point any IR remote at it and press buttons to simulate an
  object being in range.
  - Wiring: `D6` → `PB10` (`IR_Sensor_Pin`, already in `main.h`/`.ioc`
    under that label).
  - Mode: EXTI, both edges (`GPIO_MODE_IT_RISING_FALLING`) — a single
    button press produces a burst of many edges, not one clean transition;
    the ISR does the minimum (per the ISR rule) and just restarts a
    one-shot software timer (`osTimerStart`, restarts the countdown if
    already running) on every edge.
  - Semantics: **presence-by-recency, not by level.** While the timer
    hasn't expired, "object" is considered present. If the timer is ever
    allowed to expire (no edge refreshed it in time), that fires "object
    no longer found." An edge arriving while state was "not found" fires
    "object detected" — these are edge-triggered *transitions* into
    Event (matching section 2.2), not one message per button press/edge.
  - Timeout length: not finalized yet — ~10 s was discussed as a rough
    starting point, needs actual tuning once built.
  - Reuses the same "software timer restarted from an EXTI ISR" debounce
    technique the stop-alarm button (`B1_Pin`) also needs — see the
    Button bullet below; both should share one approach.
- **Button (stop-alarm) — decided: `BUTTON_D2`, moved to `PB3`** (not the
  shield's default `PA10` — see below), the 9-in-1 shield's pushbutton,
  not the Nucleo's onboard `B1` (`PC13` — `B1` is CubeMX's out-of-the-box
  default EXTI setup from project creation, not an intentional choice;
  treat it as unused unless repurposed later).
  - **Why `PB3`, not `PA10`:** `PA10` is the same pin *number* (10) as
    `IR_Sensor` on `PB10`. STM32's EXTI interrupt lines are numbered 0-15
    and shared across all ports at that pin number — only one port's
    `Px10` can actually feed "EXTI Line 10" at a time (`SYSCFG_EXTICR`
    picks which), so `PA10` and `PB10` can never both be true hardware
    interrupts simultaneously. `PB3` is a genuinely free pin number, no
    conflict with anything else allocated so far.
  - `PB3` was previously locked to `SYS_JTDO-SWO` (CubeMX's default debug
    trace pin, `Locked=true` in the `.ioc`) — freed by changing CubeMX's
    `System Core → SYS → Debug` setting from the 3-pin trace mode down to
    plain "Serial Wire" (2-pin: `SWDIO`/`SWCLK` only). SWO isn't needed
    for this project, so nothing is given up by freeing it.
  - Still needs the same CubeMX change the IR sensor needed: currently
    plain `GPIO_MODE_INPUT` (unmonitored), has to become an EXTI mode to
    be event-driven.
  - Trigger edge/pull not confirmed yet: `B1`'s `NOPULL` + `IT_FALLING`
    combination relies on a pull-up resistor built into the Nucleo board
    itself for that specific pin, which the shield's separate button
    module may or may not replicate — confirm this module's own wiring
    before picking `IT_FALLING` vs `IT_RISING`, same "verify, don't
    assume" situation as the IR sensor's polarity.
  - Debounce: either a short software timer started from the EXTI ISR, or
    a periodic poll from the owning task — the IR sensor's
    presence-timeout timer (see above) is the same general technique,
    reuse whichever approach gets picked.
- **RTC — decided: dual-source, internal RTC is the working clock.**
  Hardware is the STM32L476's internal RTC peripheral plus an external
  DS1307 on the Data Logging Shield (I2C — the `I2C3_SCL`/`I2C3_SDA` pins
  on `PC0`/`PC1`), which has its own coin-cell backup on the shield. Both
  exist because Nucleo boards ship with `VBAT` tied to `VDD` via solder
  bridge `SB45` and no separate backup battery — the internal RTC forgets
  everything on every power cycle, so it can't be the durable source of
  truth on its own; the DS1307's own battery is what actually survives a
  power loss.
  - **Internal RTC is authoritative for every runtime timestamp** —
    Monitor, Event, and Log all read it directly (a plain register read,
    no I2C transaction, no dependency on the bus being healthy, no added
    latency per timestamp).
  - **External DS1307 is the durable source of truth**, touched only at
    specific moments, not on every timestamp:
    - At boot, Init reads the DS1307 once and sets the internal RTC from
      it — this is the fallback value, best guess available immediately,
      before Communication with the Central Computer is even up.
    - **One unified `set_time(datetime)` operation** (not two different
      code paths) is used by both of the CC-sourced time events below: it
      writes the given value to internal RTC immediately and to DS1307 in
      parallel, both sourced directly from the value CC provided — never
      a write-then-read-back through DS1307. Internal RTC's correctness
      must never depend on DS1307/I2C bus health, matching the "no I2C
      transaction, no dependency on the bus being healthy" property it
      already has for reads.
      - Init's own boot-time sync request (section 2.7, "asks the Central
        Computer for date/time sync") calls `set_time()` when CC replies —
        this *corrects* the DS1307-sourced fallback set moments earlier.
      - A runtime `SET_TIME` command from the Central Computer calls the
        same `set_time()`.
    - Optionally, a periodic resync (e.g. daily) re-reads the DS1307 to
      correct the internal RTC for crystal drift accumulated since boot.
- **Buzzer — decided: PWM, not a plain toggle, driven by a `Buzzer` ADT
  (`LNC/Core/{Inc,Src}/buzzer.c/.h`), and TIM3 alone does both jobs —
  no second timer.** For a real tone rather than a flat click. `TIM3_CH1`
  on `PB4` (a small general-purpose timer — `PB4` is the chip's default
  `NJTRST` pin, free for this because the board's debug config is SWD,
  not JTAG — same reason `PB3` ended up free too, see the Button bullet
  above; this project doesn't use SWO). Base config: `Prescaler=79` for
  an 80 MHz APB1 timer clock this project runs at → a 1 MHz counter
  (80 MHz / 80); `Period`/`Pulse` are then rewritten per note from
  `buzzer.c`'s `note_table` (one {period, pulse} pair per `Note`, ~0.9%
  duty at whatever period each note's frequency needs) instead of the
  fixed single-tone values used before this ADT existed.
  - **Note timing reuses TIM3's own update-elapsed interrupt** (already
    enabled in the `.ioc` as `NVIC.TIM3_IRQn`, wired through
    `stm32l4xx_hal_msp.c`/`stm32l4xx_it.c`) rather than a second
    hardware timer — TIM3's update event fires once per PWM cycle
    (i.e. at the tone's own frequency, not once per note), so
    `Buzzer_DurationElapsed()` (called from `HAL_TIM_PeriodElapsedCallback`
    in `buzzer.c`) accumulates elapsed microseconds across cycles and
    only acts (stop, or advance to the next note) once a note's
    requested duration has actually passed.
  - **The alarm is the same ADT, not raw `HAL_TIM_PWM_*` calls.** Event's
    `alarm_start()`/`alarm_stop_if_active()` call `Buzzer_StartAlarm()`/
    `Buzzer_Stop()`, which is created once in `event_create()`
    (`Buzzer_Create(&htim3, TIM_CHANNEL_1)`) — Event owns it since it's
    the only consumer. `Buzzer_StartAlarm()` alternates two notes
    (`NOTE_A1`/`NOTE_E2`, 300 ms each) indefinitely for a rising/falling
    siren, driven by the same duration-elapsed mechanism above, until
    `Buzzer_Stop()`.
  - `Buzzer_Handle` is a static singleton (matching every other ADT in
    this codebase — `Communication`, `Monitor`, `Event`), not `malloc`'d.
- **RGB LED — decided: plain GPIO, on `PB13` (red) / `PB14` (blue) /
  `PB15` (green).** The spec only needs discrete colors via on/off R/G/B
  combinations, no brightness or color-mixing control — so no PWM needed
  here, unlike the buzzer. Don't reach for PWM on this one unless dimming
  becomes an actual requirement.
- **SD card (FatFS)** — SPI peripheral + chip-select GPIO; note which SPI
  instance so it doesn't collide with anything else on the same pins.

Write the resulting map (peripheral → owning module → FreeRTOS task) down
here once it's decided, before implementing, so the next session doesn't
have to reverse-engineer it from `main.c`.

In this order — each one is small and independently testable once
Communication exists to observe it through:

1. **Init** — reads the external DS1307 once to set the internal RTC (see
   the RTC bullet above), then does the time sync request over
   Communication, then starts everything else. Simplest module; nothing
   depends on sensor data.
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