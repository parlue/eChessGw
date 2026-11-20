// Virgin - UCI chess engine
// Search implementation.
#include "search.h"
#include "time_policy.h"
#include "uci_output.h"
#include "embedded_config.h"

#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "tt.h"
#include "weights.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <sstream>
#include <thread>
#include <vector>
#include <new>

Strength g_strength;

namespace {

#ifdef VIRGIN_EMBEDDED
constexpr int MAX_PLY = VIRGIN_EMBEDDED_MAX_PLY;
#define VIRGIN_THREAD_LOCAL
void (*g_searchPoll)() = nullptr;
#else
constexpr int MAX_PLY = 128;
#define VIRGIN_THREAD_LOCAL thread_local
#endif
constexpr int MAX_PV = 16;

std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_nodes{0};
uint64_t          g_nodeLimit = 0;
int64_t           g_hardMs = 0;
int64_t           g_softMs = 0;
bool g_infinite = false;
std::chrono::steady_clock::time_point g_start;
int               g_threads = 0;   // 0 = automatic (see threadCount())

// Lazy SMP: helpers search the same position and only fill the shared
// transposition table. Measured on 4 cores: 2.4x nodes/s and +2 plies at the
// same time, so the automatic default is worth having - a fixed default of 1
// would leave the whole gain on the table unless the GUI sets the option.
int defaultThreads() {
#ifdef VIRGIN_EMBEDDED
    return 1;
#else
    const unsigned cores = std::thread::hardware_concurrency();
    if (cores == 0)
        return 1;
    // Four threads was the sweet spot in the measurement; more only costs
    // power and heat (relevant on a phone) without adding depth.
    return int(std::min<unsigned>(cores, 4));
#endif
}

// Per thread. Two threads must never share killers, history, the counter moves
// or the line of keys - they would corrupt each other. Only the transposition
// table is shared, which is the whole point of Lazy SMP: the helpers fill it
// and the main thread profits.
VIRGIN_THREAD_LOCAL std::mt19937 g_rng(20260101u);
VIRGIN_THREAD_LOCAL int          g_seldepth = 0;
VIRGIN_THREAD_LOCAL uint32_t t_pendingNodes = 0;
VIRGIN_THREAD_LOCAL bool t_testUnpruned = false;
VIRGIN_THREAD_LOCAL bool t_testRootFull = false;
VIRGIN_THREAD_LOCAL int t_aspirationMargin = 30;
VIRGIN_THREAD_LOCAL unsigned t_aspirationRetries = 0;
VIRGIN_THREAD_LOCAL bool         t_helper = false;   // helper thread (no output, no soft limit)

// These reductions are 0..5 for the compact 20-ply profile.
uint8_t              g_lmr[MAX_PLY][64];

struct StackEntry {
    Move  killers[2] = {0, 0};
    Move  currentMove = 0;
    Score staticEval = SCORE_NONE;
    Move pv[MAX_PV] = {};
    int pvLength = 0;
};
// cer4all: all mutable per-game search storage is allocated only for a search.
// No history/killers/line keys remain allocated in ordinary bridge mode.
struct SessionSearchMemory {
    std::vector<Key> keyHistory;
    size_t repetitionStart = 0, rootHistoryIndex = 0;
    int16_t history[COLOR_NB][SQUARE_NB][SQUARE_NB]{};
    int16_t captureHistory[COLOR_NB][PIECE_TYPE_NB][SQUARE_NB][PIECE_TYPE_NB]{};
    int16_t continuation[2048]{};
    // Bounded, exact-key countermove cache for the C3. Collisions evict a
    // hint, never invent a move or alter legality/evaluation/Adaptive rules.
    struct Counter { Move previous=0, reply=0; };
    Counter counterMove[COLOR_NB][256]{};
    StackEntry stack[MAX_PLY + 4]{};
};
SessionSearchMemory *sessionMemory = nullptr;
#define g_keyHistory (sessionMemory->keyHistory)
#define g_repetitionStart (sessionMemory->repetitionStart)
#define g_rootHistoryIndex (sessionMemory->rootHistoryIndex)
#define g_history (sessionMemory->history)
#define g_continuation (sessionMemory->continuation)
#define g_counterMove (sessionMemory->counterMove)
#define g_stack (sessionMemory->stack)

#define g_captureHistory (sessionMemory->captureHistory)

void updatePv(int ply, Move move) {
    StackEntry& ss = g_stack[ply];
    const StackEntry& child = g_stack[ply + 1];
    ss.pv[0] = move;
    const int count = std::min(child.pvLength, MAX_PV - 1);
    std::copy_n(child.pv, count, ss.pv + 1);
    ss.pvLength = count + 1;
}

using TimeBudget = VirginTime::Budget;
TimeBudget allocateTime(const Position& pos, const SearchLimits& limits) {
#ifdef VIRGIN_CORE2
    if (limits.infinite) return {};
    if (limits.movetime > 0)
        return {std::max<int64_t>(1, limits.movetime * 9 / 10), limits.movetime};
    const int64_t time = pos.sideToMove() == WHITE ? limits.wtime : limits.btime;
    const int64_t increment = pos.sideToMove() == WHITE ? limits.winc : limits.binc;
    if (time < 0) return {}; // depth/node search without a clock
    if (time <= 1) return {1, 1};
    const int64_t reserve = std::min<int64_t>(50, time / 10);
    const int64_t usable = std::max<int64_t>(1, time - reserve);
    const int moves = limits.movestogo > 0 ? limits.movestogo : 30;
    int64_t budget = std::max<int64_t>(1, time / moves + std::max<int64_t>(0, increment) * 3 / 4);
    budget = std::min(budget, std::max<int64_t>(1, usable / 3));
    // A generous game clock is not a reason to spend minutes on move one.
    // Fixed movetime/depth/node instructions remain separate from this policy.
    const bool opening = pos.fullmoveNumber() <= 4;
    if (opening) budget = std::min<int64_t>(budget, 1500);
    int64_t hard = std::max<int64_t>(1, std::min<int64_t>(budget * 4, usable / 2));
    if (opening) hard = std::min<int64_t>(hard, 3000);
    return {budget, hard};
#else
    if (limits.infinite) return {};
    return VirginTime::allocate(pos.sideToMove() == WHITE ? limits.wtime : limits.btime,
        pos.sideToMove() == WHITE ? limits.winc : limits.binc, limits.movestogo,
        pos.fullmoveNumber() <= 4, limits.movetime, limits.automaticTime, limits.moveOverhead);
#endif
}

struct EasyMove {
    Move last = 0;
    int stable = 0;
    Score scores[4] = {}, gaps[4] = {};
    bool update(Move move, Score score, Score gap, int depth, int64_t elapsed, bool timed) {
        if (move != last) { last = move; stable = 0; }
        const int index = stable++ % 4;
        scores[index] = score; gaps[index] = gap;
        if (!timed || depth < 8 || elapsed < 250 || stable < 4) return false;
        const auto range = std::minmax_element(scores, scores + 4);
        return *range.second - *range.first <= 20 && *std::min_element(gaps, gaps + 4) >= 150;
    }
};

int64_t elapsedMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - g_start)
        .count();
}

void checkLimits() {
    if (g_stop.load(std::memory_order_relaxed))
        return;
    if (g_nodeLimit > 0 && g_nodes.load(std::memory_order_relaxed) >= g_nodeLimit)
        g_stop.store(true, std::memory_order_relaxed);
    else if (g_hardMs > 0 && elapsedMs() >= g_hardMs)
        g_stop.store(true, std::memory_order_relaxed);
}

void flushNodes() {
    if (t_pendingNodes) {
        g_nodes.fetch_add(t_pendingNodes, std::memory_order_relaxed);
        t_pendingNodes = 0;
    }
}

bool visitNode() {
    if (g_stop.load(std::memory_order_relaxed))
        return false;
#if defined(VIRGIN_EMBEDDED) && !defined(VIRGIN_CORE2)
    constexpr unsigned checkInterval = 32; // Low MCU NPS must not hide a near deadline.
#else
    constexpr unsigned checkInterval = 256;
#endif
    if (++t_pendingNodes >= checkInterval) {
        flushNodes();
        checkLimits();
#ifdef VIRGIN_EMBEDDED
        if (g_searchPoll && (g_nodes.load(std::memory_order_relaxed) & 255) == 0) g_searchPoll();
#endif
    }
    return !g_stop.load(std::memory_order_relaxed);
}

