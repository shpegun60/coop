#ifndef COOPPUMP_H
#define COOPPUMP_H

#include "basic_types.h"
#include <functional>

class CoopPump
{
public:
    using pump_t = std::function<void(const u32, const bool)>;

    static void setPump(pump_t fn) { _pump = std::move(fn); }
    static void reset() { _pump = nullptr; }
    static void pump(const u32 now, const bool light) {
        if (_pump) {
            _pump(now, light);
        }
    }

    struct Guard {
        Guard() { ++waitDepth; }
        ~Guard() { --waitDepth; }
        static bool inWait() { return waitDepth > 0; }
    private:
        static inline int waitDepth = 0;
    };

private:
    static inline pump_t _pump = nullptr;
};



/*
Example usage:

CoopPump::setPump([](uint32_t now, bool light){
    // cooperative logic here
});

{
    CoopPump::Guard g;
    while (!condition) {
        CoopPump::pump(current_time_ms(), true);
    }
}
*/

#endif // COOPPUMP_H

