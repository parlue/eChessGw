// Virgin - UCI chess engine
// Search: iterative deepening PVS with transposition table, quiescence search,
// null move pruning, late move reductions/prunings, killers, countermoves and
// history heuristics. Strength handicaps (Elo) and style selection are applied
// on top of it.
#pragma once

#include "position.h"

#include <cstdint>
#include <atomic>
#include <iosfwd>
#include <vector>

struct SearchLimits {
    std::atomic<bool>* cancellation = nullptr; // UCI request cancellation across serial analysis/main searches.
    bool restrictRootMoves = false;
    std::vector<Move> rootMoves; // Legal root candidates from UCI searchmoves.
    int      multiPV = 1; // internal root candidate count, independent of strength handicaps
    int      depth = 64;
    uint64_t nodes = 0;
    int64_t  movetime = 0;
    int      automaticTime = 0; // Bare go: maximum, not a fixed waiting time.
    int      moveOverhead = 50;
    bool     openingVariety = false;
    int64_t  wtime = -1, btime = -1;
    int64_t  winc = 0, binc = 0;
    int      movestogo = 0;
    bool     infinite = false;
};

struct SearchCandidate {
    Move move = 0;
    Score score = 0;
    std::vector<Move> pv;
};

struct SearchResult {
    std::vector<SearchCandidate> candidates; // complete same-depth root batch only
    std::vector<SearchCandidate> previousCandidates; // Previous complete depth, for safe opening ties.
    Move     best = 0;
    Move     ponder = 0;
    std::vector<Move> pv; // searched principal variation, capped to 16 moves
    Score    score = 0;
    int      depth = 0;
    int      seldepth = 0;
    uint64_t nodes = 0;
    int64_t  timeMs = 0;
    bool     stopped = false;
    bool     openingVariety = false;
};

// Strength handicap (set by the Elo profile, see botdata.cpp).
struct Strength {
    int      maxDepth = 64;   // cap for iterative deepening (0 = unlimited)
    uint64_t nodeLimit = 0;   // nodes per move (0 = unlimited)
    int      evalNoise = 0;   // centipawn noise added to the static evaluation
    int      moveNoise = 0;   // centipawn window for the root move choice
    int      blunderPct = 0;  // percent chance to play a random legal move
};

extern Strength g_strength;

// Search `pos`. `gameHistory` holds the Zobrist keys of the moves already
// played (used for repetition detection).
SearchResult searchPosition(const Position& pos, const SearchLimits& limits,
                            const std::vector<Key>& gameHistory, std::ostream& out,
                            std::atomic<bool>* started = nullptr);

// Signal the running search to stop (from the UCI thread).
void stopSearch();

// Lazy SMP: number of search threads (1 = single threaded, the default).
void setThreads(int n);
int  threadCount();

// Reset per game state (history heuristics, killers) and age the table.
void newSearchGame();

// Initialise reduction tables (call once at startup).
void initSearch();

// Bench: sum of nodes for a fixed set of positions (used by --selftest and tuning).
uint64_t benchNodes(int depth);

// Internal search regression checks used by --selftest.
bool searchRegressionTests(std::ostream& out);

#ifdef VIRGIN_EMBEDDED
void setSearchPoll(void (*poll)());
#endif

// Returns zero unless a stable, near-equal, materially safe opening alternative exists.
size_t chooseOpeningCandidate(const Position& pos, const SearchResult& result, double pick);

bool allocateSessionSearch();
void releaseSessionSearch();
