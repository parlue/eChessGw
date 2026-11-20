// Virgin - UCI chess engine
// Tapered evaluation. Milestone 1 wires material, piece square shapes and
// tempo; the remaining groups are registered and reported but become active
// in later milestones (see docs/ROADMAP.md).
#pragma once

#include <iosfwd>
bool evaluationRegressionTests(std::ostream& out);

#include "position.h"

// Evaluation terms, in the order of the breakdown array.
constexpr int EVAL_TERM_NB = 13;
extern const char* const EVAL_TERM_NAMES[EVAL_TERM_NB];

struct EvalBreakdown {
    Score mgWhite = 0, egWhite = 0;
    Score mgBlack = 0, egBlack = 0;
    Score mgScore = 0, egScore = 0;
    int   phase = 0;
    Score material = 0;  // tapered, from the side to move
    Score pst = 0;       // tapered, from the side to move
    Score tempo = 0;
    Score total = 0;     // from the side to move
    Score term[EVAL_TERM_NB] = {};  // per term, from the side to move
};

// Rebuild the piece square tables from the current weight values.
void build_pst_tables();

Score evaluate(const Position& pos);
EvalBreakdown evaluateDetailed(const Position& pos);

// Material and piece square tables only - cheap, used for lazy evaluation.
Score evaluateMaterial(const Position& pos);

// Lazy evaluation: returns the cheap material score when it already lies
// clearly outside the alpha/beta window, otherwise the full evaluation.
Score evaluateLazy(const Position& pos, Score alpha, Score beta);
