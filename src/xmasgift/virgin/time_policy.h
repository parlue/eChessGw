#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

// Small, platform-independent policy. Durations are milliseconds; no sleeps,
// book data or hardware-speed assumptions. This header has no engine state.
namespace VirginTime {
struct Budget { int64_t soft = 0, hard = 0; };
inline Budget allocate(int64_t remaining, int64_t increment, int movesToGo,
                       bool opening, int64_t fixedTime, int automaticTime,
                       int overhead) {
    constexpr int64_t maximum = std::numeric_limits<int64_t>::max() / 16384;
    const auto safe = [maximum](int64_t value) { return std::clamp<int64_t>(value, 0, maximum); };
    const int64_t reserve = std::clamp(overhead, 0, 5000);
    if (fixedTime > 0) {
        const int64_t hard = std::max<int64_t>(1, safe(fixedTime) - reserve);
        return {std::max<int64_t>(1, hard * 3 / 5), hard};
    }
    if (remaining < 0) {
        if (automaticTime <= 0) return {}; // Explicit depth/nodes; no hidden timer.
        const int64_t hard = std::max<int64_t>(1, automaticTime - reserve);
        return {std::max<int64_t>(1, hard * 2 / 5), hard};
    }
    remaining = safe(remaining); increment = safe(increment);
    if (remaining <= reserve) return {1, 1};
    const int moves = movesToGo > 0 ? std::clamp(movesToGo, 1, 60) : 30;
    const int64_t usable = remaining - reserve;
    const int64_t future = std::max<int64_t>(1, remaining - reserve * moves);
    int64_t soft = std::max<int64_t>(1, future / moves + increment * 3 / 4);
    soft = std::min(soft, std::max<int64_t>(1, moves == 1 ? usable * 3 / 4 : usable / 3));
    if (opening) soft = std::min<int64_t>(soft, 1500);
    int64_t hard = std::min(soft * 4, moves == 1 ? usable * 9 / 10 : usable / 2);
    if (opening) hard = std::min<int64_t>(hard, 3000);
    hard = std::max<int64_t>(1, hard);
    return {std::min(soft, hard), hard};
}

// Learn only from completed iterations. Instability earns more of the hard
// budget; a stable move/score spends less. The hard deadline never moves.
class Decision {
    uint32_t lastMove = 0;
    int lastScore = 0, stable = 0;
    int64_t previousElapsed = 0;
public:
    bool finish(Budget budget, uint32_t move, int score, int depth,
                int64_t elapsed, bool clearlyAhead) {
        const int64_t cost = std::max<int64_t>(1, elapsed - previousElapsed);
        previousElapsed = elapsed;
        const bool havePrevious = lastMove != 0;
        const bool changed = havePrevious && move != lastMove;
        const int fall = havePrevious ? std::max(0, lastScore - score) : 0;
        const int swing = havePrevious ? std::abs(lastScore - score) : 0;
        stable = havePrevious && !changed && swing <= 20 ? stable + 1 : 0;
        lastMove = move; lastScore = score;
        if (budget.hard <= 0 || depth < 2) return false;
        int factor = 1000;
        if (changed) factor += 700;
        factor += std::min(1000, fall * 12);
        if (swing >= 35) factor += 300;
        if (!changed && stable >= 3) factor = factor * 3 / 4;
        if (stable >= 6) factor = factor * 2 / 3;
        if (clearlyAhead && stable >= 2) factor = factor * 2 / 3;
        const int64_t target = std::clamp<int64_t>(budget.soft * factor / 1000,
                                                  std::min<int64_t>(100, budget.hard), budget.hard);
        if (elapsed >= target) return true;
        // Avoid starting a large iteration when even a modest cost increase
        // is unlikely to fit. Do not abandon a difficult search too early.
        return elapsed >= target / 2 && cost > (budget.hard - elapsed) / 2;
    }
};
}
