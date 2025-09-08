/*
 * CoopDelay.h
 *
 *  Created on: Aug 13, 2025
 *      Author: admin
 */

#ifndef COOP_COOPDELAY_H_
#define COOP_COOPDELAY_H_

#include "cooppump.h"
#include "time/Tick.h"
#include "time/Dwt.h"
#include <utility>      // for std::forward

struct CoopDelay
{
	using tick_t = Tick::type_t;
	using cyc_t  = Dwt::type_t;

	template <tick_t DurationMs>
	static void delay_ms() noexcept {
		static_assert(DurationMs > 0, "DurationMs must be > 0");
		TickITimer<DurationMs> timer;
		{
			CoopPump::Guard g;
			do {
				CoopPump::pump(Tick::now(), true);
			} while(!timer.isExpired());
		}
	}

	static void delay_ms(const tick_t durationMs) noexcept {
		TickITimer<> timer{durationMs};
		{
			CoopPump::Guard g;
			do {
				CoopPump::pump(Tick::now(), true);
			} while(!timer.isExpired());
		}
	}


	template <cyc_t Cycles>
	static void delay_cycles() noexcept {
		static_assert(Cycles > 0, "Cycles must be > 0");
		DwtITimer<Cycles> timer;
		{
			CoopPump::Guard g;
			do {
				CoopPump::pump(Tick::now(), true);
			} while(!timer.isExpired());
		}
	}

	static void delay_cycles(const cyc_t duration) noexcept {
		DwtITimer<> timer{duration};
		{
			CoopPump::Guard g;
			do {
				CoopPump::pump(Tick::now(), true);
			} while(!timer.isExpired());
		}
	}


	template <tick_t TimeoutMs, class Pred>
	static bool wait_until(Pred&& ready) noexcept {
		static_assert(TimeoutMs > 0, "TimeoutMs must be > 0");
		TickITimer<TimeoutMs> t;
		{
			CoopPump::Guard g;
			do {
				if (t.isExpired()) {
					return false;
				}
				CoopPump::pump(Tick::now(), true);
			} while(!std::forward<Pred>(ready)());
		}

		return true;
	}

	template <class Pred>
	static bool wait_until(Pred&& ready, const tick_t timeoutMs) noexcept {
		TickITimer<> t{timeoutMs};
		{
			CoopPump::Guard g;

			do {
				if (t.isExpired()) {
					return false;
				}
				CoopPump::pump(Tick::now(), true);
			} while(!std::forward<Pred>(ready)());
		}

		return true;
	}
};

#endif /* COOP_COOPDELAY_H_ */
