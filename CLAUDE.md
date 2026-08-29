

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
- No module calls the UART API directly except Communication.
- Keep TLV encode/decode in one shared place so the LNC and the Central Computer cannot
  drift apart.

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
 No newline at end of file