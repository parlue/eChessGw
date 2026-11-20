// Virgin - UCI chess engine
// Transposition table.
#pragma once

#include "types.h"

#include <cstddef>
#include <vector>
#include <array>
#include <mutex>
#include <memory>

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

// 16 bytes, deliberately: a cache line holds four entries, and the power of two
// table size doubles the number of entries compared to the previous 24 byte
// layout (at 32 MB: 2^21 instead of 2^20). To get there every field has to be
// packed - depth and bound share one byte (6 bits depth, 2 bits bound) - and
// scores are int16, which is enough for everything this engine stores
// (mate is +-31000, SCORE_NONE -32000).
struct TTEntry {
    Key     key;         // full key, verified on probe
    Move    move;        // best move
    int16_t score;       // score from the node's point of view
    int16_t eval;        // static evaluation of the node
    uint8_t depthBound;  // depth (0..63) in the upper 6 bits, bound in the lower 2
    uint8_t age;         // search generation
};

static_assert(sizeof(TTEntry) == 16, "TTEntry must stay 16 bytes");

// -1 (quiescence) and 0 behave the same for the depth cutoff, so they share a
// code. 64 is the largest depth this engine searches, the clamp is defensive.
inline uint8_t packDepthBound(int depth, int bound) {
    const int d = depth < 0 ? 0 : (depth > 63 ? 63 : depth);
    return uint8_t((d << 2) | (bound & 3));
}
inline int depthOf(const TTEntry& e) { return int(e.depthBound) >> 2; }
inline int boundOf(const TTEntry& e) { return int(e.depthBound) & 3; }

class TranspositionTable {
public:
    void release(); // Only while all search workers are stopped.
    void resize(size_t megabytes);
    void resizeKiB(size_t kilobytes);
    void clear();
    void newSearch() { ++generation; }

    bool     probe(Key key, TTEntry& snapshot) const;
    void     store(Key key, Move move, Score score, Score eval, int depth, Bound bound, int ply);

    int    hashfull() const;
    size_t sizeMb() const { return (tableCount * sizeof(TTEntry)) / (1024 * 1024); }
    size_t entries() const { return tableCount; }

private:
    // Protect complete entries; readers use a snapshot after releasing the lock.
    #ifdef VIRGIN_EMBEDDED
    mutable std::array<std::mutex, 1> locks;
#else
    mutable std::array<std::mutex, 256> locks;
#endif
    // Embedded allocation must report failure without throwing: the C3 SDK's
    // unwinder cannot safely recover from an allocation exception in a task.
    std::unique_ptr<TTEntry[]> table;
    size_t tableCount=0;
    size_t               entryMask = 0;
    uint8_t              generation = 0;
};

extern TranspositionTable TT;

// Mate scores are stored relative to the node, not to the root.
inline Score scoreToTT(Score s, int ply) {
    if (s == SCORE_NONE)
        return SCORE_NONE;
    if (s > SCORE_MATE_BOUND)
        return Score(s + ply);
    if (s < -SCORE_MATE_BOUND)
        return Score(s - ply);
    return s;
}

inline Score scoreFromTT(Score s, int ply) {
    if (s == SCORE_NONE)
        return SCORE_NONE;
    if (s > SCORE_MATE_BOUND)
        return Score(s - ply);
    if (s < -SCORE_MATE_BOUND)
        return Score(s + ply);
    return s;
}
