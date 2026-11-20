#include "adaptive.h"
#include "adaptive_params.h"
#include "movegen.h"
#include "opening_book.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace {
using namespace TalParams;
int material(const Position& pos, Color color) {
    constexpr int values[] = {100, 300, 300, 500, 900, 0};
    int result = 0;
    for (int type = PAWN; type < KING; ++type)
        result += popcount(pos.pieces(color, PieceType(type))) * values[type];
    return result;
}
int materialLoss(const Position& root, const SearchCandidate& candidate, int plies) {
    Position pos = root;
    const Color color = root.sideToMove();
    const int before = material(pos, color) - material(pos, ~color);
    const std::vector<Move> fallback{candidate.move};
    const auto& pv = candidate.pv.empty() ? fallback : candidate.pv;
    for (int i = 0; i < std::min(plies, int(pv.size())); ++i) {
        const Move move = pv[size_t(i)];
        if (find_legal_move(pos, move_to_uci(move)) != move)
            break;
        pos.applyMove(move);
    }
    return before - (material(pos, color) - material(pos, ~color));
}
int mateMoves(Score score) {
    return (SCORE_MATE - std::abs(score) + 1) / 2;
}
int engineScore(Score score) {
    if (!is_mate_score(score)) return score;
    const int distance = mateMoves(score);
    return score > 0 ? 100000 - distance : -100000 + distance;
}
}

