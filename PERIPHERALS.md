# LNC peripheral & timer allocation

Snapshot as of 2026-09-06 (branch `objectDetection`). Update this file
whenever a peripheral gets claimed or freed — check it before picking a
timer/peripheral for a new module instead of assigning one ad hoc.

## Timers

| Timer | Owner | What it does | Config |
|---|---|---|---|
| `TIM2` | DHT11 (Monitor), read-only by Object Detection | Free-running 1µs counter for DHT11 bit-bang timing; Object Detection reads it (doesn't reset it) for edge-speed rejection | Prescaler=79 → 1MHz, Period=max (32-bit) |
| `TIM3` | Buzzer (Event) | PWM tone (`CH1`/`PB4`) + its own update-IT drives note/duration timing for both the alarm siren and sonar ping | Prescaler=79 → 1MHz, per-note Period/Pulse |
| `TIM5` | Object Detection | Base timer + IT only (no PWM) — 10s presence-timeout countdown, reset on confirmed edges | Prescaler=7999, Period=99999 → 10.000s @ 10kHz |
| `TIM6` | **nobody (reserved)** | CubeMX-initialized but not `extern`'d in `main.h`, not touched by any module — earmarked as Object Detection's future dedicated timer (to stop sharing `TIM2` with DHT11), never actually wired up | Prescaler=0, Period=65535 (CubeMX defaults, untuned) |
| `TIM8` | SonarLed (Event) | PWM only (`CH4`/`PC9`), no interrupt — breathing red LED, duty nudged from a task every 30ms | Prescaler=7, Period=999 → 10kHz |
| `TIM1`, `TIM4`, `TIM7`, `TIM15`, `TIM16`, `TIM17`, `LPTIM1/2` | — | Not configured in CubeMX at all | fully free |

Monitor, Keep-Alive, and Watchdog's refresh all use `osDelayUntil` in
their own task loop instead of a hardware timer (CLAUDE.md section 9) —
none of the three general-purpose-timer-free periodic tasks need one.

## Other peripherals

| Peripheral | Owner | Notes |
|---|---|---|
| `ADC1` | Monitor | Battery voltage, `PA0`, 12-bit, on-demand poll |
| `ADC2` | Monitor | Light, `PA1`, 8-bit, on-demand poll |
| `I2C3` | Init/RTC sync | DS1307 external RTC, `PC0`(SCL)/`PC1`(SDA) |
| `SPI1` | Log/Event (FatFS) | SD card, CS on `PB6` |
| `USART2` | *contested* | Reserved exclusively for Communication per the transport rule, but Communication is still stubbed out (`communication_create()` commented out in `main.c`) — right now `printf` (via `syscalls.c`) is borrowing it for debug output. Once Communication is un-stubbed, that has to stop — only Communication may touch `huart2`. |
| `RTC` (internal) | Monitor/Event/Log timestamps | Currently clocked from `LSI` (poor accuracy, ~±5%, source of a known drift issue) — `LSE` is pin-locked (`PC14`/`PC15`) but not yet enabled in `RCC_OscInitStruct`/`RTCClockSelection` |
| `IWDG` | Watchdog | Refresh-only from software (`watchdog.c`, `osDelayUntil` every 1000ms) — `HAL_IWDG_Init()` (in CubeMX-generated `MX_IWDG_Init()`) both configures and starts the countdown. **Not yet activated in the `.ioc`** — needs enabling in CubeMX (System Core → IWDG) with timeout ~4000ms before `watchdog.c` will link (`hiwdg` doesn't exist until then). `init.c` separately reads the passive `RCC_FLAG_IWDGRST` reset-cause flag, which needs no IWDG setup and works regardless. |
| EXTI | Event | `PB10` (line 10, shared `EXTI15_10`) = IR receiver; `PB3` (line 3, own vector) = button. No other lines used. |

## Plain GPIO

RGB status LED `PB13`(red)/`PB14`(blue)/`PB15`(green), DHT11 `PB5`,
SD-CS `PB6`. `PA10` (`BUTTON_D2`) is defined in `main.h` but unused —
the real stop-alarm button ended up on `PB3` instead, due to the
EXTI-line-10 conflict with the IR sensor on `PB10` (same pin *number*
can't be a live EXTI source on two ports at once).

## What's free for upcoming modules

- **Keep-Alive**: needs nothing from this table — per the already-decided
  design it's pure `osDelayUntil`, no timer, no new peripheral.
- **Watchdog**: needs `IWDG`, completely untouched so far, no conflict
  with anything above.