Score evalNoiseValue() {
    if (g_strength.evalNoise <= 0)
        return 0;
    std::uniform_int_distribution<int> dist(-g_strength.evalNoise, g_strength.evalNoise);
    return dist(g_rng);
}

bool isRepetition(const Position& pos) {
    const Key key   = g_keyHistory.back();
    const int limit = std::min<int>(int(g_keyHistory.size() - g_repetitionStart) - 1, pos.halfmoveClock());
    int previousOccurrences = 0;
    for (int i = 2; i <= limit; i += 2) {
        const size_t index = g_keyHistory.size() - 1 - size_t(i);
        if (g_keyHistory[index] != key)
            continue;
        // A cycle wholly inside this search can be stopped immediately. A
        // single earlier occurrence in the played game is not a draw claim:
        // the opponent can still deviate on its next move.
        if (index >= g_rootHistoryIndex || ++previousOccurrences >= 2)
            return true;
    }
    return false;
}

bool zugzwangRisk(const Position& pos) {
    const Bitboard men = pos.pieces(KNIGHT) | pos.pieces(BISHOP) | pos.pieces(ROOK);
    const Color us = pos.sideToMove();
    return !pos.pieces(QUEEN) && popcount(men) <= 3
        && popcount(pos.pieces(PAWN)) <= 6 && popcount(men & pos.pieces(us)) <= 1;
}

bool hasNonPawnMaterial(const Position& pos, Color c) {
    return (pos.pieces(c, KNIGHT) | pos.pieces(c, BISHOP) | pos.pieces(c, ROOK)
            | pos.pieces(c, QUEEN))
        != 0;
}

int pieceValue(PieceType pt) {
    return pt == KING ? 20000 : EW.matMg[pt];
}

Bitboard pawnAttackMap(Color side, Bitboard pawns) {
    return side == WHITE ? shift_ne(pawns) | shift_nw(pawns) : shift_se(pawns) | shift_sw(pawns);
}

bool tacticalQuiet(const Position& pos, Move move) {
    const Color us = pos.sideToMove();
    const Square from = from_sq(move), to = to_sq(move);
    const PieceType pt = type_of(pos.pieceOn(from));
    if (pt == PAWN) {
        const Bitboard targets = pos.pieces(~us) & ~pos.pieces(~us, PAWN) & ~pos.pieces(~us, KING);
        if (pawn_attacks(us, to) & targets)
            return true;
        const int relativeRank = us == WHITE ? rank_of(to) : 7 - rank_of(to);
        if (relativeRank == 6) {
            Bitboard files = file_bb(to);
            if (file_of(to) > 0) files |= file_bb(file_of(to) - 1);
            if (file_of(to) < 7) files |= file_bb(file_of(to) + 1);
            const Bitboard ahead = us == WHITE ? (~0ULL << ((rank_of(to) + 1) * 8))
                : ((1ULL << (rank_of(to) * 8)) - 1);
            return !(ahead & files & pos.pieces(~us, PAWN));
        }
        return false;
    }
#ifndef VIRGIN_CORE2
    if (pos.isCastle(move)) return true; // Keep king-safety moves out of quiet pruning.
#endif
    if (pt == KING)
        return false;
    const Bitboard danger = pawnAttackMap(~us, pos.pieces(~us, PAWN));
    return (danger & square_bb(from)) && !(danger & square_bb(to));
}

void updateKillers(Move m, int ply) {
    if (g_stack[ply].killers[0] != m) {
        g_stack[ply].killers[1] = g_stack[ply].killers[0];
        g_stack[ply].killers[0] = m;
    }
}

int continuationIndex(int ply, Move move) {
    const uint32_t previous = g_stack[ply - 1].currentMove;
    return int((previous * 0x9E3779B1u ^ uint32_t(move) * 0x85EBCA6Bu) >> 21);
}

int continuationScore(int ply, Move move) {
    return ply > 0 && g_stack[ply - 1].currentMove ? g_continuation[continuationIndex(ply, move)] : 0;
}

void updateContinuation(int ply, Move move, int bonus) {
    if (ply <= 0 || !g_stack[ply - 1].currentMove)
        return;
    bonus = std::clamp(bonus, -1024, 1024);
    int16_t& h = g_continuation[continuationIndex(ply, move)];
    h = int16_t(int(h) + bonus - int(h) * std::abs(bonus) / 16384);
}

PieceType capturedType(const Position& pos, Move m) {
    return pos.isEnPassant(m) ? PAWN : type_of(pos.pieceOn(to_sq(m)));
}

int16_t& captureHistoryRef(const Position& pos, Move m) {
    return g_captureHistory[pos.sideToMove()][type_of(pos.pieceOn(from_sq(m)))][to_sq(m)][capturedType(pos, m)];
}

void updateCaptureHistory(const Position& pos, Move m, int bonus) {
    bonus = std::clamp(bonus, -1024, 1024);
    int16_t& h = captureHistoryRef(pos, m);
    h = int16_t(int(h) + bonus - int(h) * std::abs(bonus) / 16384);
}

int scoreMove(const Position& pos, Move m, Move ttMove, int ply) {
    if (m == ttMove)
        return 1 << 30;

    if (is_promotion(m))
        return (1 << 28) + pieceValue(promotion_type(m));

    if (pos.isCapture(m)) {
        const PieceType victim =
            pos.isEnPassant(m) ? PAWN : type_of(pos.pieceOn(to_sq(m)));
        const PieceType attacker = type_of(pos.pieceOn(from_sq(m)));
        int             s        = (1 << 24) + pieceValue(victim) * 16 - pieceValue(attacker)
                                 + captureHistoryRef(pos, m) / 8;
        if (!seeGE(pos, m, 0))
            s -= (1 << 24) + (1 << 20);  // below killers and all quiet moves
        return s;
    }

    if (g_stack[ply].killers[0] == m)
        return 1 << 20;
    if (g_stack[ply].killers[1] == m)
        return (1 << 20) - 1;

    if (ply > 0) {
        const Move previous = g_stack[ply - 1].currentMove;
        const auto& hint = g_counterMove[pos.sideToMove()][(previous ^ (previous >> 8)) & 255];
        if (previous && hint.previous == previous && hint.reply == m)
            return 1 << 19;
    }

    return g_history[pos.sideToMove()][from_sq(m)][to_sq(m)]
         + continuationScore(ply, m) / 2
         + (tacticalQuiet(pos, m) ? (1 << 16) : 0);
}

void updateHistory(Color us, Move move, int bonus) {
    constexpr int limit = 16384;
    bonus = std::clamp(bonus, -1024, 1024);
    int16_t& h = g_history[us][from_sq(move)][to_sq(move)];
    h = int16_t(int(h) + bonus - int(h) * std::abs(bonus) / limit);
}

void updateQuietStats(const Position& pos, Move best, int depth, int ply) {
    if (pos.isCapture(best) || is_promotion(best))
        return;

    const Color us = pos.sideToMove();
    updateHistory(us, best, std::min(depth * depth, 1024));
    updateContinuation(ply, best, std::min(depth * depth, 1024));

    if (ply > 0) {
        const Move previous = g_stack[ply - 1].currentMove;
        if (previous)
            g_counterMove[us][(previous ^ (previous >> 8)) & 255] = {previous, best};
    }
}

}  // namespace

