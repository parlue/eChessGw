// Virgin - UCI chess engine
// Bot data implementation: styles, Elo profile, data file generation.
#include "botdata.h"

#include "search.h"
#include "weights.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

const std::vector<StyleDef> g_styles = {
    {"fortress", 1, "Fortress", "Sicherheit zuerst, meidet Schaerfe, nimmt Remis an",
     "defensive,solid,drawish",
     {
         {"Misc.Contempt", -25},
         {"Group.Material", 120},
         {"Group.KingSafety", 200},
         {"Group.Pawns", 160},
         {"Group.Mobility", 90},
         {"Group.Threats", 110},
         {"Group.Drawish", 130},
         {"Group.Aggression", 40},
         {"Group.Complexity", 30},
         {"Aggression.KingAttack", 40},
         {"Aggression.PawnStorm", 0},
         {"Aggression.Initiative", 30},
         {"Aggression.SacrificeTolerance", 0},
         {"Aggression.AvoidQueenTrade", 0},
         {"Rooks.OpenFileVsKing", 15},
         {"KingSafety.ShieldMissing", -35},
         {"Pst.KingMgShelter", 30},
     }},
    {"stone", 2, "Stone", "Solide und positionell, geringes Risiko",
     "solid,positional",
     {
         {"Misc.Contempt", -5},
         {"Group.Material", 110},
         {"Group.KingSafety", 150},
         {"Group.Pawns", 130},
         {"Group.Mobility", 95},
         {"Group.Threats", 105},
         {"Group.Drawish", 115},
         {"Group.Aggression", 70},
         {"Group.Complexity", 60},
         {"Aggression.KingAttack", 70},
         {"Aggression.PawnStorm", 50},
         {"Aggression.Initiative", 60},
         {"Aggression.SacrificeTolerance", 40},
         {"Aggression.AvoidQueenTrade", 50},
         {"Rooks.OpenFileVsKing", 24},
     }},
    {"normal", 3, "Normal", "Referenzstil, alle Gewichte auf Standard", "balanced", {}},
    {"fire", 4, "Fire", "Sucht den Koenigsangriff, meidet Damentausch",
     "attacking,initiative",
     {
         {"Misc.Contempt", 15},
         {"Group.Material", 100},
         {"Group.KingSafety", 110},
         {"Group.Pawns", 100},
         {"Group.Mobility", 105},
         {"Group.Threats", 110},
         {"Group.Drawish", 90},
         {"Group.Aggression", 125},
         {"Group.Complexity", 100},
         {"Aggression.KingAttack", 125},
         {"Aggression.PawnStorm", 120},
         {"Aggression.Initiative", 150},
         {"Aggression.SacrificeTolerance", 120},
         {"Aggression.AvoidQueenTrade", 110},
         {"Rooks.OpenFileVsKing", 42},
     }},
    {"thunder", 5, "Thunder", "Tollkuehn: Opfer, Bauernsturm, Koenig jagt Koenig",
     "bold,sacrificial,gambit",
     {
         {"Misc.Contempt", 45},
         {"Group.Material", 90},
         {"Group.KingSafety", 70},
         {"Group.Pawns", 70},
         {"Group.Mobility", 110},
         {"Group.Threats", 120},
         {"Group.Drawish", 80},
         {"Group.Aggression", 220},
         {"Group.Complexity", 220},
         {"Aggression.KingAttack", 220},
         {"Aggression.PawnStorm", 250},
         {"Aggression.Initiative", 220},
         {"Aggression.SacrificeTolerance", 300},
         {"Aggression.AvoidQueenTrade", 200},
         {"Rooks.OpenFileVsKing", 54},
         {"KingSafety.ShieldMissing", -10},
         {"Pst.KingMgShelter", 10},
     }},
};

