/*
 * reentry_guard.h
 *
 *  Created on: Aug 25, 2025
 *      Author: admin
 */
#pragma once
#ifndef COOP_REENTRY_GUARD_H_
#define COOP_REENTRY_GUARD_H_

#include "macro.h"

// Utility to prevent re-entrant execution of the same function.
// Each function that includes this guard owns a static counter.
// Scope increments on entry and decrements on exit (RAII).
class ReentryGuard {
    _DELETE_COPY_MOVE(ReentryGuard);
public:
    ReentryGuard()  = default;
    ~ReentryGuard() = default;

    // RAII handle that blocks re-entry while alive.
    class Scope {
        _DELETE_COPY_MOVE(Scope);
    public:
        explicit Scope(ReentryGuard& guard) noexcept
            : g_(guard), reentered_(++g_.depth_ > 1) {}

        ~Scope() noexcept { --g_.depth_; }

        [[nodiscard]] bool is_reentered() const noexcept { return reentered_; }

    private:
        ReentryGuard& g_;
        const bool    reentered_;
    };

private:
    // Per-function recursion depth (0/1 is all you need).
    int depth_{0};

    // Nested classes are not automatically friends in C++,
    // so give Scope access to depth_.
    friend class Scope;
};

// ---------- Unique ID helpers ----------
#if defined(__COUNTER__)
#  define COOP_RG_UNIQUE_ID() __COUNTER__
#else
#  define COOP_RG_UNIQUE_ID() __LINE__
#endif /* defined(__COUNTER__) */

#define COOP_RG_CAT_(a,b) a##b
#define COOP_RG_CAT(a,b)  COOP_RG_CAT_(a,b)

// ---------- Implementation macros (internal) ----------
#define COOP_REENTRY_ENTER_IMPL(NAME, ID)                                 \
    static ReentryGuard COOP_RG_CAT(_rg_, ID);                            \
    ReentryGuard::Scope NAME{COOP_RG_CAT(_rg_, ID)}

#define COOP_REENTRY_GUARD_IMPL(ID, ...)                                  \
    COOP_REENTRY_ENTER_IMPL(COOP_RG_CAT(_scope_, ID), ID);                \
    if (COOP_RG_CAT(_scope_, ID).is_reentered()) return __VA_ARGS__

#define COOP_REENTRY_GUARD_OBJ_IMPL(ID, OBJ, ...)                               \
    ReentryGuard::Scope COOP_RG_CAT(_scope_, ID){ (OBJ) };                      \
    if (COOP_RG_CAT(_scope_, ID).is_reentered()) return __VA_ARGS__             \

// ---------- Public macros ----------
// If called without args -> `return;`
// If called with args   -> `return <args>;` (GNU++17 empty __VA_ARGS__ OK)
#define COOP_REENTRY_GUARD(...)  COOP_REENTRY_GUARD_IMPL(COOP_RG_UNIQUE_ID(), __VA_ARGS__)

// Manual mode: expose a Scope named `name` with .is_reentered()
#define COOP_REENTRY_ENTER(name)  COOP_REENTRY_ENTER_IMPL(name, COOP_RG_UNIQUE_ID())

#define COOP_REENTRY_GUARD_OBJ(OBJ, ...) COOP_REENTRY_GUARD_OBJ_IMPL(COOP_RG_UNIQUE_ID(), (OBJ), __VA_ARGS__)

#endif /* COOP_REENTRY_GUARD_H_ */