namespace {

// ---------------------------------------------------------------------------
// Quiescence search: captures, promotions and (in check) all evasions.
// ---------------------------------------------------------------------------
Score qsearch(Position& pos, Score alpha, Score beta, int ply, int qPly = 0) {
    g_stack[ply].pvLength = 0;
    if (!visitNode())
        return 0;

    if (ply > g_seldepth)
        g_seldepth = ply;
    if (ply >= MAX_PLY - 1)
        return evaluate(pos);

    const bool inCheck = pos.inCheck();
    if (isRepetition(pos) || pos.halfmoveClock() >= 100) {
        if (inCheck && !hasLegalMove(pos))
            return Score(-SCORE_MATE + ply);
        return SCORE_DRAW;
    }
    const Score oldAlpha = alpha;
    const bool pvNode = beta - alpha > 1;
    TTEntry qentry{};
    const bool qhit = TT.probe(pos.key(), qentry);
    if (qhit && !pvNode && !t_testUnpruned) {
        const Score score = scoreFromTT(qentry.score, ply);
        const int bound = boundOf(qentry);
        if (bound == BOUND_EXACT || (bound == BOUND_LOWER && score >= beta)
            || (bound == BOUND_UPPER && score <= alpha))
            return score;
    }
    if (!inCheck && !hasLegalMove(pos))
        return SCORE_DRAW;
    const auto storeLeaf = [&](Move move, Score score, Score evaluation, Bound bound) {
        if (!t_testUnpruned)
            TT.store(pos.key(), move, score, evaluation, 0, bound, ply);
    };
    Score staticScore = SCORE_NONE;
    Move bestMove = 0;
    Score best;
    if (inCheck) {
        best = -SCORE_INFINITE;
    } else {
        staticScore = qhit && qentry.eval != SCORE_NONE ? qentry.eval : evaluate(pos);
        best = staticScore + evalNoiseValue();
        if (best >= beta) {
            storeLeaf(0, best, staticScore, BOUND_LOWER);
            return best;
        }
        if (best > alpha)
            alpha = best;
    }

    MoveList list;
    // PV-ness is a property of the incoming window. Stand pat may raise
    // alpha to beta-1 without turning this into a non-PV leaf.
    const bool quietChecks = !inCheck && qPly == 0 && (pvNode || t_testUnpruned);
    generatePseudoLegal(pos, list, !inCheck && !quietChecks);
    if (quietChecks) {
        const Color us = pos.sideToMove();
        const Square king = pos.kingSquare(~us);
        const Bitboard bishopRays = bishop_attacks(king, pos.occupied());
        const Bitboard rookRays = rook_attacks(king, pos.occupied());
        const Bitboard checks[PIECE_TYPE_NB] = {
            pawn_attacks(~us, king), knight_attacks(king), bishopRays,
            rookRays, bishopRays | rookRays, 0
        };
        Bitboard discovered = 0;
        Bitboard sliders = (bishop_attacks(king, 0) & (pos.pieces(us, BISHOP) | pos.pieces(us, QUEEN)))
                         | (rook_attacks(king, 0) & (pos.pieces(us, ROOK) | pos.pieces(us, QUEEN)));
        while (sliders) {
            const Square slider = pop_lsb(sliders);
            const Bitboard between = BetweenBB[king][slider] & pos.occupied();
            if (popcount(between) == 1 && (between & pos.pieces(us)))
                discovered |= between;
        }
        int count = 0;
        for (int i = 0; i < list.count; ++i) {
            const Move m = list.moves[size_t(i)];
            const PieceType pt = type_of(pos.pieceOn(from_sq(m)));
            if (pos.isCapture(m) || is_promotion(m)
                || (((checks[pt] & square_bb(to_sq(m))) || (discovered & square_bb(from_sq(m)))
                     || pos.isCastle(m)) && pos.givesCheck(m) && seeGE(pos, m, 0)))
                list.moves[size_t(count++)] = m;
        }
        list.count = count;
    }
    int scores[256];
    for (int i = 0; i < list.count; ++i)
        scores[i] = scoreMove(pos, list.moves[size_t(i)], qhit ? qentry.move : 0, ply);

    int legal = 0;
    for (int i = 0; i < list.count; ++i) {
        int bestIdx = i;
        for (int j = i + 1; j < list.count; ++j)
            if (scores[j] > scores[bestIdx])
                bestIdx = j;
        if (bestIdx != i) {
            std::swap(scores[size_t(i)], scores[size_t(bestIdx)]);
            std::swap(list.moves[size_t(i)], list.moves[size_t(bestIdx)]);
        }

        const Move m = list.moves[size_t(i)];
        Position next = pos;
        next.applyMove(m);
        if (next.isSquareAttacked(next.kingSquare(pos.sideToMove()), next.sideToMove()))
            continue;
        // A negative material swap does not refute a forcing checking sacrifice.
        // scoreMove already ran SEE for captures; reuse its winning/losing band.
        if (!inCheck && !is_promotion(m) && !next.inCheck()
            && ((qhit && m == qentry.move) ? !seeGE(pos, m, 0) : scores[size_t(i)] < 0))
            continue;
        ++legal;
        g_keyHistory.push_back(next.key());
        const Score s = -qsearch(next, -beta, -alpha, ply + 1, qPly + 1);
        g_keyHistory.pop_back();

        if (g_stop.load(std::memory_order_relaxed))
            return 0;

        if (s > best) {
            best = s;
            bestMove = m;
            if (s > alpha) {
                alpha = s;
                updatePv(ply, m);
                if (alpha >= beta)
                    break;
            }
        }
    }

    if (inCheck && legal == 0) {
        best = Score(-SCORE_MATE + ply);
        storeLeaf(0, best, SCORE_NONE, BOUND_EXACT);
        return best;
    }
    const Bound bound = best >= beta ? BOUND_LOWER : best > oldAlpha ? BOUND_EXACT : BOUND_UPPER;
    storeLeaf(bestMove, best, staticScore, bound);
    return best;
}

// ---------------------------------------------------------------------------
// Main search: PVS with transposition table, null move, LMR and futility.
// ---------------------------------------------------------------------------
Score search(Position& pos, int depth, Score alpha, Score beta, int ply, bool cutNode, bool allowNull = true) {
    g_stack[ply].pvLength = 0;
    if (!visitNode())
        return 0;

    const bool pvNode   = (beta - alpha) > 1;
    const bool rootNode = (ply == 0);

    if (ply > g_seldepth)
        g_seldepth = ply;

    if (!rootNode) {
        if (pos.halfmoveClock() >= 100 || isRepetition(pos)) {
            if (pos.inCheck() && !hasLegalMove(pos))
                return Score(-SCORE_MATE + ply);
            return SCORE_DRAW;
        }

        // Mate distance pruning
        alpha = std::max(alpha, Score(-SCORE_MATE + ply));
        beta  = std::min(beta, Score(SCORE_MATE - ply - 1));
        if (alpha >= beta)
            return alpha;
    }

    // Check extensions can keep depth unchanged for many plies.
    if (ply >= MAX_PLY - 1)
        return evaluate(pos);

    if (depth <= 0)
        return qsearch(pos, alpha, beta, ply);

    TTEntry entry{};
    const bool ttHit = TT.probe(pos.key(), entry);
    const TTEntry* tte = &entry;
    const Move ttMove = ttHit ? tte->move : 0;

    if (ttHit && !t_testUnpruned && !pvNode && depthOf(*tte) >= depth) {
        const Score s    = scoreFromTT(tte->score, ply);
        const int   bnd  = boundOf(*tte);
        if (bnd == BOUND_EXACT || (bnd == BOUND_LOWER && s >= beta)
            || (bnd == BOUND_UPPER && s <= alpha))
            return s;
    }

    const bool inCheck = pos.inCheck();
    Score staticScore = (ttHit && tte->eval != SCORE_NONE) ? tte->eval : evaluate(pos);
    if (!inCheck)
        staticScore += evalNoiseValue();
    g_stack[ply].staticEval = inCheck ? SCORE_NONE : staticScore;

    const Color us = pos.sideToMove();
    const Bitboard pawnDanger = pawnAttackMap(~us, pos.pieces(~us, PAWN));
    const bool threatenedPiece = (pawnDanger & pos.pieces(us)
                                  & ~pos.pieces(us, PAWN) & ~pos.pieces(us, KING)) != 0;

    const bool endgameRisk = zugzwangRisk(pos);
    // Reductions and move-count/futility pruning only misjudge in true pawn
    // endings (exact tempo play). With pieces on the board they are safe, and
    // switching them off in every thin endgame cost several plies of depth.
    const bool pawnEnding = !hasNonPawnMaterial(pos, WHITE) && !hasNonPawnMaterial(pos, BLACK);
    // ------------------------------------------------------------- pruning
    if (!t_testUnpruned && !endgameRisk && !pvNode && !inCheck && !is_mate_score(beta)) {
        // Reverse futility pruning
        if (!threatenedPiece && depth <= 7 && staticScore - 75 * depth >= beta)
            return staticScore;

        // Razoring
        if (depth <= 3 && staticScore + 220 * depth <= alpha) {
            const Score q = qsearch(pos, alpha, beta, ply);
            if (q <= alpha)
                return q;
        }

        // Null move pruning
        if (allowNull && depth >= 3 && staticScore >= beta && hasNonPawnMaterial(pos, pos.sideToMove())) {
            Position nullPos = pos;
            nullPos.applyNullMove();
            const size_t savedStart = g_repetitionStart;
            g_repetitionStart = g_keyHistory.size();
            g_keyHistory.push_back(nullPos.key());
            const int   R = 3 + depth / 4;
            const Score s = -search(nullPos, depth - 1 - R, Score(-beta), Score(-beta + 1),
                                    ply + 1, !cutNode, false);
            g_keyHistory.pop_back();
            g_repetitionStart = savedStart;
            if (g_stop.load(std::memory_order_relaxed))
                return 0;
            if (s >= beta)
                return is_mate_score(s) ? beta : s;
        }
    }

    // Internal iterative reduction when no TT move is available
    if (!t_testUnpruned && depth >= 5 && !ttMove && !inCheck && !threatenedPiece && !endgameRisk)
        --depth;

    MoveList list;
    generatePseudoLegal(pos, list);

    int scores[256];
    for (int i = 0; i < list.count; ++i)
        scores[i] = scoreMove(pos, list.moves[size_t(i)], ttMove, ply);

    Move        bestMove = 0;
    Score       best     = -SCORE_INFINITE;
    const Score oldAlpha = alpha;
    int         legal    = 0;
    int         moveCount = 0;
    auto& quiets = list.moves; // Reuse already searched prefix; saves 512 bytes per ply.
    int quietCount = 0;
    Move captures[64];
    int captureCount = 0;

    for (int i = 0; i < list.count; ++i) {
        int bestIdx = i;
        for (int j = i + 1; j < list.count; ++j)
            if (scores[j] > scores[bestIdx])
                bestIdx = j;
        if (bestIdx != i) {
            std::swap(scores[size_t(i)], scores[size_t(bestIdx)]);
            std::swap(list.moves[size_t(i)], list.moves[size_t(bestIdx)]);
        }

        const Move m = list.moves[size_t(i)];
        Position next = pos;
        next.applyMove(m);
        if (next.isSquareAttacked(next.kingSquare(pos.sideToMove()), next.sideToMove()))
            continue;
        ++legal;
        ++moveCount;

        const bool isCaptureMove = pos.isCapture(m);
        const bool isPromoMove   = is_promotion(m);

        const bool givesCheck = next.inCheck();
        const bool dangerousQuiet = !isCaptureMove && !isPromoMove && tacticalQuiet(pos, m);

        // Futility pruning and late move pruning (quiet moves only)
        if (!t_testUnpruned && !pawnEnding && !rootNode && !pvNode && !inCheck && !isCaptureMove && !isPromoMove && !givesCheck && !dangerousQuiet
            && best > -SCORE_MATE_BOUND && g_stack[ply].staticEval != SCORE_NONE) {
            if (depth <= 8 && g_stack[ply].staticEval + 100 + 120 * depth <= alpha)
                continue;
            if (depth <= 8 && moveCount > (4 + depth * depth) / (2 - int(cutNode)))
                continue;
        }

        // SEE pruning: skip moves that lose too much material for the depth left.
        if (!t_testUnpruned && !pawnEnding && !endgameRisk && !rootNode && !pvNode && !inCheck && !givesCheck && !isPromoMove
            && depth <= 8 && best > -SCORE_MATE_BOUND && moveCount > 1
            && !seeGE(pos, m, isCaptureMove ? -90 * depth : -25 * depth * depth))
            continue;

        g_stack[ply].currentMove = m;
        if (!isCaptureMove && !isPromoMove)
            quiets[quietCount++] = m;
        else if (isCaptureMove && !isPromoMove && captureCount < 64)
            captures[captureCount++] = m;
        g_keyHistory.push_back(next.key());

        // Extend sound checks, including checking captures, with a ply budget.
        const bool extend = givesCheck && seeGE(pos, m, 0) && ply < 2 * depth;
        const int newDepth = depth - 1 + int(extend);

        Score s;
        if (moveCount == 1) {
            s = -search(next, newDepth, -beta, -alpha, ply + 1, false);
        } else {
            int r = 0;
            if (!t_testUnpruned && !pawnEnding && depth >= 3 && moveCount > 3 && !isCaptureMove && !isPromoMove && !inCheck
                && !givesCheck) {
                r = g_lmr[std::min(depth, MAX_PLY - 1)][std::min(moveCount, 63)];
                if (cutNode)
                    ++r;
                // Preserve more depth on the principal variation and on quiet
                // moves with a consistent record of refuting earlier choices.
                if (pvNode)
                    --r;
                if (dangerousQuiet)
                    --r;
                const int history = g_history[pos.sideToMove()][from_sq(m)][to_sq(m)]
                                  + continuationScore(ply, m) / 2;
                if (history > 4096)
                    --r;
                else if (history < -4096)
                    ++r;
                r = std::max(0, std::min(r, newDepth - 1));
            }

            s = -search(next, newDepth - r, Score(-alpha - 1), -alpha, ply + 1, true);
            if (s > alpha && r > 0)
                s = -search(next, newDepth, Score(-alpha - 1), -alpha, ply + 1, !cutNode);
            if (s > alpha && s < beta)
                s = -search(next, newDepth, -beta, -alpha, ply + 1, false);
        }

        g_keyHistory.pop_back();
        g_stack[ply].currentMove = 0;

        if (g_stop.load(std::memory_order_relaxed))
            return 0;

        if (s > best) {
            best     = s;
            bestMove = m;
            if (s > alpha) {
                alpha = s;
                updatePv(ply, m);
                if (alpha >= beta) {
                    if (!isCaptureMove && !isPromoMove) {
                        updateQuietStats(pos, m, depth, ply);
                        updateKillers(m, ply);
                        const int malus = -std::min(depth * depth, 1024);
                        for (int q = 0; q + 1 < quietCount; ++q) {
                            updateHistory(pos.sideToMove(), quiets[q], malus);
                            updateContinuation(ply, quiets[q], malus);
                        }
                    }
                    if (isCaptureMove && !isPromoMove) {
                        const int bonus = std::min(depth * depth, 1024);
                        updateCaptureHistory(pos, m, bonus);
                        for (int c = 0; c + 1 < captureCount; ++c)
                            updateCaptureHistory(pos, captures[c], -bonus);
                    }
                    break;
                }
            }
        }
    }

    if (legal == 0)
        return inCheck ? Score(-SCORE_MATE + ply) : SCORE_DRAW;

    const Bound bound = (best >= beta)    ? BOUND_LOWER
                      : (best > oldAlpha) ? BOUND_EXACT
                                          : BOUND_UPPER;
    TT.store(pos.key(), bestMove, best, g_stack[ply].staticEval, depth, bound, ply);
    return best;
}

}  // namespace

