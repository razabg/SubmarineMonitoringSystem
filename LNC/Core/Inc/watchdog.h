/*
 * watchdog.h - LNC Watchdog module (section 2.9)
 *
 * Refreshes the IWDG on schedule so no reset happens, as long as the
 * system is genuinely alive. Uses IWDG, not WWDG -- IWDG is clocked
 * from LSI, independent of the main system clock, so it still protects
 * the system even if the main clock itself is what's broken; WWDG is
 * clocked from PCLK1 and would be compromised right along with it.
 * Section 2.9 only asks for a plain "refresh on schedule" liveness
 * check, not WWDG's early-refresh-also-resets window behaviour.
 *
 * osDelayUntil, no hardware timer -- matches this project's
 * already-decided design for Monitor/Keep-Alive/Watchdog (CLAUDE.md
 * section 9).
 *
 * Init already handles reporting whether the last boot was caused by
 * a WD reset (check_and_clear_watchdog_reset() in init.c, reading the
 * passive RCC_FLAG_IWDGRST flag) -- that half works whether or not
 * this module is even running. This module only owns the refresh.
 *
 * ADT, matching monitor.c's/keepalive.c's shape: opaque handle, one
 * static instance.
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

typedef struct Watchdog Watchdog;

/* Creates Watchdog's task. Returns the handle, or NULL if the task
 * failed to create. */
Watchdog *watchdog_create(void);

/* Stops the task. Null-safe. Provided for ADT completeness -- NOTE:
 * once IWDG_Init() has run (in main()'s MX_IWDG_Init(), before this
 * task even starts), the countdown cannot be stopped in software
 * short of a full reset, so this only stops the *refreshing*, not the
 * IWDG itself. Not expected to be called in practice on real
 * hardware. */
void watchdog_destroy(Watchdog *w);

#endif /* WATCHDOG_H */
