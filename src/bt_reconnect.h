#pragma once
// Opt-in reconnect of a paired Bluetooth audio device that dropped out: Windows does not always bring
// a classic (A2DP / Hands-Free) headset back by itself. The retry policy is pure and portable.
#include <string>

namespace bt_reconnect {
constexpr int kEagerAttempts = 10;
constexpr unsigned long long kRetryMs = 5 * 60 * 1000;

struct Retry {
    int attempts = 0;               // since the device went missing, capped at kEagerAttempts
    unsigned long long lastMs = 0;  // time of the last attempt
};

// Asked on every poll while the device is missing; true = try now. Every poll for the first
// kEagerAttempts (it usually just walked back into range), then at most once per kRetryMs, so a
// headset that is switched off or in use on a phone is not paged all day.
inline bool Due(Retry& r, unsigned long long nowMs) {
    if (r.attempts >= kEagerAttempts && nowMs - r.lastMs < kRetryMs) return false;
    if (r.attempts < kEagerAttempts) ++r.attempts;
    r.lastMs = nowMs;
    return true;
}

// key is a DeviceStatus.key of the form "bt:{container id}"; anything else does nothing. Asks the
// Bluetooth audio driver to reconnect the device's unplugged audio endpoints, as the "Connect" button
// in Settings does. Returns at once (the endpoints come back a few seconds later) with the number of
// requests sent.
int Reconnect(const std::wstring& key);
}  // namespace bt_reconnect