void initSearch() {
    for (int d = 0; d < MAX_PLY; ++d)
        for (int m = 0; m < 64; ++m)
            g_lmr[d][m] = (m < 2 || d < 2)
                              ? 0
                              : int(0.77 + std::log(double(d)) * std::log(double(m)) / 1.75);
}

void newSearchGame() {
    std::memset(g_history, 0, sizeof(g_history));
    std::memset(g_continuation, 0, sizeof(g_continuation));
    std::memset(g_captureHistory, 0, sizeof(g_captureHistory));
    std::memset(g_counterMove, 0, sizeof(g_counterMove));
    for (StackEntry& entry : g_stack) {
        entry.killers[0]  = 0;
        entry.killers[1]  = 0;
        entry.currentMove = 0;
        entry.staticEval  = SCORE_NONE;
    }
    TT.newSearch();
}

void stopSearch() { g_stop.store(true, std::memory_order_relaxed); }

namespace {

// MultiPV: each rank searches all remaining legal root moves with PVS.
// Only complete batches are returned; subset bounds never enter the root TT.
bool searchRootCandidates(const Position& pos, std::vector<Move>& moves, int depth,
                          int count, std::vector<SearchCandidate>& candidates) {
    candidates.clear();
    for (int rank = 0; rank < std::min(count, int(moves.size())); ++rank) {
        Score alpha = -SCORE_INFINITE;
        size_t bestIndex = size_t(rank);
        SearchCandidate best;
        for (size_t i = size_t(rank); i < moves.size(); ++i) {
            Position next = pos;
            next.applyMove(moves[i]);
            g_stack[0].currentMove = moves[i];
            g_keyHistory.push_back(next.key());
            Score score;
            if (i == size_t(rank))
                score = -search(next, depth - 1, -SCORE_INFINITE, SCORE_INFINITE, 1, false);
            else {
                score = -search(next, depth - 1, Score(-alpha - 1), -alpha, 1, true);
                if (score > alpha)
                    score = -search(next, depth - 1, -SCORE_INFINITE, -alpha, 1, false);
            }
            g_keyHistory.pop_back();
            g_stack[0].currentMove = 0;
            if (g_stop.load(std::memory_order_relaxed)) return false;
            if (score > alpha) {
                alpha = score; bestIndex = i;
                best.move = moves[i]; best.score = score; best.pv = {best.move};
                const int length = std::min(g_stack[1].pvLength, MAX_PV - 1);
                best.pv.insert(best.pv.end(), g_stack[1].pv, g_stack[1].pv + length);
            }
        }
        std::rotate(moves.begin() + rank, moves.begin() + bestIndex, moves.begin() + bestIndex + 1);
        candidates.push_back(std::move(best));
    }
    // Selective PVS can slightly change the order when a remaining move is
    // searched afresh. The chooser consumes sorted, exact candidate scores.
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    return true;
}

// One iterative deepening run on `pos`. Only the main thread (t_helper == false)
// prints the UCI info lines and honours the soft time limit; the helpers of
// Lazy SMP only fill the shared transposition table and stop on the hard limit.
SearchResult runSearch(const Position& pos, const std::vector<Key>& gameHistory, int maxDepth,
                       std::ostream& out, int multiPV = 1, const std::vector<Move>* allowed = nullptr) {
    SearchResult result;

    g_keyHistory = gameHistory;
    if (g_keyHistory.empty() || g_keyHistory.back() != pos.key())
        g_keyHistory.push_back(pos.key());
    g_repetitionStart = 0;
    g_rootHistoryIndex = g_keyHistory.size() - 1;
    g_seldepth = 0;
    t_pendingNodes = 0;

    MoveList rootList;
    generateLegal(pos, rootList);
    if (rootList.count == 0)
        return result;

    std::vector<Move>  rootMoves(rootList.begin(), rootList.end());
    if (allowed) {
        rootMoves.erase(std::remove_if(rootMoves.begin(), rootMoves.end(), [&](Move m) {
            return std::find(allowed->begin(), allowed->end(), m) == allowed->end();
        }), rootMoves.end());
        if (rootMoves.empty()) return result;
    }
    std::vector<Score> rootScores(rootMoves.size(), Score(-SCORE_INFINITE));

    Score bestScore = -SCORE_INFINITE;
    Move  bestMove  = rootMoves[0];
    Score previous  = 0;
    const Score rootStatic = evaluate(pos);
    g_stack[0].staticEval = rootStatic;
    const bool exactRootScores = g_strength.moveNoise > 0 || t_testRootFull;
    t_aspirationRetries = 0;
    EasyMove easy;
#ifndef VIRGIN_CORE2
    VirginTime::Decision timeDecision;
#endif
    const auto timeFinished = [&](int depth, bool clear) {
#ifdef VIRGIN_CORE2
        return !t_helper && g_softMs > 0 && elapsedMs() >= g_softMs && depth >= 2;
#else
        return !t_helper && timeDecision.finish({g_softMs,g_hardMs},bestMove,bestScore,
                                               depth,elapsedMs(),clear);
#endif
    };
    const auto finishEarly = [&](Score gap, int depth) {
        if (t_helper || g_infinite) return false;
        if (rootMoves.size() == 1 && allowed == nullptr) {
            const UciOutputGuard outputGuard;
            out << "info string early move: only legal move" << std::endl;
            return true;
        }
        const bool timed = (g_softMs > 0 || g_hardMs > 0)
            && !g_strength.evalNoise && !g_strength.moveNoise && !g_strength.blunderPct;
        if (easy.update(bestMove, bestScore, gap, depth, elapsedMs(), timed)) {
            const UciOutputGuard outputGuard;
            out << "info string early move: stable clear candidate" << std::endl;
            return true;
        }
        return false;
    };

    for (int depth = 1; depth <= maxDepth; ++depth) {
        if (multiPV > 1) {
            std::vector<SearchCandidate> batch;
            if (!searchRootCandidates(pos, rootMoves, depth, multiPV, batch)) break;
#ifndef VIRGIN_CORE2
            result.previousCandidates = result.candidates;
#endif
            result.candidates = std::move(batch);
            const SearchCandidate& best = result.candidates.front();
            bestMove = best.move; bestScore = best.score;
            result.best = bestMove; result.score = bestScore; result.pv = best.pv;
            result.depth = depth; result.seldepth = g_seldepth;
            TT.store(pos.key(), bestMove, bestScore, rootStatic, depth, BOUND_EXACT, 0);
            flushNodes();
            if (!t_helper) {
                const UciOutputGuard outputGuard;
                const int64_t ms = elapsedMs();
                const uint64_t nodes = g_nodes.load(std::memory_order_relaxed);
                for (size_t rank = 0; rank < result.candidates.size(); ++rank) {
                    const auto& candidate = result.candidates[rank];
                    out << "info depth " << depth << " seldepth " << g_seldepth
                        << " multipv " << rank + 1 << " score cp " << candidate.score
                        << " nodes " << nodes << " time " << ms << " pv";
                    for (Move move : candidate.pv) out << ' ' << move_to_uci(move);
                    out << std::endl;
                }
            }
            if (is_mate_score(bestScore) && !g_infinite) break;
            const Score gap = result.candidates.size() > 1
                ? result.candidates[0].score - result.candidates[1].score : SCORE_INFINITE;
            if (finishEarly(gap, depth)) break;
            if (g_nodeLimit && g_nodes.load(std::memory_order_relaxed) >= g_nodeLimit) break;
            if (timeFinished(depth, gap >= 100)) break;
            if (g_hardMs > 0 && elapsedMs() >= g_hardMs) break;
            continue;
        }
        Score alpha = -SCORE_INFINITE;
        Score beta  = SCORE_INFINITE;
        int delta = t_aspirationMargin;
        if (depth >= 4 && !exactRootScores) {
            alpha = std::max(-SCORE_INFINITE, Score(previous - delta));
            beta  = std::min(SCORE_INFINITE, Score(previous + delta));
        }

        while (true) {
            std::fill(rootScores.begin(), rootScores.end(), Score(-SCORE_INFINITE));
            Score iterBest = -SCORE_INFINITE;
            Move  iterMove = rootMoves[0];
            Score a        = alpha;
            Move iterPv[MAX_PV] = {};
            int iterPvLength = 0;

            for (size_t i = 0; i < rootMoves.size(); ++i) {
                Position next = pos;
                next.applyMove(rootMoves[i]);
                g_keyHistory.push_back(next.key());

                g_stack[0].currentMove = rootMoves[i];
                Score s;
                if (exactRootScores) {
                    // Weak bots need true scores for their random candidate
                    // window. Full strength needs only the best move exact.
                    s = -search(next, depth - 1, -SCORE_INFINITE, SCORE_INFINITE, 1, false);
                } else if (i == 0) {
                    s = -search(next, depth - 1, -beta, -a, 1, false);
                } else {
                    s = -search(next, depth - 1, Score(-a - 1), -a, 1, true);
                    if (s > a && s < beta)
                        s = -search(next, depth - 1, -beta, -a, 1, false);
                }

                g_keyHistory.pop_back();
                g_stack[0].currentMove = 0;
                if (g_stop.load(std::memory_order_relaxed))
                    break;

                rootScores[i] = s;
                if (s > iterBest) {
                    iterBest = s;
                    iterMove = rootMoves[i];
                    iterPv[0] = iterMove;
                    const int count = std::min(g_stack[1].pvLength, MAX_PV - 1);
                    std::copy_n(g_stack[1].pv, count, iterPv + 1);
                    iterPvLength = count + 1;
                }
                if (s > a)
                    a = s;
                if (!exactRootScores && a >= beta)
                    break;
            }

            if (g_stop.load(std::memory_order_relaxed))
                break;

            if (iterBest <= alpha && alpha > -SCORE_INFINITE) {
                ++t_aspirationRetries;
                alpha = std::max(-SCORE_INFINITE, Score(std::min(alpha, iterBest) - delta));
                delta = std::min(16000, delta * 2);
                continue; // bounds are never published as exact root scores
            }
            if (iterBest >= beta && beta < SCORE_INFINITE) {
                ++t_aspirationRetries;
                beta = std::min(SCORE_INFINITE, Score(std::max(beta, iterBest) + delta));
                delta = std::min(16000, delta * 2);
                // Retry the refuting move first, not all preceding root moves.
                const auto found = std::find(rootMoves.begin(), rootMoves.end(), iterMove);
                std::rotate(rootMoves.begin(), found, found + 1);
                continue;
            }

            bestScore = iterBest;
            bestMove  = iterMove;
            previous  = iterBest;
            result.pv.assign(iterPv, iterPv + iterPvLength);
            TT.store(pos.key(), bestMove, bestScore, rootStatic, depth, BOUND_EXACT, 0);
            break;
        }

        if (g_stop.load(std::memory_order_relaxed))
            break;

        // Order the root moves by score for the next iteration.
        std::vector<size_t> order(rootMoves.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(),
                  [&](size_t x, size_t y) { return rootScores[x] > rootScores[y]; });

        std::vector<Move>  newMoves(rootMoves.size());
        std::vector<Score> newScores(rootScores.size());
        for (size_t i = 0; i < order.size(); ++i) {
            newMoves[i]  = rootMoves[order[i]];
            newScores[i] = rootScores[order[i]];
        }
        rootMoves  = std::move(newMoves);
        rootScores = std::move(newScores);

        result.depth    = depth;
        result.score    = bestScore;
        result.best     = bestMove;
        result.seldepth = g_seldepth;

        flushNodes();
        const int64_t  ms  = elapsedMs();
        const uint64_t n  = g_nodes.load(std::memory_order_relaxed);
        const uint64_t nps = ms > 0 ? n * 1000 / uint64_t(ms) : n * 1000;
        // Only the main thread talks to the GUI - helpers would interleave
        // their lines with its own.
        if (!t_helper) {
            const UciOutputGuard outputGuard;
            out << "info depth " << depth << " seldepth " << g_seldepth << " score cp " << bestScore
                << " nodes " << n << " nps " << nps << " hashfull " << TT.hashfull()
                << " time " << ms << " pv";
            for (Move move : result.pv)
                out << ' ' << move_to_uci(move);
            out << std::endl;
        }

        if (is_mate_score(bestScore) && !g_infinite)
            break;
        Score second = -SCORE_INFINITE;
        for (size_t i = 0; i < rootMoves.size(); ++i)
            if (rootMoves[i] != bestMove) second = std::max(second, rootScores[i]);
        if (finishEarly(bestScore - second, depth)) break;
        if (g_nodeLimit > 0 && g_nodes.load(std::memory_order_relaxed) >= g_nodeLimit)
            break;
        if (timeFinished(depth, bestScore - second >= 100))
            break;
        if (g_hardMs > 0 && elapsedMs() >= g_hardMs)
            break;
    }

    // Analysis is explicitly unbounded, even if depth 64 or a mate was reached.
    while (!t_helper && g_infinite && !g_stop.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // -------------------------------- strength: blunder chance and move noise
    if (rootMoves.size() > 1 && !g_stop.load(std::memory_order_relaxed)) {
        std::uniform_int_distribution<int> chance(1, 100);
        if (g_strength.blunderPct > 0 && chance(g_rng) <= g_strength.blunderPct) {
            std::uniform_int_distribution<size_t> pick(0, rootMoves.size() - 1);
            bestMove = rootMoves[pick(g_rng)];
        } else if (g_strength.moveNoise > 0) {
            std::vector<size_t> candidates;
            for (size_t i = 0; i < rootMoves.size(); ++i)
                if (rootScores[i] >= bestScore - g_strength.moveNoise)
                    candidates.push_back(i);
            if (candidates.size() > 1) {
                std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
                bestMove = rootMoves[candidates[pick(g_rng)]];
            }
        }
    }

    if (result.pv.empty() || result.pv.front() != bestMove)
        result.pv.assign(1, bestMove);
    flushNodes();
    result.best    = bestMove;
    result.nodes   = g_nodes.load(std::memory_order_relaxed);
    result.timeMs  = elapsedMs();
    result.stopped = g_stop.load(std::memory_order_relaxed);
    return result;
}

}  // namespace

SearchResult searchPosition(const Position& pos, const SearchLimits& limits,
                            const std::vector<Key>& gameHistory, std::ostream& out,
                            std::atomic<bool>* started) {
    SearchResult result;

    g_stop.store(false, std::memory_order_relaxed);
    if(limits.cancellation && limits.cancellation->load(std::memory_order_acquire))
        g_stop.store(true,std::memory_order_relaxed);
    // The UCI reader may accept stop only after this reset has happened.
    if (started)
        started->store(true, std::memory_order_release);
    g_nodes.store(0, std::memory_order_relaxed);
    g_nodeLimit = limits.nodes > 0 ? limits.nodes : g_strength.nodeLimit;
    g_start     = std::chrono::steady_clock::now();
    g_hardMs    = 0;
    g_softMs    = 0;

    g_infinite = limits.infinite;
    const TimeBudget budget = allocateTime(pos, limits);
    g_softMs = budget.soft; g_hardMs = budget.hard;

    const int depthCap = g_strength.maxDepth > 0 ? g_strength.maxDepth : 64;
    const int maxDepth = limits.infinite ? 64 : std::min(limits.depth, depthCap);

    TT.newSearch(); // age entries per root search, not just per game

    // ------------------------------------------------------------- Lazy SMP
    // The helpers search the same position with the same limits. They never
    // report anything: the shared transposition table is the whole point, so
    // whoever sees a position first does the work for the others. The main
    // thread keeps the result and the soft time limit, then stops the helpers.
    std::vector<std::thread> helpers;
    const int threads = threadCount();
    for (int i = 1; i < threads; ++i) {
        helpers.emplace_back([&pos, &gameHistory, maxDepth, &out, &limits, i] {
            t_helper = true;
            g_rng.seed(20260101u + uint32_t(i) * 7919u);
            (void)runSearch(pos, gameHistory, maxDepth, out, 1,
                            limits.restrictRootMoves ? &limits.rootMoves : nullptr);
        });
    }

    t_helper = false;
    result   = runSearch(pos, gameHistory, maxDepth, out, std::clamp(limits.multiPV, 1, 5),
                         limits.restrictRootMoves ? &limits.rootMoves : nullptr);

    g_stop.store(true, std::memory_order_relaxed);
    for (std::thread& helper : helpers)
        helper.join();
    result.nodes = g_nodes.load(std::memory_order_relaxed);
    result.timeMs = elapsedMs();
    result.openingVariety = limits.openingVariety;
    return result;
}

void setThreads(int n) {
#ifdef VIRGIN_EMBEDDED
    (void)n; g_threads = 1;
#else
#ifdef VIRGIN_WASM
    g_threads = std::clamp(n, 0, 4); // Fixed prewarmed browser worker pool.
#else
    g_threads = std::clamp(n, 0, 64);
#endif
#endif
}
#ifdef VIRGIN_EMBEDDED
void setSearchPoll(void (*poll)()) { g_searchPoll = poll; }
#endif

int threadCount() { return g_threads > 0 ? g_threads : defaultThreads(); }

uint64_t benchNodes(int depth) {
    static const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
    };

