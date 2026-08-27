# Submarine Monitoring System

An embedded monitoring system for a submarine, built on an STM32 Nucleo board with
two Linux programs on the other end of the wire.

The board reads environmental sensors, decides whether the submarine is in a
Normal, Warning or Error state, and reports to a Central Computer over UART.
A Ground Station asks the Central Computer for historical data and events.

All messages use a TLV (Tag, Length, Value) binary protocol shared by every part
of the system.

---

## Repository layout

| Folder | Runs on | Language | What it is |
|---|---|---|---|
| `LNC/` | STM32 Nucleo-L476RG | C | Firmware. The Local Node Controller end unit. |
| `CentralComputer/` | WSL Ubuntu | C++ | Talks to the board, stores data, serves the ground station. |
| `GroundStation/` | WSL Ubuntu | C++ | Requests logs and events for a time range. |
| `Shared/` | both | C | TLV protocol. Compiled into the firmware and the PC programs. |
| `docs/` | — | — | Specification, setup notes, diagrams. |

Everything lives in one repository on purpose. The TLV tag numbers are used by
the firmware and by both PC programs. Keeping them in one tree means a protocol
change is a single commit, so the two sides can never drift out of sync.

---

## Hardware

- STM32 Nucleo-L476RG
- 9-in-1 multifunctional expansion board — DHT11 (temperature + humidity),
  LM35, light sensor, potentiometer (stands in for battery voltage), RGB LED,
  buzzer, buttons
- Data Logging Shield v1.0 — microSD card and external RTC

---

## System overview

```
   Ground Station  <--- Ethernet --->  Central Computer  <--- UART --->  LNC (STM32)
   (data requests)                     (fleet manager)                   (sensors)
```

The LNC hosts the sensors and the object-detection component. The Central
Computer manages this end unit alongside others. The Ground Station never talks
to the board directly.

---

## The LNC firmware

Nine modules, each with one job:

| Module | Responsibility |
|---|---|
| Monitor | Samples sensors every 5 s, compares against configured limits, decides the mode |
| Object Detection | Watches the direction of travel, reports objects appearing and clearing |
| Event | Receives events, timestamps them, drives the LED and alarm, writes the event file |
| Log | Writes measurements to a daily file, keeps 7 days |
| Communication | UART gateway. Sends by priority: keep-alive, then events, then data |
| Configuration | Holds all limits, persists them in Flash, loads defaults on first boot |
| Init | Syncs time from the Central Computer, then starts everything |
| Keep-Alive | Sends a status message every 6 s |
| Watchdog | Refreshes the hardware watchdog timer |

### Operating modes

| Mode | Condition | LED | Alarm |
|---|---|---|---|
| Normal | every measurement in the Normal range | green | off |
| Warning | at least one in the Warning range, none in Error | yellow | off |
| Error | at least one in the Error range | red | on |

Entering Error also suppresses non-essential operations. The button stops the
alarm sound without changing the mode.

---

## Building

### PC side (WSL Ubuntu)

```bash
cd CentralComputer && make
cd GroundStation   && make
```

Binaries land in `build/`.

### Firmware

Open `LNC/` in STM32CubeIDE on Windows, build, flash over ST-LINK.

`Shared/` is added to the CubeIDE project as an extra source location and
include path, so the firmware and the PC programs compile the exact same
protocol code.

---

## Running

The board's USB gives Windows a virtual COM port. WSL2 is a VM and cannot see
Windows COM ports, so `usbipd` hands the device across. Only one side can hold
it at a time.

```powershell
stm win      # give the board to Windows, flash from CubeIDE
stm linux    # give the board to WSL, /dev/ttyACM0 appears
```

Then:

```bash
./build/central_computer /dev/ttyACM0
./build/ground_station
```

Full setup instructions: [`docs/wsl-stm32-uart-setup.md`](docs/wsl-stm32-uart-setup.md)

---

## Design decisions

**The transport sits behind an interface.** The serial port is reached through a
struct of function pointers, not by calling `read()` directly. One implementation
is the real port, another replays canned TLV messages from a buffer. The
specification requires a replaceable transport, and it also means the framing and
command layers are developed and tested with no board attached — which matters,
because flashing takes the USB device away from Linux.

**The protocol code is plain C99 with no dependencies.** No HAL, no `malloc`, no
`printf`, no file access. It only turns a struct into bytes and back. That is
what lets it compile for both ARM and x86 without a single `#ifdef`, and what
lets it be unit tested on a laptop.

**Reads are looped, not assumed.** `read()` returns whatever has arrived, not
what was asked for. Ask for 64 bytes, get 7. The framing layer keeps reading
until it holds a complete TLV message.

**Logs rotate at 7 days.** The board writes one file per day and deletes the
oldest on the eighth. Storage on the SD card is finite and the specification
only ever asks for a week of history.

---

## Status

Work in progress.