AdaptiveChoice chooseAdaptive(const Position& pos, const std::vector<SearchCandidate>& cands,
                             int ply, AdaptiveState& state, double tieRoll, double tiePick) {
    using namespace TalParams;
    AdaptiveChoice choice;
    if (cands.empty()) { choice.mode = "keine_kandidaten"; return choice; }
    std::vector<size_t> order(cands.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return engineScore(cands[a].score) > engineScore(cands[b].score);
    });
    const size_t best = order.front();
    const int bestScore = engineScore(cands[best].score);
    const int E = -bestScore;
    const auto player = [&](size_t index) { return -engineScore(cands[index].score); };
    if (state.hasExpected)
        state.accuracy.push_back(state.expected - E <= anpassung_gut_bis_verlust_cp);
    while (state.accuracy.size() > size_t(anpassung_genauigkeit_fenster))
        state.accuracy.erase(state.accuracy.begin());
    const double accuracy = state.accuracy.empty() ? -1.0
        : double(std::count(state.accuracy.begin(), state.accuracy.end(), true)) / state.accuracy.size();
    (void)tieRoll; // Kept for the development probe protocol.
    const auto done = [&](size_t index, const char* mode, bool protect = false) {
        if (!protect && !pos.inCheck() && !is_mate_score(cands[best].score) && std::isfinite(tiePick)) {
            std::vector<size_t> equal{index};
            const int anchor = engineScore(cands[index].score);
            const int maxLoss = std::max(gleichwertig_cp, bestScore - anchor);
            const int anchorLoss = materialLoss(pos, cands[index], taktik_plies);
            const size_t needed = std::min(size_t(taktik_plies), cands[index].pv.size());
            for (size_t other : order) {
                if (other == index || is_mate_score(cands[other].score) ||
                    std::abs(engineScore(cands[other].score) - anchor) > gleichwertig_cp ||
                    bestScore - engineScore(cands[other].score) > maxLoss ||
                    cands[other].pv.size() < needed) continue;
                Position continuation = pos; bool legal = true;
                for (size_t i = 0; i < needed; ++i) {
                    const Move move = cands[other].pv[i];
                    if ((i == 0 && move != cands[other].move) ||
                        find_legal_move(continuation, move_to_uci(move)) != move) { legal = false; break; }
                    continuation.applyMove(move);
                }
                if (!legal || materialLoss(pos, cands[other], taktik_plies) > anchorLoss ||
                    materialLoss(pos, cands[other], sicherheit_materialpruefung_halbzuege) >
                        sicherheit_max_materialverlust_in_cp) continue;
                equal.push_back(other);
            }
            index = equal[std::min(equal.size() - 1,
                size_t(std::clamp(tiePick, 0.0, 1.0) * equal.size()))];
        }
        state.expected = player(index); state.hasExpected = true;
        return AdaptiveChoice{index, mode, E, player(index),
            accuracy < 0 ? -1 : int(std::floor(accuracy * 100 + 0.5))};
    };
    // Tactical protection also applies during the opening and adjustment phases.
    if (taktik_aktiv) {
        if (ply <= state.tacticUntil) return done(best, "taktik_folge", true);
        int length = std::min(taktik_plies, int(cands[best].pv.empty() ? 1 : cands[best].pv.size()));
        length -= length % 2;
        const int gain = length ? -materialLoss(pos, cands[best], length) : 0;
        if (gain >= taktik_min_materialgewinn) {
            state.tacticUntil = ply + taktik_sperre_halbzuege;
            return done(best, "taktik", true);
        }
    }
    if (ply < eroeffnung_beste_halbzuege) return done(best, "eroeffnung");
    if ((is_mate_score(cands[best].score) && cands[best].score > 0
         && mateMoves(cands[best].score) <= volle_staerke_immer_bei_matt_in)
        || E <= volle_staerke_ab_bewertung)
        return done(best, "volle_staerke");
    if (anpassung_aktiv) {
        state.band = std::abs(E) <= anpassung_gleich_bereich ? state.band + 1 : 0;
        if (ply <= state.adjustUntil) return done(best, "anpassung_folge");
    }
    if (E >= halten_ab_bewertung && halten_klebt_bis_partieende) state.hold = true;
    const auto safe = [&](size_t index) {
        if (index == best) return true;
        const int loss = materialLoss(pos, cands[index], sicherheit_materialpruefung_halbzuege);
        return loss <= sicherheit_max_materialverlust_in_cp
            && !(sicherheit_nie_dame_oder_turm_opfern && loss >= 450);
    };
    const auto nearest = [&](int target, int maxLoss) {
        std::vector<size_t> pool;
        for (size_t index : order)
            if (bestScore - engineScore(cands[index].score) <= maxLoss && safe(index))
                pool.push_back(index);
        std::stable_sort(pool.begin(), pool.end(), [&](size_t a, size_t b) {
            return std::abs(player(a) - target) < std::abs(player(b) - target);
        });
        if (pool.empty()) return best;
        return pool.front();
    };
    if (state.hold)
        return done(nearest(halten_ziel, halten_max_verlust_gegen_bester_zug), "halten");
    if (anpassung_aktiv && ply >= anpassung_ab_halbzug && std::abs(E) <= anpassung_gleich_bereich
        && state.band >= anpassung_stabil_zuege && ply - state.lastAdjust >= anpassung_pause_halbzuege
        && state.adjusts < anpassung_max_pro_partie && cands.size() >= size_t(anpassung_min_kandidaten)
        && std::none_of(cands.begin(), cands.end(), [](const auto& c) { return is_mate_score(c.score); })
        && state.accuracy.size() >= size_t(anpassung_genauigkeit_min_zuege)
        && accuracy >= 0 && accuracy < anpassung_genauigkeit_max) {
        bool ladder = bestScore - engineScore(cands[order.back()].score) <= anpassung_max_gesamtverlust_cp;
        for (size_t i = 1; i < order.size(); ++i) {
            const int step = engineScore(cands[order[i - 1]].score) - engineScore(cands[order[i]].score);
            ladder &= step >= 0 && step <= anpassung_max_stufe_cp;
        }
        size_t pick = best;
        int distance = 1000000;
        if (ladder) for (size_t index : order) {
            if (index == best) continue;
            const int loss = bestScore - engineScore(cands[index].score);
            const int delta = std::abs(loss - anpassung_abgeben_cp);
            if (loss >= anpassung_abgeben_von && loss <= anpassung_abgeben_bis && safe(index) && delta < distance) {
                pick = index; distance = delta;
            }
        }
        if (pick != best) {
            ++state.adjusts; state.lastAdjust = ply; state.adjustUntil = ply + anpassung_schonfrist_halbzuege;
            return done(pick, "anpassung");
        }
    }
    if (E <= helfen_ab_bewertung) {
        const int target = std::min(0, E + helfen_schritt);
        size_t pick = best; int distance = 1000000;
        for (size_t rank = 0; rank < std::min(order.size(), size_t(helfen_nur_plausible_kandidaten_bis_rang)); ++rank) {
            const size_t index = order[rank]; const int value = player(index);
            if (index == best || value > helfen_nie_ueber_bewertung || value - E > helfen_schritt
                || value <= E || bestScore - engineScore(cands[index].score) > helfen_max_verlust_gegen_bester_zug) continue;
            const int loss = materialLoss(pos, cands[index], helfen_materialpruefung_halbzuege);
            if (loss > helfen_kein_materialverlust_ueber_cp || (helfen_nie_dame_oder_turm_opfern && loss >= 450)) continue;
            const int delta = std::abs(value - target);
            if (delta < distance) { pick = index; distance = delta; }
        }
        if (pick != best) { ++state.helped; return done(pick, "helfen"); }
        return done(best, "helfen_keiner");
    }
    return done(nearest(ausgleich_ziel, ausgleich_max_verlust_gegen_bester_zug), "ausgleich");
}