    const Strength saved = g_strength;
    g_strength = Strength{};  // bench must be deterministic

    uint64_t total = 0;
    for (const char* fen : fens) {
        Position    pos;
        std::string error;
        if (!pos.setFen(fen, &error))
            continue;
        SearchLimits       limits;
        limits.depth = depth;
        std::ostringstream sink;
        const SearchResult r = searchPosition(pos, limits, {}, sink);
        total += r.nodes;
    }

    g_strength = saved;
    return total;
}

// __SPLIT__





bool searchRegressionTests(std::ostream& out) {
    const Strength savedStrength = g_strength;
    g_strength = Strength{};
    g_stop.store(false);
    g_hardMs = g_softMs = 0;
    g_nodeLimit = 0;
    g_repetitionStart = 0;
    g_rootHistoryIndex = 0;
    bool passed = true;
    const auto check = [&](bool ok, const char* text) {
        out << (ok ? "ok    " : "FAIL  ") << text << '\n';
        passed &= ok;
    };
    Position endgameProbe;
    endgameProbe.setFen("8/8/2k5/2b5/K7/3r4/R7/8 b - - 57 87");
    check(!zugzwangRisk(endgameProbe), "active rook and bishop side keeps normal search pruning");
    endgameProbe.setFen("8/8/2k5/2b5/K7/3r4/R7/8 w - - 57 87");
    check(zugzwangRisk(endgameProbe), "lone rook defender receives zugzwang-safe search");
    endgameProbe.setFen("8/8/4k3/4p3/4K3/8/8/8 w - - 0 1");
    check(zugzwangRisk(endgameProbe), "pawn opposition endgame disables speculative forward pruning");
    Position clockPos;
    clockPos.setFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    SearchLimits longClock; longClock.wtime = 7380000; longClock.btime = 7200000;
    const auto openingTime = allocateTime(clockPos, longClock);
    check(openingTime.soft <= 1500 && openingTime.hard <= 3000,
          "BearChess two-hour clock does not allocate minutes to 1...reply");
    SearchLimits fixedTime; fixedTime.movetime = 1000;
    const auto fixedBudget = allocateTime(clockPos, fixedTime);
#ifdef VIRGIN_CORE2
    check(fixedBudget.soft == 900 && fixedBudget.hard == 1000,
#else
    check(fixedBudget.soft == 570 && fixedBudget.hard == 950,
#endif
          "fixed one-second search retains its own time budget");
    longClock.btime = 0;
    check(allocateTime(clockPos, longClock).hard == 1,
          "an exhausted clock does not become an unlimited search");

#ifndef VIRGIN_CORE2
    SearchLimits automatic; automatic.automaticTime=5000;
    check(allocateTime(clockPos,automatic).hard==4950,"bare go has a five-second maximum minus overhead");
    automatic.automaticTime=10000;
    check(allocateTime(clockPos,automatic).hard==9950,"adaptive automatic play can use ten seconds");
    automatic.movetime=3000;
    check(allocateTime(clockPos,automatic).hard==2950,"explicit three-second limit overrides automatic ten seconds");
    automatic.infinite=true;
    check(allocateTime(clockPos,automatic).hard==0,"infinite analysis never acquires the automatic timer");
    check(VirginTime::allocate(300000,2000,30,false,0,0,50).soft >
          VirginTime::allocate(300000,0,30,false,0,0,50).soft,"Fischer increment increases available thinking time");
    const auto stage=VirginTime::allocate(90000,0,1,false,0,0,50);
    check(stage.soft>VirginTime::allocate(90000,0,40,false,0,0,50).soft && stage.hard<90000,
          "last move before a time control can spend more without spending the whole clock");
    check(VirginTime::allocate(80,10000,0,false,0,0,100).hard==1,
          "increment does not buy time before the current move is completed");
    check(VirginTime::allocate(INT64_MAX,INT64_MAX,1,false,0,0,50).hard>0,
          "huge valid clocks use overflow-safe budget arithmetic");
    VirginTime::Decision stableTime, unstableTime;
    stableTime.finish({1000,4000},1,20,1,100,false);
    unstableTime.finish({1000,4000},1,20,1,100,false);
    stableTime.finish({1000,4000},1,20,2,200,false);
    unstableTime.finish({1000,4000},1,20,2,200,false);
    stableTime.finish({1000,4000},1,20,3,400,false);
    unstableTime.finish({1000,4000},1,20,3,400,false);
    check(stableTime.finish({1000,4000},1,20,4,800,false) &&
          !unstableTime.finish({1000,4000},2,-30,4,800,false),
          "stable results finish sooner while changed moves and evaluation drops earn time");
    check(unstableTime.finish({1000,4000},3,-100,5,4000,false),
          "instability never extends the hard deadline");
    const auto makeCandidate=[&](const char* sequence,Score score) {
        SearchCandidate c; c.score=score; Position pos;pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        std::istringstream in(sequence);std::string text;
        while(in>>text){Move m=find_legal_move(pos,text);if(!m)break;c.pv.push_back(m);pos.applyMove(m);}
        if (!c.pv.empty()) c.move=c.pv.front();
        return c;
    };
    Position opening;opening.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    SearchResult ties;ties.depth=6;
    ties.candidates={makeCandidate("e2e4 e7e5 g1f3 b8c6",20),makeCandidate("d2d4 d7d5 c2c4 e7e6",15)};
    ties.previousCandidates=ties.candidates;
    check(chooseOpeningCandidate(opening,ties,0.99)==1 && chooseOpeningCandidate(opening,ties,0.0)==0,
          "verified near-equal opening moves can vary without an opening book");
    auto unsafe=ties;unsafe.candidates[1].score=9;
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"eleven-centipawn loss is outside the opening window");
    unsafe=ties;unsafe.previousCandidates[1].score=0;
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"opening alternative must also be near-equal at the previous depth");
    unsafe=ties;unsafe.depth=5;
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"shallow opening results do not qualify for random selection");
    unsafe=ties;unsafe.candidates[1].pv.resize(2);
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"short principal variations cannot qualify as safe alternatives");
    unsafe=ties;unsafe.candidates[0].score=SCORE_MATE-3;
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"known mates always keep the strongest move");
    unsafe=ties;
    unsafe.candidates[0]=makeCandidate("d2d4 d7d5 c2c4 e7e6",20);
    unsafe.candidates[1]=makeCandidate("e2e4 d7d5 e4e5 d8d7 e5e6 d7e6",15);
    unsafe.previousCandidates=unsafe.candidates;
    check(chooseOpeningCandidate(opening,unsafe,0.99)==0,"near-equal scores cannot admit a line that loses a pawn");
