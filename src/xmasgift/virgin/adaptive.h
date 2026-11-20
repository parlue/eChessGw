#pragma once
#include "search.h"
#include <iosfwd>
#include <string>

// Based on CynusTactics TalChooser, with always-on safe equal-move sampling.
// Player scores have the opposite sign from engine root scores.
// Parameters are embedded, no runtime files.
struct AdaptiveState {
    bool hold = false;
    int helped = 0, tacticUntil = -1;
    int adjusts = 0, lastAdjust = -999, adjustUntil = -1, band = 0;
    bool hasExpected = false;
    int expected = 0;
    std::vector<bool> accuracy;
};
struct AdaptiveChoice {
    size_t index = 0;
    std::string mode;
    int playerEval = 0, afterEval = 0, accuracy = -1;
};
AdaptiveChoice chooseAdaptive(const Position& pos, const std::vector<SearchCandidate>& candidates,
                             int ply, AdaptiveState& state, double tieRoll = 1.0,
                             double tiePick = 0.0);
bool adaptiveRegressionTests(std::ostream& out);
int adaptiveProbe(std::istream& in, std::ostream& out);
