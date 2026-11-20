// Virgin - UCI chess engine
// Transposition table implementation: four-entry buckets with depth and
// generation based replacement.
#include "tt.h"

#include <algorithm>
#include <cstring>
#include <new>

TranspositionTable TT;

void TranspositionTable::resize(size_t megabytes) {
    resizeKiB(std::max<size_t>(megabytes, 1) * 1024);
}

void TranspositionTable::resizeKiB(size_t kilobytes) {
    const size_t bytes = std::max<size_t>(kilobytes, 1) * 1024;
    const size_t count = std::max<size_t>(bytes / sizeof(TTEntry), 256);

    // Round down to a power of two so that indexing is a simple mask.
    size_t powerOfTwo = 1;
    while (powerOfTwo * 2 <= count)
        powerOfTwo *= 2;

    table.reset(new (std::nothrow) TTEntry[powerOfTwo]{});
    tableCount=table ? powerOfTwo : 0;
    if (!table) { entryMask=0;generation=0;return; }
    entryMask = powerOfTwo - 1;
    generation = 0;
}

void TranspositionTable::clear() {
    if (table) std::memset(table.get(), 0, tableCount * sizeof(TTEntry));
    generation = 0;
}

namespace {
int replacementValue(const TTEntry& entry, uint8_t generation) {
    if (boundOf(entry) == BOUND_NONE)
        return -10000;
    const int age = int(uint8_t(generation - entry.age));
    return depthOf(entry) - age * 8;
}
}

bool TranspositionTable::probe(Key key, TTEntry& snapshot) const {
    const size_t bucket = (size_t(key) & entryMask) & ~size_t(3);
    const std::lock_guard<std::mutex> guard(locks[(bucket >> 2) & (locks.size() - 1)]);
    const TTEntry* candidate = &table[bucket];
    for (size_t i = 0; i < 4; ++i) {
        const TTEntry& entry = table[bucket + i];
        if (entry.key == key && boundOf(entry) != BOUND_NONE) {
            snapshot = entry;
            return true;
        }
        if (replacementValue(entry, generation) < replacementValue(*candidate, generation))
            candidate = &entry;
    }
    snapshot = *candidate;
    return false;
}

void TranspositionTable::store(Key key, Move move, Score score, Score eval, int depth, Bound bound,
                               int ply) {
    const size_t bucket = (size_t(key) & entryMask) & ~size_t(3);
    const std::lock_guard<std::mutex> guard(locks[(bucket >> 2) & (locks.size() - 1)]);
    TTEntry* entry = &table[bucket];
    for (size_t i = 0; i < 4; ++i) {
        TTEntry* slot = &table[bucket + i];
        if (slot->key == key && boundOf(*slot) != BOUND_NONE) {
            entry = slot;
            break;
        }
        if (replacementValue(*slot, generation) < replacementValue(*entry, generation))
            entry = slot;
    }
    // Leaf caching must not displace a deeper result for the same position,
    // or flood out deeper entries from this search when a bucket is full.
    if (depth == 0 && boundOf(*entry) != BOUND_NONE && depthOf(*entry) > 0
        && (entry->key == key || entry->age == generation))
        return;
    if (entry->key == key && entry->age == generation && depthOf(*entry) > depth + 2
        && bound != BOUND_EXACT)
        return;
    if (entry->key != key || bound == BOUND_EXACT || depth + 2 > depthOf(*entry)
        || entry->age != generation || boundOf(*entry) == BOUND_NONE) {
        if (!move && entry->key == key)
            move = entry->move;
        const auto packScore = [](Score s) {
            return int16_t(std::max<Score>(-32767, std::min<Score>(32767, s)));
        };
        entry->key = key;
        entry->move = move;
        entry->score = packScore(scoreToTT(score, ply));
        entry->eval = packScore(eval);
        entry->depthBound = packDepthBound(depth, bound);
        entry->age = generation;
    }
}

int TranspositionTable::hashfull() const {
    if (!table)
        return 0;
    int used = 0;
    const size_t sample = std::min<size_t>(tableCount, 1000);
    for (size_t i = 0; i < sample; ++i) {
        const std::lock_guard<std::mutex> guard(locks[(i >> 2) & (locks.size() - 1)]);
        if (boundOf(table[i]) != BOUND_NONE && table[i].age == generation)
            ++used;
    }
    return int(used * 1000 / int(sample));
}

void TranspositionTable::release() {
    table.reset();tableCount=0;entryMask = 0; generation = 0;
}