#endif

    EasyMove easyTest;
    bool clear = false;
    for (int i = 0; i < 4; ++i) clear = easyTest.update(Move(1), 200 + i, 180, 8 + i, 300, true);
    check(clear && !easyTest.update(Move(2), 200, 180, 12, 300, true),
          "early clear-move decision requires stable score and unchanged move");
    EasyMove closeTest;
    for (int i = 0; i < 4; ++i) clear = closeTest.update(Move(1), 200, 30, 8 + i, 300, true);
    check(!clear, "close alternatives do not trigger the clear-move shortcut");
    g_infinite = false;

    Position p;
    p.setFen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1");
    g_keyHistory = {p.key()};
    check(qsearch(p, -SCORE_INFINITE, SCORE_INFINITE, 0) == SCORE_DRAW,
          "quiescence detects stalemate before stand pat");
    p.setFen("7k/6Q1/5K2/8/8/8/8/8 b - - 100 1");
    g_keyHistory = {p.key()};
    check(qsearch(p, -SCORE_INFINITE, SCORE_INFINITE, 0) == -SCORE_MATE,
          "checkmate takes precedence over the 50 move clock");
    p.setFen("7k/4P3/8/8/8/8/8/K7 w - - 0 1");
    g_keyHistory = {p.key()};
    check(qsearch(p, -SCORE_INFINITE, SCORE_INFINITE, 0) > evaluate(p) + 300,
          "quiescence searches a quiet promotion");
    check(search(p, 8, -SCORE_INFINITE, SCORE_INFINITE, MAX_PLY - 1, false) == evaluate(p),
          "main search respects its stack boundary");
    p.setFen("4k3/8/2p5/3p4/4Q3/8/8/4K3 w - - 0 1");
    const Move bad = make_move(SQ_E4, SQ_D5);
    const Move quiet = make_move(SQ_E4, SQ_E3);
    g_stack[0].killers[0] = g_stack[0].killers[1] = 0;
    check(!seeGE(p, bad, 0) && scoreMove(p, bad, 0, 0) < scoreMove(p, quiet, 0, 0),
          "losing captures sort behind quiet moves");
    for (int n = 0; n < 100000; ++n)
        updateHistory(WHITE, quiet, 1024);
    check(g_history[WHITE][SQ_E4][SQ_E3] > 0
              && g_history[WHITE][SQ_E4][SQ_E3] <= 16384,
          "history saturates without wrapping negative");
    p.setFen("7k/7p/5n2/7Q/8/8/1B6/6KR w - - 0 1");
    g_keyHistory = {p.key()};
    check(is_mate_score(qsearch(p, -SCORE_INFINITE, SCORE_INFINITE, 0)),
          "quiescence sees mate through a nominally defended checking capture");
    p.setFen("7k/7p/5KQ1/8/8/8/8/8 w - - 0 1");
    g_keyHistory = {p.key()};
    check(is_mate_score(qsearch(p, -SCORE_INFINITE, SCORE_INFINITE, 0)),
          "bounded quiet checks expose mate at a quiescence leaf");
    TT.clear(); g_stop.store(false); g_keyHistory = {p.key()};
    const Score standPat = evaluate(p);
    check(is_mate_score(qsearch(p, -SCORE_INFINITE, Score(standPat + 1), 0)),
          "PV quiet mate survives stand pat narrowing its window to one centipawn");
