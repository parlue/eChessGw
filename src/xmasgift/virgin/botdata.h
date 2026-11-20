// Virgin - UCI chess engine
// Bot data: playing styles, strength (Elo) profile and the generated data
// files. This lives in the engine because milestone 4 applies it to the search.
#pragma once

#include <string>
#include <vector>

struct StyleWeight {
    const char* name;
    int         value;
};

struct StyleDef {
    const char*              id;
    int                      level;  // 1 = timid .. 5 = bull
    const char*              name;
    const char*              desc;
    const char*              tags;
    std::vector<StyleWeight> weights;
};

struct KnowledgeRange {
    const char* term;
    int         start;  // Elo where the term starts to count
    int         full;   // Elo where the term is fully active
};

struct EloHandicap {
    int    maxDepth;
    int    nodeLimit;
    int    moveError;
    double blunderPct;
    int    npsTarget;
    int    moveNoise;
};

constexpr int ELO_MIN  = 700;
constexpr int ELO_MAX  = 2800;
constexpr int ELO_STEP = 50;

const std::vector<StyleDef>&       styles();
const std::vector<KnowledgeRange>& knowledgeRanges();
std::vector<int>                   eloLevels();

EloHandicap eloHandicap(int elo);

// Bot identification: "v<elo>-<style>", e.g. "v1200-thunder".
std::string              botId(int elo, const std::string& styleId);
const StyleDef*          findStyle(const std::string& idOrName);
const StyleDef*          styleFromBotId(const std::string& botId);
int                      botEloFromId(const std::string& botId, int fallback);
std::string              botDisplay(int elo, const StyleDef& style);
std::string              botTags(const StyleDef& style);

// Applies a style: resets all weights to their defaults, then applies the
// style overrides. This is what setoption name Style/Bot does.
bool applyStyle(const std::string& idOrName);

// Boldness in tenths (10 = Fortress ... 50 = Thunder). Values in between are
// interpolated between the two neighbouring style profiles.
bool applyBoldness(int boldnessTenths);

// Applies the Elo profile: knowledge scaling of the evaluation groups plus the
// search handicap (depth cap, node limit, evaluation noise, move noise,
// blunder chance). This is what setoption name UCI_Elo does.
void applyElo(int elo);

// Removes all strength handicaps (full knowledge, no noise, no blunders).
void clearStrength();

// Writes weights_default.txt, styles.txt, elo_profile.txt, bots.txt and
// bots.json into dataDir. Returns the number of files written, -1 on error.
int generateDataFiles(const std::string& dataDir, std::string* error);
