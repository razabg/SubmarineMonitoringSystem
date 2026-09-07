

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

### Ethernet-simulation gateway (requested, not yet built)

Was deferred ("build this when asked, don't start unprompted") — now asked
for, next up on the Central Computer.

```
STM32 (real UART) <--UART-->  gateway (new, separate process)  <--TCP, localhost-->  Central Computer
```

**Selectable, not a default swap.** UART and Ethernet are both real options,
chosen at connect time — not "Ethernet replaces UART." When UART is chosen,
Communication talks straight to the real serial port, no gateway involved at
all. When Ethernet is chosen, Communication talks to the gateway over TCP
instead. Same `Communication` code either way; only which transport it was
handed at construction differs.

**Why a gateway at all**: the board only ever has UART; there is no real
Ethernet hardware. To still exercise the "Ethernet" transport path end-to-end
rather than just UART forever, a small standalone gateway process owns the
real serial port (reusing `SerialPort` from
`CentralComputer/src/uartTransport/serial.h`/`.cpp` as-is — no new UART code)
and exposes it as a plain TCP socket on localhost. It is a dumb byte pipe: no
TLV parsing, no framing, both directions — it never calls `tlv_encode()` or
`tlv_receiver_feed_byte()`, and has no dependency on `Shared/ProtocolTLV`.
Encode/decode happens only in the Communication module on each real endpoint
— the LNC firmware and the Central Computer — regardless of which transport
(UART or the gateway's TCP socket) sits underneath it. The bytes the gateway
forwards are the already-TLV-encoded frame, unchanged end to end.

**Client/server roles**: the **gateway is the TCP server** (it owns the
serial port resource, so it binds/listens/accepts); the **Central Computer's
Communication module is the TCP client** (it connects out to the gateway,
never includes `termios.h`, never touches a serial device directly). As far
as Communication's code is concerned, it is talking Ethernet to the LNC —
that satisfies the transport rule above literally, not just as a simulation.
Plain TCP over loopback is the deliberate choice over a Unix-domain socket or
pipe, specifically so this is the same BSD-sockets code the real GroundStation
↔ CentralComputer Ethernet link needs later — pointing at a different address
is the only change (that later link's own client/server roles are separate
and not necessarily the same direction as this one; the reuse is the general
BSD-sockets approach, not a claim that CC is a client in both relationships).

**Full byte flow, LNC → CC** (CC → LNC is the exact mirror): LNC's
Communication `tlv_encode()`s a frame and pushes it out over the physical
UART wire → the gateway reads those raw bytes off the serial port (same
`SerialPort` read loop as `sermon.cpp` today) and, with zero parsing, writes
the identical bytes out over its TCP connection → CC's Communication reads
them off its TCP socket and feeds them into the same `tlv_receiver_feed_byte()`
streaming decoder already used for direct-serial mode → once a full frame
assembles, it routes to `on_management_`/`on_log_` exactly as today. Every
hop except the two real endpoints is a raw byte relay; nothing in between
ever understands TLV.

**Practical gotcha**: the gateway is a dumb pipe with no buffering-for-later
— if it reads bytes off the serial port while no TCP client is connected yet,
there's nowhere to send them and they're dropped. Same habit as
`comm_test.cpp` already requires ("listening. reset the board now."): start
the gateway and get the Central Computer connected *before* resetting the
board, to catch everything from boot.

**Transport-interface refactor — done.** `Communication` no longer owns a
concrete `SerialPort` directly; it now takes a `Transport &` (new
`transport.h`, a small abstract class: `read()`/`write()`/`reconnect()` —
exactly what `rx_loop()`/`send()` already called on `port_` before, so no
behavior changed, only what type it's called through). `Communication`'s
constructor changed from `Communication(const std::string &path)` (opened
the port itself) to `Communication(Transport &transport)` (takes an
already-open transport; the transport's lifetime is the caller's
responsibility, `Communication` only holds a reference — it must outlive the
`Communication` object). New `serial_transport.h`/`.cpp`: `SerialTransport`
adapts `SerialPort` to the `Transport` interface by composition (holds a
`SerialPort` member, forwards each call) — `SerialPort` itself is completely
untouched, still the same hardware-tested class. Declarations in the header,
implementations in the `.cpp`, matching every other class in this codebase
(`communication.h`/`.cpp`, `serial.h`/`.cpp`) — not left inline in the header.
`comm_test.cpp` updated to build a `SerialTransport` and hand it to
`Communication` — confirmed compiling clean (`-Wall -Wextra -Wpedantic`, zero
warnings) via the existing makefile (new `serial_transport.o` target added,
`OBJ`/dependency lines updated for the new files).

**TCP-based `Transport` — built.** New `tcp_transport.h`/`.cpp`:
`TcpTransport` is always the **client** side (the gateway is the server —
see "client/server roles" above), connecting to `host:port` at construction,
RAII-style like `SerialPort` (constructor connects, destructor closes, no
separate open/close). Split into declarations (`.h`) and implementation
(`.cpp`) from the start, matching every other class here.

One real semantic difference from `SerialPort::read()` had to be handled
carefully: for a serial port, a `read()` returning `0` just means "nothing
arrived before the timeout" — but for a TCP socket, `recv()` returning `0`
specifically means *the far end closed the connection*, a genuine
disconnect, not a timeout. `TcpTransport::read()` tells these apart: a
`SO_RCVTIMEO` of 100ms (matching `SerialPort`'s own `VMIN=0`/`VTIME=1`, i.e.
0.1s, so both transports look the same to `Communication`'s `rx_loop()`) is
set on the socket, so `recv()` timing out (`errno == EAGAIN`/`EWOULDBLOCK`)
returns `0` exactly like `SerialPort` would — but an actual `recv() == 0`
(orderly peer shutdown) is thrown as a `std::system_error` instead, so
`rx_loop()`'s existing catch-and-reconnect logic handles a real gateway
disconnect exactly like it already handles a real serial disconnect, with
no changes needed on the `Communication` side.

Address resolution uses `getaddrinfo()` (not a hardcoded `sockaddr_in` +
`inet_pton()`), so a hostname would work too, not just a literal IP —
though in practice this will point at `127.0.0.1` (the gateway, running on
the same machine). Confirmed compiling clean (`-Wall -Wextra -Wpedantic`,
zero warnings) via the makefile (`tcp_transport.o` target added, `OBJ`
updated) — not yet wired into `comm_test.cpp`'s actual usage or exercised
end-to-end, since the gateway process itself doesn't exist yet.

**Gateway process — built**: new `CentralComputer/src/gateway/gateway.cpp`
(+ its own `makefile`). Owns the real serial port via `SerialPort` (reused
as-is), listens as a TCP server (default `127.0.0.1:5555`, both
overridable via args — `./gateway [serial_device] [tcp_port]`), and relays
bytes both directions between whichever client connects and the serial
port using two threads (mirroring `Communication`'s own TX/RX split —
`serial_to_tcp()`/`tcp_to_serial()`, no coordination needed between them
beyond a shared stop flag). Loops back to `accept()` after a session ends,
so the Central Computer can restart/reconnect without restarting the
gateway.

A real bug was caught by testing: `SerialPort`'s constructor throws on
failure, and that was initially left to propagate straight out of
`main()` uncaught, crashing via `std::terminate()`/`abort()` instead of
failing gracefully. Fixed with the same try/catch pattern `comm_test.cpp`
already uses. `fflush(stdout)` added after every status print, matching
`comm_test.cpp`'s own convention.

`comm_test.cpp` now supports both transports: `./comm_test /dev/ttyACM0`
(UART, default if no args) or `./comm_test --tcp host:port` (Ethernet, via
the gateway) — the choice happens once, in `main()`, building either a
`SerialTransport` or a `TcpTransport` into a `std::unique_ptr<Transport>`
before constructing `Communication` from it; `Communication`'s own code
is unchanged either way.

**Confirmed working end-to-end without real hardware**, using a `socat`-
created linked PTY pair as a stand-in serial port (`gateway` opened one
end as if it were `/dev/ttyACM0`) and a hand-encoded `tlv_encode()` frame
(a throwaway tool built against the real `tlv.c`) written directly to the
other end to stand in for "the LNC":
- **CC → LNC**: `comm_test`'s auto-sent `QUERY_DATA`/`QUERY_EVENTS` test
  frames were correctly captured on the fake board side, byte-for-byte
  correct (`a5 5a 30 0c 00 01 01 00 00 00 63 0c 1f 17 3b 3b ...` — sync,
  tag, len, the exact widest-range payload, CRC).
  - The gateway's accept-loop reconnect behavior was also confirmed:
    multiple sequential `comm_test` connections were each accepted,
    relayed, and cleanly torn down, with the gateway correctly returning
    to `accept()` for the next one each time.
- **LNC → CC**: a hand-crafted `TLV_TAG_KEEP_ALIVE` frame written to the
  fake board side came back out of `comm_test` as a correctly decoded
  `LOG tag=0x14 len=12 value=...` line, matching the input payload exactly.

Not yet tested against the real board — that's next, on real hardware
(`stm linux`, the real `/dev/ttyACM*`, and the real gateway/TCP path all
together for the first time).

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
- **Known open issue, not yet fixed: `DHT11.c`'s read is fully blocking.**
  `DHT_Read()` bit-bangs the single-wire protocol via busy-wait polling
  loops timed against `TIM2` (`delay_us()`, `wait_for_response()`,
  `read_bit()` x40) — no `osDelay()` or yielding anywhere in the file. The
  18ms start-signal hold alone plus 40 bits at up to ~150us each adds up to
  roughly 20-25ms of the CPU doing nothing else, every time `monitor_task()`
  calls it. Hardware interrupts aren't masked anywhere in this code, so
  ISR-driven things (Object Detection's EXTI, the buzzer's TIM3) are
  unaffected — the real cost is that other **tasks** at Monitor's priority
  or lower only get scheduled in via FreeRTOS's tick-based time-slicing
  during that window, not freely. Likely tolerable given this project's
  other timing is all second-scale (5s Monitor rounds, 10s presence
  timeout), but that's incidental, not a real fix. Not addressed yet —
  would need converting the polling into something that yields
  periodically, or moving the bit-timing onto a hardware capture mechanism
  instead of software polling.
- **Object Detection status (as of 2026-09-06):**
  - **Built, wired, and confirmed working on hardware, SD card installed**:
    `objectdetection.c/.h` (the presence state machine — EXTI edge on `PB10`
    resets TIM5's 10 s countdown via a direct register write from the ISR,
    ISR-safe; a dedicated task, woken via `osThreadFlagsSet()`, does the
    actual `event_object_detected()`/`event_object_cleared()` calls, since
    those touch the SD card through FatFS and can't run in interrupt
    context — same reasoning ruled out calling `osTimerStart()` from the
    ISR too, confirmed by reading the actual `cmsis_os2.c`, which
    explicitly errors out if called from an ISR). Wired into
    `init_create()` alongside the other modules. `TIM5` NVIC/MSP/vector
    fully enabled; `PB10` is `GPIO_MODE_IT_RISING_FALLING`.
  - **Sonar sound built**: `Buzzer_StartSonar()` in `buzzer.c`/`.h` — a
    "ping ... ping ... ping" pattern (150 ms `NOTE_C2`, 850 ms silence,
    repeating), distinct from the alarm's continuous siren. The silent gap
    is timed by setting the duty cycle to 0 rather than stopping the timer,
    so the same interrupt keeps timing it. Wired into
    `event_object_detected()`/`event_object_cleared()`.
  - **Noise problem (SD card's SPI traffic triggering false detections)
    root-caused and solved in software**, after a decoupling capacitor and
    physical re-wiring weren't options (no spare parts/tools on hand).
    Fix is two layers in `objdet_on_edge()`, both required:
    1. **Speed rejection** (`MIN_EDGE_SPACING_US = 300`): an edge arriving
       less than 300us after the previous *accepted* one is discarded
       outright — SPI toggles at MHz rates (sub-microsecond), while a
       VS1838B's demodulated output can't physically change faster than
       ~562us (NEC's shortest real pulse element). Timestamped via `TIM2`
       (already a free-running 1us counter for DHT11), since
       `HAL_GetTick()`'s 1ms resolution can't tell these apart.
    2. **Burst confirmation** (`BURST_WINDOW_MS = 60`, `BURST_THRESHOLD =
       50`): requires 50 speed-filtered edges within 60ms before accepting
       a detection as real. Critically, this check applies whether or not
       `present` is already `true` — the first version of this fix only
       gated the *initial* detection this way but still reset TIM5
       unconditionally on any speed-filtered edge, so routine 5 s SD
       writes (each producing ~8-14 accepted-but-sparse edges — enough to
       clear the speed filter, not enough to be a real signal) kept
       refreshing the 10 s countdown forever and permanently stuck
       `present` at `true` after the first false trigger. Gating the
       *refresh* on the same burst proof fixed it. `BURST_THRESHOLD` was
       raised from 20 to 50 after a confirmed false positive at
       `burst_edge_count=21` (via a temporary debug print in
       `objdet_task()`, tick well past boot so not a startup artifact) —
       routine SD-write noise typically tops out around 8-14 edges but was
       observed spiking to 21 on at least one occasion, leaving 20 too
       thin a margin; a real click reliably produces 200+, so 50 leaves
       comfortable room on both sides.
    This is a heuristic, not real NEC protocol decoding — it is not
    mathematically guaranteed against noise that happens to be both
    correctly-timed *and* dense, but has been confirmed reliable across
    repeated real-hardware tests with the SD card installed and writing
    on its normal schedule throughout, including a clean 10-minute
    soak test at `BURST_THRESHOLD=50` with zero false positives and
    correct detection on every real remote click. Temporary debug
    prints (`objdet: DETECTED`/`cleared` in `objdet_task()`, and the
    earlier `edge_isr_count`/heartbeat instrumentation) have all been
    removed now that this is confirmed.
  - **Found and fixed a second bug causing rarer, ~4-5 min-interval false
    positives even after the burst filter above**: `TIM2` is shared with
    `DHT11.c`'s bit-banging, which zeroes it ~85 times during every 5 s
    Monitor round (`delay_us()`'s `__HAL_TIM_SET_COUNTER(h->timer, 0)`).
    Since `objdet_on_edge()` also reads `TIM2` for its speed-rejection
    check, a DHT11 reset landing between two IR edges made the unsigned
    subtraction `now_us - last_edge_us` underflow into a huge number —
    which looks "properly spaced" (i.e. passes the >=300us check) instead
    of the correct "impossibly fast, reject it." Fixed in `objdet_on_edge()`
    by detecting `now_us < last_edge_us` (only possible if the clock was
    reset out from under it) and treating that edge as untrustworthy
    rather than accepting it, while still recording it as the new
    baseline so the next edge measures correctly. `TIM2` remains shared
    with DHT11 -- this patches the specific symptom, it doesn't remove
    the underlying shared-timer conflict. A cleaner (not yet done) fix
    would give Object Detection its own dedicated free-running
    microsecond timer instead (a spare one, e.g. `TIM6`, was already
    identified as available).
  - **Breathing red LED — built and confirmed working on hardware.** Pin is
    `RED_LED_SONAR_Pin` on `PC9` (renamed from an earlier `BLUE_LED_SONAR_Pin`
    to match the actual LED color wired in).
    `sonarled.c/.h` (new files): a `SonarLed` ADT, same static-singleton
    shape as `Buzzer_Handle`, owned by Event (`g_event.sonar_led`,
    created via `SonarLed_Create(&htim8, TIM_CHANNEL_4)` in
    `event_create()`, alongside the Buzzer). Deliberately **task-driven,
    not interrupt-driven** — unlike the siren/sonar sound, which reuses
    `TIM3`'s own update interrupt for note timing, this fade is a slow
    (2 s half-cycle), non-critical visual effect, and `TIM8` runs its PWM
    at 10 kHz (`Prescaler=7`, `Period=999` — corrected from an earlier,
    wrong `65535` assumption), far faster than driving a multi-second
    fade from its own interrupt would need. So `SonarLed_Create()` starts
    a small dedicated FreeRTOS task that wakes every 30 ms via
    `osDelayUntil()` and nudges `TIM8`'s `CCR4` directly
    (`__HAL_TIM_SET_COMPARE()`) up and down between 0 and 999 (a
    triangle-wave sweep), idling (no register writes) whenever inactive.
    `SonarLed_Start()`/`SonarLed_Stop()` just flip an `active` flag plus
    start/stop the PWM channel itself
    (`HAL_TIM_PWM_Start/Stop(&htim8, TIM_CHANNEL_4)`) — no NVIC/interrupt
    configuration needed for `TIM8` at all, confirmed no conflict with
    the two remaining modules (Keep-Alive, Watchdog), which per section 9
    already use `osDelayUntil` with no hardware timer of their own.
    Wired into `event_object_detected()`/`event_object_cleared()`
    alongside the existing `Buzzer_StartSonar()`/`Buzzer_Stop()` calls.
    Confirmed working on real hardware.
  - **Known, deliberately deferred bug: alarm/sonar buzzer contention.**
    The buzzer can only sound one thing at a time (real hardware
    constraint), and `Buzzer_StartAlarm()`/`Buzzer_StartSonar()` silently
    cancel each other. Worse, `event.c`'s own `g_event.alarm_active`
    bookkeeping (managed by the separate `alarm_start()`/
    `alarm_stop_if_active()` helpers) doesn't know when Object Detection's
    sonar has silently interrupted-then-stopped an active Error alarm, so
    it can desync from Buzzer's actual state (Event still thinks the alarm
    is sounding when the buzzer has actually gone silent). Not fixed —
    needs a real decision (should Error mode take priority and auto-resume
    once Object Detection lets go, or should Object Detection refuse to
    interrupt an active alarm at all).
- Whether the alarm restarts if a new event arrives after the button stopped it.
- Object Detection hardware: a VS1838B IR remote-control receiver used as a demo
  stand-in (point a remote at it and press buttons to simulate an object present) —
  see section 9 step 4's "Object Detection sensor" bullet for the full design.
- RTC authority: internal STM32 RTC is the working clock for every runtime
  timestamp; external DS1307 (I2C, `PC0`/`PC1`) is the durable source of truth,
  synced to the internal RTC at boot and on `SET_TIME` — see section 9 step 4's
  "RTC" bullet for the full design and why (no backup battery on `VBAT`).
- **DS1307 needs a one-time manual seed before first deployment.** Nothing on
  the LNC has a genuine real-time source (no GPS/NTP/UI) — the only two ways
  real time ever enters the system are the CC sync (`init.c`'s
  `apply_cc_time()`, but that only works once a CC has answered at least
  once) and typing the correct time in by hand, once, via the commented-out
  `RTC_SetTime()` block at the bottom of `RTC_ds1307_I2C.c`. Because the
  DS1307 has its own coin-cell battery, this only has to happen once ever —
  it then keeps correct time through every future power cycle on its own,
  refined by the CC sync going forward. **Footgun:** that seed block must be
  disabled/removed again right after use — left enabled, it re-stamps the
  DS1307 with the same stale hardcoded value on every single boot,
  overwriting whatever correct time it had built up since.
- **Deliberate deviation from section 2.5's "set RTC date and time" command:
  not implemented, by design.** This project's time protocol only has two
  exchanges — Init's own boot-time sync request (`TLV_TAG_TIME_SYNC_REQUEST`
  → `TLV_TAG_TIME_SYNC_REPLY`, LNC asks CC) and the CC checking the LNC's
  current time (`TLV_TAG_GET_TIME` → `TLV_TAG_TIME_REPLY`, CC asks LNC). The
  CC is never able to push a time correction to the LNC outside of answering
  Init's own request — `TLV_TAG_SET_TIME` has been removed from `tlv.h`
  entirely (was `0x28`; `TLV_TAG_GET_TIME`/`TLV_TAG_TIME_SYNC_REPLY` shifted
  down to fill the gap) and from `communication.c`'s routing. If a future
  session needs to reconcile against the source document and finds "set RTC
  date and time" still listed there as its own command, this is a known,
  deliberate deviation, not an oversight — ask before reintroducing it.
- "Suppress non-essential ops" (section 2.3's any→Error row): not implemented
  yet, plan only. `event_is_essential_only()` already tracks the flag correctly
  (event.c), but nothing reads it. Planned home: the LNC Communication module's
  send path drops/refuses to queue `TLV_TAG_DATA_REPORT` frames while it's true,
  leaving keep-alive and events unaffected — matches Communication's own existing
  priority tiers (2.5: keep-alive > events > data reports), so "non-essential"
  == the lowest tier. Deferred until something actually sends
  `TLV_TAG_DATA_REPORT`, so this has real traffic to gate instead of an
  untestable guess. (Communication itself is live now — `communication_create()`
  is no longer stubbed out.)
- **Section 2.5's two "Instructions received" — "get measurement data for a
  time range" / "get events for a time range" — now implemented.**
  `TLV_TAG_QUERY_DATA` routes to `log_on_frame()` (`log.c`), `TLV_TAG_QUERY_EVENTS`
  routes to `event_on_frame()` (`event.c`) — replacing the single `query_on_frame()`
  stub that used to handle both. Both use an identical provisional
  `query_range_payload_t` request format (12 bytes: start+end, each
  year/month/date/hour/min/sec — year is an offset from 2000, same convention
  as `time_payload_t`/`keepalive_payload_t`), duplicated locally in each file
  per this codebase's own convention rather than a shared header.
  - **Design choice: `TLV_TAG_QUERY_RECORD` forwards the whole matching
    stored line as-is**, not a re-encoded binary struct — Log's/Event's
    stored lines (`LOG1..7.TXT`, `EVENTS.TXT`) are already complete,
    self-describing text (`"[2026-09-06 14:30:00] temp=25C hum=64% ..."`),
    so re-parsing them into a separate binary shape would be pure extra
    complexity for no benefit. This is why `COMM_MAX_VALUE` in
    `communication.c` was raised from 32 to 96 bytes — worst observed line
    (a mode-change event) is ~87 bytes.
  - Matching is done by parsing each line's own `[timestamp]` prefix back
    into six fields (`sscanf`) and packing both the request's range and
    the line's timestamp into one comparable `uint64_t`
    (`year*100+month, *100+date, ...` chained) rather than six separate
    field comparisons.
  - `SDFatFS_ForEachLine()` (new, in `sdfatfs.c`/`.h`) generalizes the
    existing `SDFatFS_PrintFile()` read loop to hand each line to a
    caller-supplied callback instead of always `printf`-ing it.
  - `log_on_frame()` searches **all 7** `LOG1..7.TXT` slots unconditionally
    on every `QUERY_DATA` — slots are keyed by weekday, not calendar date,
    and get overwritten every 7 days, so there's no reliable way to know
    which calendar dates a given slot currently holds without just reading
    it (`FR_NO_FILE` for an unused slot is an expected outcome, not an
    error).
  - `log_create()`'s signature changed to `log_create(Communication *comm)`
    (was `log_create(void)`) — needed a `comm` handle to reply, matching
    `event_create()`'s existing shape. `init.c`'s call site updated.
  - **Hardware-tested via `comm_test.cpp`'s new interactive query-send
    feature** (press Enter to fire both `QUERY_DATA`/`QUERY_EVENTS` with
    the widest possible range) — and this surfaced a real bug, now
    fixed: `comm_rx_task`'s stack (`communication.c`, `256 * 4` = 1024
    bytes) was sized for the old, shallow frame-routing handlers
    (a Flash read, an RTC call). `log_on_frame()`/`event_on_frame()`
    run **on that same task** (`comm_rx_task` → `comm_route_frame` →
    `log_on_frame` → `SDFatFS_ForEachLine` → FatFS internals →
    the per-line callback → `sscanf` → `comm_send`), a much deeper
    call chain than anything this task previously did, including a
    128-byte line buffer and a now-98-byte `comm_tx_item_t` (grown
    from 34 bytes when `COMM_MAX_VALUE` went 32→96 for this same
    feature). Confirmed as an actual stack overflow, not a sensor
    glitch: right after sending a test query, one Monitor round
    reported `mode=255` (`(uint8_t)MODE_UNKNOWN`, `monitor.c`'s
    just-booted sentinel value) with every measurement field zeroed —
    `struct Monitor`'s entire state had been reset to its
    `monitor_create()`-time initial values, then self-corrected on the
    very next round. A real sensor fault can't also reset Monitor's
    own internal state; only something overwriting `g_monitor`'s
    memory out from under it explains both symptoms together — the
    classic signature of a stack overflow corrupting adjacent static
    memory. Fixed by raising `rx_task_attr.stack_size` to `256 * 16`
    (4096 bytes, 4x). `comm_tx_task` (which calls `tlv_encode()` with
    a 261-byte `TLV_MAX_FRAME` buffer, but no comparably deep call
    chain) was left at `256 * 4` — not implicated, no symptom observed
    there.
  - **A second, independent bug found the same way, via `EVENTS.TXT`'s
    own history after the stack fix**: a `mode UNKNOWN -> ERROR
    (temp=0C hum=0%)` line with no boot event anywhere near it (Monitor
    had already produced several good readings since the last real
    boot) — same corruption signature as above, but this time with no
    stack overflow to blame. Root cause: `sdfatfs.c`'s `s_fs`/`s_fil`
    are shared static state with **no locking**, safe only as long as
    every caller was serialized onto one task by happenstance — which
    stopped being true the moment `log_on_frame()`/`event_on_frame()`
    started calling into this module from Communication's own RX task
    while Monitor/Log/Event keep writing from their own independent
    task schedules. Two tasks can now genuinely call in at the same
    time. **Fixed**: added `s_sd_lock` (an `osMutexId_t`), acquired at
    the top of every public function in `sdfatfs.c` and released before
    every return path. New `SDFatFS_Init()` creates it — called once
    from `main.c`'s `RTOS_THREADS` section (needs `osKernelInitialize()`
    to have already run, same requirement `communication_create()`'s
    queues/semaphores already follow — an `osMutexNew()` call placed
    any earlier, e.g. in `USER CODE BEGIN 2`, would be wrong), before
    `communication_create()`/`init_create()`, since `init_create()`
    itself writes to `EVENTS.TXT` via `event_startup()`.
  - **Correction found after the mutex fix, via a clean-SD-card test with
    no query ever sent**: the corruption still happened (`g_monitor`
    reset to its just-booted state ~27s after boot — same signature,
    confirmed by a paired `TLV_TAG_MODE_CHANGE UNKNOWN -> ERROR` frame,
    with `light`/`battery` also reading exactly 0%, which real ADC
    values never do). Since no query was ever sent, `log_on_frame()`/
    `event_on_frame()` never ran and `comm_rx_task` was never involved
    — proving the stack-overflow and the missing-mutex fixes above,
    while real bugs worth having fixed, were **not** this bug's actual
    root cause; they'd just coincided with the only sessions that
    exercised the system hard enough to trigger it. **Real root cause**:
    `monitor.c`'s own task stack (`256 * 4` = 1024 bytes) was never
    adjusted, and Monitor's task is the deepest one in the firmware on
    a mode-change round — `monitor_task()` calls both `log_write()` (its
    own SD-card write) and `event_mode_changed()`, which does a *second*
    SD-card write (`write_events_file()`) and calls `comm_send()` (now a
    98-byte `comm_tx_item_t`, same `COMM_MAX_VALUE` growth as before) —
    all nested on top of DHT11's bit-banging locals, two ADC reads, and
    several `snprintf` line buffers. Fixed by raising
    `monitor_create()`'s `stack_size` to `256 * 16` (4096 bytes), same
    target as `comm_rx_task`. `objectdetection.c`'s task has the same
    risk profile (`event_object_detected()`/`cleared()` also do a full
    SD write plus `comm_send()`) and was bumped the same way.
    `keepalive.c`'s task doesn't touch the SD card but does call
    `comm_send()`, so it was bumped too (`256 * 8`, lower risk, cheap
    precaution). `watchdog.c` only calls `HAL_IWDG_Refresh()` — no
    `comm_send()`, no SD access, genuinely shallow — left at `256 * 4`.
    **Confirmed fixed on hardware**: with all four stack bumps plus the
    SD-card mutex in place, `g_monitor` corruption has not recurred
    across multiple clean-SD-card boots, several minutes of steady
    running, and a full `QUERY_DATA`/`QUERY_EVENTS` round trip.
  - **A fifth thing this surfaced, not a new bug**: raising four task
    stacks by several KB each pushed total dynamic RAM demand past
    `configTOTAL_HEAP_SIZE` (`FreeRTOSConfig.h`, was `16000` bytes,
    `heap_4.c`) — CMSIS-RTOS2 allocates every task's stack *from* that
    one shared heap (`pvPortMalloc()`), not as separate reserved memory,
    so growing several stacks at once is a real hit against the same
    budget everything else (queues, semaphores, mutexes) draws from too.
    Total task-stack demand alone reached ~17.9KB, already over budget
    before counting anything else. Symptom on hardware matched exactly:
    a ~17-minute run of `startup (watchdog reset: yes)` events every
    4-21 seconds — some `_create()` call's `pvPortMalloc()` returning
    `NULL`, hitting `Error_Handler()`'s infinite loop *before*
    `osKernelStart()` ever ran, so Watchdog's task never started
    refreshing IWDG and it kept timing out (~4.096s) and forcing a
    reset, repeatedly. **Fixed**: `configTOTAL_HEAP_SIZE` raised to
    28000 bytes (confirmed safe — the L476RG has 96KB of `SRAM1` where
    all of `.data`/`.bss`/this heap/the main-ISR-stack live, per
    `STM32L476RGTX_FLASH.ld`; even after this increase, ~59KB of `SRAM1`
    remains free, plus an entirely untouched 32KB `SRAM2` region this
    project doesn't use). Confirmed on hardware: the reset loop stopped
    the moment this was reflashed, and the board has run clean since.

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
  swappable — see the "Ethernet-simulation gateway" note above (now
  requested — the transport-interface refactor described there is next).
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

Actual order so far deviated from the original plan below (built
bottom-up from what was independently testable on hardware first,
without waiting for Communication to be live) — done: **Monitor**,
**Event**, **Log**, **Init**, **Configuration**, **Object Detection**
(core state machine, sonar sound, breathing LED all built and
hardware-confirmed; buzzer contention bug still open, see section 7),
**Keep-Alive**, **Watchdog**. All nine LNC modules are now built.

**Keep-Alive** — `keepalive.c/.h`: its own task, `osDelayUntil` every
6 s, no hardware timer (matches this section's already-decided design
for Monitor/Keep-Alive/Watchdog). Reads Monitor's latest measurement +
mode via a new `monitor_get_latest()` getter (Monitor's `struct
Monitor` gained a full `last_data` cache plus an `osMutexId_t` guarding
it as a pair, since this is the first cross-task read in this codebase
spanning more than one field). Sends `TLV_TAG_KEEP_ALIVE` with a
provisional packed payload (timestamp + measurement + mode, no `dow`)
— same "nothing on the CC side parses this yet" status as `event.c`'s
`mode_change_payload_t` / `init.c`'s `time_payload_t`. Wired into
`init_create()`, after `objdet_create()`. **Hardware-confirmed**: a
`makefile` was added for `CentralComputer/src/communication/` (built
`comm_test` clean against the current `tlv.c`/`tlv.h`) and the full
round trip was observed live — `TLV_TAG_KEEP_ALIVE` frames decoding
correctly (timestamp, measurement, mode all sane), plus a real
`TLV_TAG_MODE_CHANGE` (Error→Normal) and `TLV_TAG_OBJECT_DETECTED`/
`OBJECT_CLEARED` frames, all field-correct on the CC side. This also
surfaced a real bug, now fixed: `sdfatfs.c`'s `printf()` calls (SD
card status/write messages) share `USART2` with Communication's real
TLV traffic — harmless while Communication was stubbed, but once
`communication_create()` went live, mixing human-readable debug text
into the same wire as binary TLV frames corrupted the stream (visible
as garbled binary mixed with readable text on the console).
`SDFatFS_SaveData()`'s and `SDFatFS_DeleteFile()`'s `printf()` calls
(the two functions actually on the live path, via `event.c`/`log.c`)
are now commented out, not deleted, so they're easy to re-enable for
future debugging with Communication disabled. `SDFatFS_PrintFile()`/
`ListFiles()` are unused by any module and were left untouched.
`TLV_TAG_TIME_SYNC_REQUEST` (`0x16`) is still silently dropped by the
CC's `communication.cpp`'s `route_frame()` (no case for it) — known,
not yet fixed, harmless since nothing replies to it yet either.

**Watchdog** — `watchdog.c/.h`: uses **IWDG, not WWDG** — IWDG is
clocked from `LSI`, independent of the main system clock, so it still
protects the system even if the main clock itself is what's broken;
WWDG is clocked from `PCLK1` and would be compromised right along with
it. Section 2.9 only asks for a plain "refresh on schedule" liveness
check, not WWDG's early-refresh-also-resets window behaviour, which
this project has no use for. Its own task, `osDelayUntil` every 1000ms,
no hardware timer (same already-decided design). Wired into
`init_create()`, after `keepalive_create()`. Init already handles
reporting whether the last boot was a WD reset (built ahead of
Watchdog itself, reading the passive `RCC_FLAG_IWDGRST` flag, which
works with or without Watchdog actually running the timer).
**Needs a CubeMX step not yet done**: IWDG must be activated in the
`.ioc` (System Core → IWDG) with its timeout set to ~4000ms, which
generates `hiwdg` (already `extern`'d in `main.h`) and calls
`MX_IWDG_Init()` from `main()`. Until that's done, this won't link.
`HAL_IWDG_Init()` both configures *and* starts the countdown
immediately, before `osKernelStart()` even runs — so the 4s timeout
also has to comfortably cover the rest of `main()`'s boot sequence
(the other modules' `create()` calls, SD card mounts, Flash reads)
before this task's first refresh actually executes; 1000ms refresh
against a 4000ms timeout gives 4x margin, generous against both LSI's
~5% inaccuracy and normal scheduling jitter, but if the board resets
repeatedly right at boot once this is flashed, an unexpectedly slow
boot sequence colliding with this timeout is the first thing to check.
Not yet hardware-tested (blocked on the CubeMX step above).

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