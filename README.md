# coop — Cooperative “blocking” delays for STM32 (no RTOS)

`coop` lets you write familiar synchronous-looking code (delays and waits) **without freezing the rest of the firmware**.  
While a function is “blocking,” the library **pumps** your user callback so other logic continues to run.

**Core pieces**

- **CoopPump** — the central *pump*. You register a function that is called regularly.
- **CoopDelay** — delays and timeouts that **pump** inside their wait loops.
- **ReentryGuard** — tiny per-function or per-block guard to prevent re-entry when the pump re-enters your code.

Works great on STM32 and similar MCUs. No threads, no dynamic allocations in the hot path, no RTOS.

---

## Dependencies

- **[StmTimeLib](https://github.com/shpegun60/StmTimeLib)** — provides `Tick::now()` and `TickITimer<>`.
- C++17 toolchain for ARM (e.g., `arm-none-eabi-g++`). Recommended: `-std=gnu++17`.

> Tip: on bare-metal builds you can use `-fno-threadsafe-statics`.

---

## Installation

Add `coop/` headers to your include path and link in **StmTimeLib**.  
All headers support both `#pragma once` and classic include guards.

```
your_project/
  |-- src/
  |-- libs/
       |-- coop/
            |-- CoopPump.h
            |-- CoopDelay.h
            |-- reentry_guard.h
  |-- libs/StmTimeLib/...
```

---

## Quick start

### 1) Register the pump and run your main loop

```cpp
#include "coop/CoopPump.h"
#include "coop/CoopDelay.h"
#include "coop/reentry_guard.h"
#include "StmTimeLib/Tick.h"   // from StmTimeLib

struct App {
    void proceed(u32 now) {
        // non-blocking work here
    }
} app;

int main() {
    // HAL_Init(); SystemClock_Config(); peripherals...

    // Register the pump: this will be called during waits
    CoopPump::setPump([&](u32 now, bool light){
        app.proceed(now);
    });

    // Your main loop keeps calling proceed as usual
    while (true) {
        app.proceed(Tick::now());
        // other lightweight work...
    }
}
```

### 2) A “blocking” delay that doesn’t freeze the app

```cpp
void do_blocking_task() {
    COOP_REENTRY_GUARD();        // cut re-entry for this function

    // Sleep 1000 ms but keep the system alive
    CoopDelay::delay_ms<1000>();

    // ...do whatever comes after the delay
}
```

### 3) Wait for a condition with a timeout

```cpp
bool ok = CoopDelay::wait_until<2000>([] {
    return hardware_ready();     // true → exit, otherwise keep pumping
});
if (!ok) {
    // handle timeout
}
```

---

## API Overview

### CoopPump

```cpp
// Register the pump: called as CoopPump::pump(now, light)
void CoopPump::setPump(std::function<void(u32 now, bool light)> fn);

// Drive the pump (call it in your main loop and from CoopDelay)
void CoopPump::pump(u32 now, bool light);

// Internal RAII used during waits (exposed for advanced use)
struct CoopPump::Guard {
    Guard(); ~Guard();
    static bool inWait(); // indicates we are inside a wait loop
};
```

**Notes**

- Do **not** call `CoopPump::pump()` from ISRs. Set flags in ISRs and handle them in your pump callback.
- The `light` flag lets you skip heavy work during tight wait loops if you want.

### CoopDelay

```cpp
// Fixed delay (compile-time constant)
template<u32 Ms> static void CoopDelay::delay_ms();

// Variable delay
static void CoopDelay::delay_ms(u32 ms);

// Wait for a predicate with a timeout (compile-time)
template<u32 TimeoutMs, class Pred>
static bool CoopDelay::wait_until(Pred&& ready);

// Wait for a predicate with a timeout (runtime)
template<class Pred>
static bool CoopDelay::wait_until(Pred&& ready, u32 timeoutMs);
```

Under the hood, wait loops look like this:

```cpp
CoopPump::Guard g;
while (!ready()) {
    if (timer.isExpired()) return false;
    CoopPump::pump(Tick::now(), true);   // <- keeps the app alive
}
```

### ReentryGuard (anti re-entry)

Header: `coop/reentry_guard.h`

**One-liner macros**

```cpp
COOP_REENTRY_GUARD();        // for void functions
COOP_REENTRY_GUARD(false);   // for non-void: return false on re-entry
```

**Manual mode (skip only a block, not the whole function)**

```cpp
COOP_REENTRY_ENTER(g);
if (!g.is_reentered()) {
    // protected block
}
```

**Per-instance guard for class methods**

```cpp
class Driver {
    mutable ReentryGuard rg_activate; // one guard per instance
public:
    void activate() {
        ReentryGuard::Scope s{rg_activate};
        if (s.is_reentered()) return;
        // ...
    }
};
```

> The macros generate unique names (using `__COUNTER__/__LINE__`) and deliberately **do not** use `do { } while (0)` so the RAII object lives until end of function/block.

---

## Usage patterns (no coroutines)

### A) Multiple “blocking” sections in a single `proceed()`

Protect each section separately so they don’t starve one another when the pump re-enters your code:

```cpp
void App::proceed(u32 now) {
    // Section A
    {
        COOP_REENTRY_ENTER(a);
        if (!a.is_reentered()) {
            CoopDelay::delay_ms<200>();
            // ...A work
        }
    }

    // Section B
    {
        COOP_REENTRY_ENTER(b);
        if (!b.is_reentered()) {
            bool ok = CoopDelay::wait_until<500>([]{ return readyB(); });
            // ...B work
        }
    }

    // Section C
    {
        COOP_REENTRY_ENTER(c);
        if (!c.is_reentered()) {
            CoopDelay::delay_ms<50>();
            // ...C work
        }
    }
}
```

### B) Per-instance “command” with a hardware wait

```cpp
class IActiveBalancer {
public:
    void activateControl(Cmd::Direction cmd, u16 pwm, u8 cell) {
        ReentryGuard::Scope s{rg_activate};
        if (s.is_reentered()) return;

        if (cmd != Cmd::Off && cell != 0) {
            const auto dir = cmd == Cmd::Charge ? HrtimFrontend::Charge
                                                : HrtimFrontend::Discharge;
            ctrl->setMode(dir);
            ctrl->setPwm(pwm);
            ctrl->turnOn(true);
            power->start(true);
            sw_matrix->write(true);
            swm->setCell(cell);
        } else {
            ctrl->turnOn(false);
            power->start(false);
            sw_matrix->write(false);
            swm->setCell(0);
        }

        const bool ok = CoopDelay::wait_until<2000>([this]{ return swm->done(); });
        if (!ok) {
            // log/set fault/retry
        }
    }

private:
    mutable ReentryGuard rg_activate;
    // ctrl, power, sw_matrix, swm...
};
```

### C) ISR-safe flag + pump-side handling

```cpp
static volatile uint32_t rx_flag = 0;

// ISR (do minimal work)
extern "C" void USARTx_IRQHandler() {
    // read status/DR quickly...
    rx_flag = 1;
}

// Pump callback (registered via CoopPump::setPump)
void pump_handler(u32 now, bool light) {
    if (rx_flag) {
        rx_flag = 0;
        process_rx();         // full processing outside ISR
    }
    // other periodic tasks...
}
```

### D) Short time slices to avoid monopolizing CPU (optional)

If a section might run long, give it a tiny budget so others get CPU time too:

```cpp
static inline bool budget_elapsed(u32 start_ms, u32 budget_ms) {
    return (Tick::now() - start_ms) >= budget_ms;
}

void heavy_section() {
    COOP_REENTRY_ENTER(g);
    if (g.is_reentered()) return;

    const u32 slice_start = Tick::now();
    while (!done()) {
        CoopPump::pump(Tick::now(), true);
        if (budget_elapsed(slice_start, 2)) return; // come back next tick
    }
}
```

---

## FAQ / Troubleshooting

**Q: My wait never finishes.**  
A: Ensure the timer is started before the loop (`TickITimer::next()` is called inside `CoopDelay`). Also make sure nothing globally short-circuits your `proceed()` on re-entry; otherwise the predicate won’t change.

**Q: Can I call the pump from an ISR?**  
A: No. In ISRs set flags only. Handle them in the pump callback.

**Q: I see stack growth/recursion during waits.**  
A: You’re likely missing a re-entry guard on a section that calls delays/waits. Add `COOP_REENTRY_GUARD()` (or per-block `COOP_REENTRY_ENTER(...)`).

**Q: Why GNU++17?**  
A: The one-liner macro uses an empty `__VA_ARGS__` for `return;` vs `return <expr>;`. GCC/Clang accept this in `-std=gnu++17`.

---

## License

See `LICENSE` in the repository.

---

## Credits

- **[StmTimeLib](https://github.com/shpegun60/StmTimeLib)** for time primitives.
- The “cooperative blocking” idea works because waits pump your logic. Register the pump and call it regularly.