const std::vector<KnowledgeRange> g_knowledge = {
    {"Material.Pawn", 700, 800},
    {"Material.Knight", 700, 900},
    {"Material.Bishop", 700, 900},
    {"Material.Rook", 700, 1000},
    {"Material.Queen", 700, 1000},
    {"Group.Pst", 700, 1700},
    {"Group.Mobility", 700, 1800},
    {"Group.KingSafety", 700, 1500},
    {"Group.Pawns", 700, 2000},
    {"Group.Threats", 700, 2200},
    {"Group.Pieces", 700, 2400},
    {"Group.Rooks", 700, 2500},
    {"Group.Space", 700, 2300},
    {"Group.Initiative", 700, 1600},
    {"Group.Endgame", 700, 1900},
    {"Group.Drawish", 700, 2100},
    {"Group.Aggression", 700, 700},
    {"Group.Complexity", 700, 700},
};

// Nickname pool per style; picked by Elo index so a bot has a stable name.
const std::vector<std::vector<const char*>> g_botNames = {
    {"Maus", "Hase", "Reh", "Lamm", "Taube"},
    {"Fels", "Anker", "Bollwerk", "Mauer", "Schild"},
    {"Waage", "Mitte", "Balance", "Mittelgrund", "Ausgleich"},
    {"Falke", "Speer", "Klinge", "Sturm", "Pfeil"},
    {"Stier", "Bulle", "Rammbock", "Tornado", "Vulkan"},
};