bool adaptiveRegressionTests(std::ostream& out) {
    Position pos; pos.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    const char* moves[] = {"e2e4", "d2d4", "g1f3", "c2c4", "b1c3"};
    const int scores[] = {70, 40, 0, -30, -60};
    std::vector<SearchCandidate> cands;
    for (int i = 0; i < 5; ++i) {
        SearchCandidate c; c.move = find_legal_move(pos, moves[i]); c.score = scores[i]; c.pv = {c.move}; cands.push_back(c);
    }
    bool passed = true;
    const auto check = [&](bool ok, const char* text) { passed &= ok; out << (ok ? "ok    " : "FAIL  ") << text << '\n'; };
    AdaptiveState state;
    check(chooseAdaptive(pos, cands, 0, state).index == 0, "adaptive opening keeps the best candidate");
    state = {};
    check(chooseAdaptive(pos, cands, 16, state).index == 2, "adaptive equalisation selects the candidate nearest the player target");
    state = {}; state.hold = true;
    check(chooseAdaptive(pos, cands, 16, state).mode == "halten" && state.hold, "adaptive hold remains sticky");
    state = {}; state.adjustUntil = 20;
    check(chooseAdaptive(pos, cands, 16, state).mode == "anpassung_folge", "adaptive grace period preserves full-strength moves");
    bool legalBook = true; size_t count = 0;
    for (size_t offset = 0; offset < sizeof(OpeningBook::data) / sizeof(Move);) {
        Position bookPos; bookPos.setFen(OpeningBook::startFen); int plies = 0;
        while (const Move move = OpeningBook::data[offset++]) {
            const auto options = OpeningBook::moves(bookPos);
            if (find_legal_move(bookPos, move_to_uci(move)) != move ||
                std::find(options.begin(), options.end(), move) == options.end()) {
                legalBook = false;
            }
            bookPos.applyMove(move); ++plies;
        }
        legalBook &= plies >= 6 && plies <= OpeningBook::maxPly;
        ++count;
    }
    check(legalBook && count == OpeningBook::lineCount && OpeningBook::dataBytes < 10000,
          "all compact book lines legal, covered at lookup, and below 10000 bytes");
    Position bookPos; bookPos.setFen(OpeningBook::startFen);
    check(OpeningBook::moves(bookPos).size() == 7, "opening book covers seven distinct first moves");
    bookPos.applyMove(find_legal_move(bookPos, "e2e4"));
    const auto replies = OpeningBook::moves(bookPos);
    std::vector<Move> sampled;
    for (size_t i = 0; i < replies.size(); ++i)
        sampled.push_back(OpeningBook::choose(bookPos, (i + 0.5) / replies.size()));
    check(replies.size() == 9 && sampled == replies,
          "book samples distinct e4 replies equally regardless of line counts");
    check(OpeningBook::choose(bookPos, 0) == replies.front() &&
          OpeningBook::choose(bookPos, 1) == replies.back(), "book random endpoints valid");
    bookPos.applyMove(find_legal_move(bookPos, "a7a6"));
    check(OpeningBook::moves(bookPos).empty(), "book exits on an unlisted player move");

    auto equal = cands;
    equal[0].score = equal[1].score = 10;
    equal[2].score = -11; // outside the 20 cp window
    state = {};
    check(chooseAdaptive(pos, equal, 0, state, 1.0, 0.75).index == 1,
          "opening randomises equal moves without a probability gate or previous-depth veto");
    state = {};
    check(chooseAdaptive(pos, equal, 0, state, 1.0, 0).index == 0,
          "opening random selection can retain the first equal move");
    state = {}; state.adjustUntil = 20;
    check(chooseAdaptive(pos, equal, 16, state, 1.0, 1.0).index == 1,
          "equal moves also randomise during adaptive grace periods");
    state = {}; state.tacticUntil = 20;
    check(chooseAdaptive(pos, equal, 0, state, 0, 1).index == 0,
          "ongoing tactics protect the strongest move even during the opening");
    auto mate = equal; mate[0].score = SCORE_MATE - 5; mate[1].score = SCORE_MATE - 7;
    state = {};
    check(chooseAdaptive(pos, mate, 0, state, 0, 1).index == 0,
          "mate continuations cannot be replaced by random alternatives");
    auto corrupt = equal; corrupt[1].pv = {equal[0].move}; state = {};
    check(chooseAdaptive(pos, corrupt, 0, state, 0, 1).index == 0,
          "invalid alternative PV cannot enter random pool");
    Position tactic; tactic.setFen("k7/4q3/8/8/8/8/8/K3R3 w - - 0 1");
    const Move win = find_legal_move(tactic, "e1e7");
    const Move quiet = find_legal_move(tactic, "e1e2");
    Position afterWin = tactic; afterWin.applyMove(win);
    Position afterQuiet = tactic; afterQuiet.applyMove(quiet);
    std::vector<SearchCandidate> tactical = {
        {win, 10, {win, find_legal_move(afterWin, "a8b8")}},
        {quiet, 10, {quiet, find_legal_move(afterQuiet, "a8b8")}}
    };
    state = {};
    const auto protectedChoice = chooseAdaptive(tactic, tactical, 0, state, 0, 1);
    check(protectedChoice.index == 0 && protectedChoice.mode == "taktik" && state.tacticUntil >= 0,
          "new material-winning tactic is protected before opening randomisation");
    return passed;
}

