// Standalone retry-policy test; no Bluetooth device required.
#include "../src/bt_reconnect.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace bt_reconnect;
    const unsigned long long minute = 60 * 1000;
    Retry r;
    unsigned long long now = 1000;
    for (int i = 0; i < kEagerAttempts; ++i, now += minute) assert(Due(r, now));  // every poll at first
    assert(r.attempts == kEagerAttempts);
    const unsigned long long last = r.lastMs;
    assert(!Due(r, now));                    // one minute after the tenth attempt
    assert(!Due(r, last + kRetryMs - 1));
    assert(r.lastMs == last);                // a skipped poll does not push the next attempt back
    assert(Due(r, last + kRetryMs));         // then once every five minutes
    assert(!Due(r, last + kRetryMs + minute));
    assert(Due(r, last + 2 * kRetryMs));
    assert(r.attempts == kEagerAttempts);    // stays capped however long the device is away

    r = Retry{};                             // the device came back: start over
    assert(Due(r, now) && Due(r, now));      // eager attempts do not wait, even in the same millisecond
    assert(r.attempts == 2);

    Retry atBoot;                            // device already missing when the app starts
    assert(Due(atBoot, 0));

    std::puts("Bluetooth reconnect policy tests passed");
}