std::string formatOneDecimal(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

const std::vector<StyleDef>&       styles() { return g_styles; }
const std::vector<KnowledgeRange>& knowledgeRanges() { return g_knowledge; }

std::vector<int> eloLevels() {
    std::vector<int> levels;
    for (int elo = ELO_MIN; elo <= ELO_MAX; elo += ELO_STEP)
        levels.push_back(elo);
    return levels;
}

EloHandicap eloHandicap(int elo) {
    const double t = double(elo - ELO_MIN) / double(ELO_MAX - ELO_MIN);
    const double s = 1.0 - t;

    EloHandicap h;
    h.maxDepth   = int(std::lround(2.0 + 20.0 * std::pow(t, 0.85)));
    h.nodeLimit  = int(std::lround(5000.0 * std::pow(20'000'000.0 / 5000.0, t)));
    // Error model: a 700 Elo bot blunders often, a 2800 Elo bot almost never.
    h.moveError  = int(std::lround(3.0 + 87.0 * std::pow(s, 1.6)));
    h.blunderPct = std::round((0.3 + 10.7 * std::pow(s, 1.3)) * 10.0) / 10.0;
    h.npsTarget  = int(std::lround(800.0 * std::pow(260'000.0 / 800.0, t)));
    h.moveNoise  = std::max(0, int(std::lround(double(h.moveError) * 0.35)));
    return h;
}

std::string botId(int elo, const std::string& styleId) {
    return "v" + std::to_string(elo) + "-" + styleId;
}

const StyleDef* findStyle(const std::string& idOrName) {
    std::string key = idOrName;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    // Keep saved UCI settings and v<elo>-<oldstyle> bot IDs compatible.
    static const char* legacy[] = {"furchtsam", "vorsichtig", "ausgeglichen", "angriffslustig", "stier"};
    for (size_t i = 0; i < g_styles.size(); ++i)
        if (key == g_styles[i].id || key == legacy[i])
            return &g_styles[i];
    return nullptr;
}

const StyleDef* styleFromBotId(const std::string& botIdText) {
    const size_t dash = botIdText.find('-');
    if (dash == std::string::npos)
        return nullptr;
    return findStyle(botIdText.substr(dash + 1));
}

int botEloFromId(const std::string& botIdText, int fallback) {
    if (botIdText.size() < 2 || botIdText[0] != 'v')
        return fallback;
    const size_t dash = botIdText.find('-');
    const std::string digits = botIdText.substr(1, dash == std::string::npos ? std::string::npos
                                                                             : dash - 1);
    if (digits.empty())
        return fallback;
    for (char c : digits)
        if (c < '0' || c > '9')
            return fallback;
    const int elo = std::atoi(digits.c_str());
    return (elo >= ELO_MIN && elo <= ELO_MAX) ? elo : fallback;
}

std::string botDisplay(int elo, const StyleDef& style) {
    const int index = (elo - ELO_MIN) / ELO_STEP;
    const std::vector<const char*>& pool = g_botNames[size_t(style.level - 1)];
    const std::string nickname = pool[size_t((index / 2) % int(pool.size()))];
    return nickname + " " + std::to_string(elo);
}

std::string botTags(const StyleDef& style) { return style.tags; }

bool applyStyle(const std::string& idOrName) {
    const StyleDef* style = findStyle(idOrName);
    if (!style)
        return false;

    reset_weights();  // neutral baseline
    for (const StyleWeight& w : style->weights)
        set_weight(w.name, w.value);
    return true;
}

namespace {

// Maps a knowledge term name ("Group.Pst", "Material.Pawn", ...) to a group.
int groupOfTerm(const std::string& term) {
    if (term.rfind("Group.", 0) == 0) {
        const std::string name = term.substr(6);
        if (name == "Material")   return G_MATERIAL;
        if (name == "Pst")        return G_PST;
        if (name == "Mobility")   return G_MOBILITY;
        if (name == "KingSafety") return G_KINGSAFETY;
        if (name == "Pawns")      return G_PAWNS;
        if (name == "Rooks")      return G_ROOKS;
        if (name == "Pieces")     return G_PIECES;
        if (name == "Threats")    return G_THREATS;
        if (name == "Space")      return G_SPACE;
        if (name == "Initiative") return G_INITIATIVE;
        if (name == "Aggression") return G_AGGRESSION;
        if (name == "Complexity") return G_COMPLEXITY;
        if (name == "Drawish")    return G_DRAWISH;
        if (name == "Endgame")    return G_ENDGAME;
    }
    if (term.rfind("Material.", 0) == 0) return G_MATERIAL;
    if (term.rfind("Pawns.", 0) == 0)    return G_PAWNS;
    if (term.rfind("Rooks.", 0) == 0)    return G_ROOKS;
    return -1;
}

int styleWeightValue(const StyleDef& style, const std::string& name, int fallback) {
    for (const StyleWeight& w : style.weights)
        if (name == w.name)
            return w.value;
    return fallback;
}

}  // namespace

void applyElo(int elo) {
    refresh_eval_weights();  // rebuild the cached values from the current weights

    const int floorPercent = std::max(0, std::min(100, weight_value("Elo.KnowledgeFloor")));

    for (int g = 0; g < GROUP_NB; ++g)
        EW.knowledge[g] = 100;

    for (const KnowledgeRange& k : g_knowledge) {
        const int group = groupOfTerm(k.term);
        if (group < 0)
            continue;
        int percent;
        if (elo <= k.start)
            percent = floorPercent;
        else if (elo >= k.full)
            percent = 100;
        else
            percent = floorPercent + (100 - floorPercent) * (elo - k.start) / (k.full - k.start);
        EW.knowledge[group] = percent;
    }

    const EloHandicap h = eloHandicap(elo);
    g_strength.maxDepth   = h.maxDepth;
    g_strength.nodeLimit  = h.nodeLimit;
    g_strength.evalNoise  = h.moveError / 2;
    g_strength.moveNoise  = h.moveNoise;
    g_strength.blunderPct = int(std::lround(h.blunderPct));
}

void clearStrength() {
    g_strength = Strength{};
    for (int g = 0; g < GROUP_NB; ++g)
        EW.knowledge[g] = 100;
}

bool applyBoldness(int boldnessTenths) {
    const int clamped = std::max(10, std::min(50, boldnessTenths));
    const int lower   = clamped / 10;                 // 1..5
    const int upper   = std::min(5, lower + 1);
    const int blend   = clamped % 10;                 // 0..9, weight of the upper style

    const StyleDef& styleA = g_styles[size_t(lower - 1)];
    const StyleDef& styleB = g_styles[size_t(upper - 1)];

    // Start from the lower profile, then blend towards the upper one.
    reset_weights();
    for (const StyleWeight& w : styleA.weights)
        set_weight(w.name, w.value);

    if (blend == 0 || upper == lower)
        return true;

    std::vector<std::string> names;
    for (const StyleWeight& w : styleA.weights)
        names.emplace_back(w.name);
    for (const StyleWeight& w : styleB.weights) {
        bool present = false;
        for (const std::string& n : names)
            if (n == w.name)
                present = true;
        if (!present)
            names.emplace_back(w.name);
    }

    for (const std::string& name : names) {
        const int base   = weight_value(name);
        const int target = styleWeightValue(styleB, name, base);
        set_weight(name, base + (target - base) * blend / 10);
    }
    return true;
}


int generateDataFiles(const std::string& dataDir, std::string* error) {
    const auto fail = [&](const std::string& text) {
        if (error) *error = text;
        return -1;
    };

    // Guard: every style weight must exist in the registry.
    for (const StyleDef& s : g_styles)
        for (const StyleWeight& w : s.weights)
            if (weight_index(w.name) < 0)
                return fail(std::string("style '") + s.id + "' uses unknown weight '" + w.name + "'");

    // ---------------------------------------------------- weights_default.txt
    {
        std::ofstream out(dataDir + "/weights_default.txt");
        if (!out)
            return fail("cannot write " + dataDir + "/weights_default.txt");
        out << "# Virgin weights - every parameter of the engine\n"
            << "# Format: Name = value      (comments start with # or ;)\n"
            << "# Reload at runtime: setoption name ReloadWeights\n\n";
        for (int i = 0; i < weight_count(); ++i) {
            const WeightDef& d = weight_def(i);
            out << d.name << " = " << d.def << "   # " << d.min << ".." << d.max << "\n";
        }
    }

    // ------------------------------------------------------------ styles.txt
    {
        std::ofstream out(dataDir + "/styles.txt");
        if (!out)
            return fail("cannot write " + dataDir + "/styles.txt");
        out << "# Virgin style profiles - five levels of boldness\n"
            << "# 1 = Fortress ... 5 = Thunder\n\n";
        for (const StyleDef& s : g_styles) {
            out << "[style." << s.id << "]\n"
                << "# level = " << s.level << "\n"
                << "# name  = " << s.name << "\n"
                << "# desc  = " << s.desc << "\n";
            for (const StyleWeight& w : s.weights)
                out << w.name << " = " << w.value << "\n";
            out << "\n";
        }
    }

    // ------------------------------------------------------ elo_profile.txt
    {
        std::ofstream out(dataDir + "/elo_profile.txt");
        if (!out)
            return fail("cannot write " + dataDir + "/elo_profile.txt");
        out << "# Virgin strength handicap per Elo level\n"
            << "# Knowledge.<term> = start:full  (Elo where the term switches on / is complete)\n\n";
        for (int elo : eloLevels()) {
            const EloHandicap h = eloHandicap(elo);
            out << "[elo." << elo << "]\n"
                << "MaxDepth = " << h.maxDepth << "\n"
                << "NodeLimit = " << h.nodeLimit << "\n"
                << "MoveError = " << h.moveError << "\n"
                << "BlunderPct = " << formatOneDecimal(h.blunderPct) << "\n"
                << "NpsTarget = " << h.npsTarget << "\n"
                << "MoveNoise = " << h.moveNoise << "\n";
            for (const KnowledgeRange& k : g_knowledge)
                out << "Knowledge." << k.term << " = " << k.start << ":" << k.full << "\n";
            out << "\n";
        }
    }

    // ------------------------------------------------------------- bots.txt
    const std::vector<int> levels = eloLevels();
    {
        std::ofstream out(dataDir + "/bots.txt");
        if (!out)
            return fail("cannot write " + dataDir + "/bots.txt");
        out << "# Virgin bot catalog - id elo style \"display name\" tags\n\n";
        for (int elo : levels)
            for (const StyleDef& s : g_styles)
                out << botId(elo, s.id) << " " << elo << " " << s.id << " \""
                    << botDisplay(elo, s) << "\" " << s.tags << "\n";
    }

    // ------------------------------------------------------------ bots.json
    {
        std::ofstream out(dataDir + "/bots.json");
        if (!out)
            return fail("cannot write " + dataDir + "/bots.json");

        const auto writeTags = [&out](const char* tags) {
            std::istringstream stream(tags);
            std::string tag;
            bool first = true;
            while (std::getline(stream, tag, ',')) {
                out << (first ? "" : ", ") << "\"" << jsonEscape(tag) << "\"";
                first = false;
            }
        };

        out << "{\n \"version\": 1,\n \"engine\": \"Virgin\",\n"
            << " \"eloRange\": {\"min\": " << ELO_MIN << ", \"max\": " << ELO_MAX
            << ", \"step\": " << ELO_STEP << "},\n"
            << " \"botCount\": " << levels.size() * g_styles.size() << ",\n"
            << " \"styleCount\": " << g_styles.size() << ",\n"
            << " \"styles\": [\n";

        for (size_t i = 0; i < g_styles.size(); ++i) {
            const StyleDef& s = g_styles[i];
            out << "  {\"id\": \"" << s.id << "\", \"level\": " << s.level << ", \"name\": \""
                << jsonEscape(s.name) << "\", \"desc\": \"" << jsonEscape(s.desc)
                << "\", \"tags\": [";
            writeTags(s.tags);
            out << "], \"weights\": {";
            for (size_t w = 0; w < s.weights.size(); ++w)
                out << (w ? ", " : "") << "\"" << s.weights[w].name << "\": "
                    << s.weights[w].value;
            out << "}}" << (i + 1 < g_styles.size() ? "," : "") << "\n";
        }

        out << " ],\n \"eloProfile\": [\n";
        for (size_t i = 0; i < levels.size(); ++i) {
            const EloHandicap h = eloHandicap(levels[i]);
            out << "  {\"elo\": " << levels[i] << ", \"MaxDepth\": " << h.maxDepth
                << ", \"NodeLimit\": " << h.nodeLimit << ", \"MoveError\": " << h.moveError
                << ", \"BlunderPct\": " << formatOneDecimal(h.blunderPct)
                << ", \"NpsTarget\": " << h.npsTarget << ", \"MoveNoise\": " << h.moveNoise
                << "}" << (i + 1 < levels.size() ? "," : "") << "\n";
        }

        out << " ],\n \"knowledge\": {";
        for (size_t i = 0; i < g_knowledge.size(); ++i)
            out << (i ? ", " : "") << "\"" << g_knowledge[i].term << "\": {\"start\": "
                << g_knowledge[i].start << ", \"full\": " << g_knowledge[i].full << "}";
        out << "},\n \"bots\": [\n";

        bool firstBot = true;
        for (int elo : levels) {
            for (const StyleDef& s : g_styles) {
                out << (firstBot ? "" : ",\n")
                    << "  {\"id\": \"" << botId(elo, s.id) << "\", \"elo\": " << elo
                    << ", \"style\": \"" << s.id << "\", \"styleLevel\": " << s.level
                    << ", \"styleName\": \"" << jsonEscape(s.name) << "\", \"display\": \""
                    << jsonEscape(botDisplay(elo, s)) << "\", \"tags\": [";
                writeTags(s.tags);
                out << "]}";
                firstBot = false;
            }
        }
        out << "\n ]\n}\n";
    }

    return 5;
}