// Development oracle protocol, separate from UCI. One FEN/header/candidate
// block per case, one result/state line. No external files needed by the engine.
int adaptiveProbe(std::istream& in, std::ostream& out) {
    std::string fen, line;
    while (std::getline(in, fen)) {
        if (fen.empty()) continue;
        Position pos; if (!pos.setFen(fen)) return 2;
        if (!std::getline(in, line)) return 2;
        std::istringstream header(line);
        AdaptiveState state; int ply, count, accCount; double roll, pick;
        if (!(header >> ply >> state.hold >> state.helped >> state.tacticUntil >> state.adjusts
              >> state.lastAdjust >> state.adjustUntil >> state.band >> state.hasExpected >> state.expected
              >> accCount >> roll >> pick >> count) || count < 1 || count > 256 || accCount < 0 || accCount > 100) return 2;
        for (int i = 0, value; i < accCount; ++i) { if (!(header >> value)) return 2; state.accuracy.push_back(value != 0); }
        std::vector<SearchCandidate> cands;
        for (int i = 0; i < count; ++i) {
            if (!std::getline(in, line)) return 2;
            std::istringstream row(line); std::string uci; SearchCandidate candidate; int plies;
            if (!(row >> uci >> candidate.score >> plies) || plies < 0 || plies > 128) return 2;
            candidate.move = find_legal_move(pos, uci); if (!candidate.move) return 2;
            Position replay = pos;
            for (int n = 0; n < plies; ++n) {
                if (!(row >> uci)) return 2;
                const Move move = find_legal_move(replay, uci); if (!move) return 2;
                candidate.pv.push_back(move); replay.applyMove(move);
            }
            cands.push_back(candidate);
        }
        const auto c = chooseAdaptive(pos, cands, ply, state, roll, pick);
        out << c.index << ' ' << c.mode << ' ' << c.playerEval << ' ' << c.afterEval << ' ' << c.accuracy
            << ' ' << state.hold << ' ' << state.helped << ' ' << state.tacticUntil << ' ' << state.adjusts
            << ' ' << state.lastAdjust << ' ' << state.adjustUntil << ' ' << state.band << ' ' << state.expected
            << ' ' << state.accuracy.size();
        for (bool value : state.accuracy) out << ' ' << value;
        out << '\n';
    }
    return 0;
}