#ifndef VIRGIN_CORE2
    Position castleProbe;
    castleProbe.setFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    check(tacticalQuiet(castleProbe,find_legal_move(castleProbe,"e1g1")) &&
          tacticalQuiet(castleProbe,find_legal_move(castleProbe,"e1c1")),
          "legal castling is protected from quiet move pruning");
#endif
    {
        const int savedThreads = g_threads; setThreads(1);
        Position root; root.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        SearchLimits restricted;restricted.depth=4;restricted.restrictRootMoves=true;
        restricted.rootMoves={find_legal_move(root,"e2e4")};
        TT.clear();newSearchGame();std::ostringstream sink;
        const SearchResult one=searchPosition(root,restricted,{},sink);
        check(one.best==restricted.rootMoves[0] && one.depth==4,
              "one searchmoves candidate is analysed to depth, not treated as forced play");
        restricted.rootMoves.push_back(find_legal_move(root,"d2d4"));restricted.multiPV=2;
        TT.clear();newSearchGame();const auto two=searchPosition(root,restricted,{},sink);
        check(two.candidates.size()==2 && two.candidates[0].move!=two.candidates[1].move &&
              std::all_of(two.candidates.begin(),two.candidates.end(),[&](const auto& c){
                  return std::find(restricted.rootMoves.begin(),restricted.rootMoves.end(),c.move)!=restricted.rootMoves.end();
              }),"MultiPV respects the allowed root moves");
        restricted.rootMoves.clear();TT.clear();newSearchGame();
        check(searchPosition(root,restricted,{},sink).best==0,
              "empty searchmoves list cannot fall back to unrestricted play");
        setThreads(savedThreads);
    }


    {
        const int savedThreads = g_threads;
        setThreads(1);
        t_testUnpruned = true;
        t_aspirationMargin = 1;
        bool equal = true, legalPv = true, retried = false;
        const char* fens[] = {
            "8/8/3k4/3p4/3P4/4K3/8/8 w - - 0 1",
            "7k/7p/8/8/3P4/8/6K1/8 w - - 0 1"
        };
        for (const char* fen : fens) {
            p.setFen(fen);
            SearchLimits limits; limits.depth = 5;
            std::ostringstream sink;
            TT.clear(); newSearchGame();
            t_testRootFull = false;
            const SearchResult narrow = searchPosition(p, limits, {}, sink);
            retried |= t_aspirationRetries > 0;
            TT.clear(); newSearchGame();
            t_testRootFull = true;
            const SearchResult exhaustive = searchPosition(p, limits, {}, sink);
            equal &= narrow.depth == 5 && exhaustive.depth == 5 && narrow.score == exhaustive.score;
            Position replay = p;
            legalPv &= !narrow.pv.empty() && narrow.pv.front() == narrow.best;
            for (Move m : narrow.pv) {
                if (find_legal_move(replay, move_to_uci(m)) != m) { legalPv = false; break; }
                replay.applyMove(m);
            }
        }
        p.setFen("7k/7p/8/8/3P4/8/6K1/8 w - - 0 1");
        SearchLimits multiLimits; multiLimits.depth = 3; multiLimits.multiPV = 5;
        TT.clear(); newSearchGame(); t_testRootFull = false;
        std::ostringstream multiSink;
        const SearchResult multi = searchPosition(p, multiLimits, {}, multiSink);
        bool complete = multi.depth == 3 && multi.candidates.size() == 5;
        std::vector<Move> seen;
        for (const auto& candidate : multi.candidates) {
            complete &= candidate.move != 0 && find_legal_move(p, move_to_uci(candidate.move)) == candidate.move
                && std::find(seen.begin(), seen.end(), candidate.move) == seen.end();
            seen.push_back(candidate.move);
            Position child = p; child.applyMove(candidate.move);
            TT.clear(); g_stop.store(false); g_keyHistory = {p.key(), child.key()};
            g_rootHistoryIndex = 0; g_repetitionStart = 0;
            const Score reference = -search(child, 2, -SCORE_INFINITE, SCORE_INFINITE, 1, false);
            complete &= reference == candidate.score;
            Position replay = p;
            for (Move move : candidate.pv) {
                if (find_legal_move(replay, move_to_uci(move)) != move) { complete = false; break; }
                replay.applyMove(move);
            }
        }
        check(complete, "five same-depth root candidates agree with independent full-window searches");
        check(equal && retried, "narrow aspiration PVS agrees with exhaustive full-window root search");
        check(legalPv, "searched principal variations replay legally and begin with bestmove");
        t_testUnpruned = t_testRootFull = false;
        t_aspirationMargin = 30;
        g_threads = savedThreads;
        TT.clear();
    }

    // Use real reversible moves to distinguish a historical twofold from
    // three occurrences, and from a cycle wholly inside the current search.
    p.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    g_keyHistory = {p.key()};
    const char* cycle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int round = 0; round < 2; ++round) {
        for (const char* text : cycle) {
            const Move m = find_legal_move(p, text);
            p.applyMove(m);
            g_keyHistory.push_back(p.key());
        }
        if (round == 0) {
            g_rootHistoryIndex = g_keyHistory.size() - 1;
            check(!isRepetition(p), "historical twofold is not an immediately claimable draw");
        } else {
            check(isRepetition(p), "a cycle back to the search root terminates the line");
            g_rootHistoryIndex = g_keyHistory.size() - 1;
            check(isRepetition(p), "three actual occurrences are recognised");
            g_repetitionStart = g_keyHistory.size() - 1;
            check(!isRepetition(p), "repetition cannot cross a null move boundary");
        }
    }
    g_repetitionStart = g_rootHistoryIndex = 0;
    newSearchGame();
    g_keyHistory.clear();
    g_strength = savedStrength;
    return passed;
}
size_t chooseOpeningCandidate(const Position& root, const SearchResult& result, double pick) {
#ifdef VIRGIN_CORE2
    (void)root; (void)result; (void)pick; return 0;
#else
    const auto& current = result.candidates;
    const auto& previous = result.previousCandidates;
    if (root.fullmoveNumber() > 4 || root.inCheck() || result.depth < 6 ||
        current.size() < 2 || previous.size() < 2 || !std::isfinite(pick)) return 0;
    if (is_mate_score(current[0].score) || is_mate_score(previous[0].score) ||
        current[0].score < -75 || previous[0].score < -75) return 0;
    const auto material = [](const Position& pos, Color us) {
        constexpr int values[] = {100,320,330,500,900,0};
        int value = 0;
        for (int type=PAWN; type<KING; ++type)
            value += values[type] * (popcount(pos.pieces(us,PieceType(type))) -
                                     popcount(pos.pieces(~us,PieceType(type))));
        return value;
    };
    const int before = material(root,root.sideToMove());
    size_t pool[5] = {0}; size_t count = 1;
    for (size_t i=1;i<current.size() && i<5;++i) {
        const auto& candidate = current[i];
        const auto old = std::find_if(previous.begin(),previous.end(),[&](const auto& c){return c.move==candidate.move;});
        if (old==previous.end() || is_mate_score(candidate.score) || is_mate_score(old->score) ||
            candidate.score > current[0].score || old->score > previous[0].score ||
            current[0].score-candidate.score>10 || previous[0].score-old->score>10 ||
            std::abs(candidate.score-old->score)>25 || candidate.score < -75 ||
            (current[0].score>=0 && candidate.score<0) || candidate.pv.size()<4 ||
            candidate.pv.front()!=candidate.move) continue;
        Position pos = root; bool safe = true;
        for (size_t ply=0;ply<std::min<size_t>(6,candidate.pv.size());++ply) {
            const Move move = candidate.pv[ply];
            if (find_legal_move(pos,move_to_uci(move))!=move) {safe=false;break;}
            pos.applyMove(move);
            if ((ply&1) && material(pos,root.sideToMove())<before) {safe=false;break;}
        }
        if (safe) pool[count++] = i;
    }
    const size_t index = std::min(count-1,size_t(std::clamp(pick,0.0,1.0)*count));
    return pool[index];
#endif
}

// Called by the single cer4all search task; never concurrent with a search.
bool allocateSessionSearch() {
    if (!sessionMemory) sessionMemory = new (std::nothrow) SessionSearchMemory{};
    return sessionMemory != nullptr;
}
extern "C" const uint32_t c3SearchMemoryBytes=sizeof(SessionSearchMemory);
void releaseSessionSearch() {
    delete sessionMemory; sessionMemory = nullptr;
    g_stop = true; g_nodes = 0; g_rng.seed(20260101u);
    g_nodeLimit = 0; g_hardMs = g_softMs = 0; g_infinite = false;
    g_seldepth = 0; t_pendingNodes = 0; g_searchPoll = nullptr;
    g_strength = Strength{};
}
