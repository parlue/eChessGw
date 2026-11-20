// Virgin - UCI chess engine
// Weight registry implementation.
#include "weights.h"

#include "eval.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------------------
// The registry. Every entry is a tunable parameter:
//   name            unique, UCI safe (used as "option name <name>")
//   def             default value
//   min, max        clamp range (protects against broken configurations)
//
// Term weights are in centipawns, group multipliers are percentages.
// ---------------------------------------------------------------------------
const WeightDef WEIGHT_DEFS[] = {
    // ------------------------------------------------------------- material
    // Calibrated after the consensus of Arasan 23.4 (ref/arasan-v23.4.0,
    // params.cpp: 128 = one pawn), PeSTO and the usual hand crafted engines.
    // All three put the minors at ~390-410, the rook at ~590-620 and the queen
    // at ~1200, while our first guess was much flatter (320/330/500/950). The
    // tuner had already pushed pawn/rook down and queen up, in the same
    // direction.
    {"Material.Pawn.Mg",      100,    0, 1600},
    {"Material.Pawn.Eg",      107,    0, 1600},
    {"Material.Knight.Mg",    395,    0, 1600},
    {"Material.Knight.Eg",    377,    0, 1600},
    {"Material.Bishop.Mg",    405,    0, 1600},
    {"Material.Bishop.Eg",    400,    0, 1600},
    {"Material.Rook.Mg",      595,    0, 1600},
    {"Material.Rook.Eg",      620,    0, 1600},
    {"Material.Queen.Mg",    1225,    0, 2000},
    {"Material.Queen.Eg",    1180,    0, 2000},

    // ------------------------------------------- piece square table shapes
    {"Pst.PawnMgAdvance",       3,  -40,  40},
    {"Pst.PawnEgAdvance",       6,  -40,  40},
    {"Pst.KnightCenter",       10,  -40,  40},
    {"Pst.KnightRim",         -12,  -60,  20},
    {"Pst.BishopCenter",        6,  -40,  40},
    {"Pst.BishopDiagonal",      2,  -40,  40},
    {"Pst.RookRank7",           0,  -40,  60},
    {"Pst.RookFileCenter",      4,  -40,  40},
    {"Pst.QueenCenter",         3,  -40,  40},
    {"Pst.KingMgShelter",      22,  -40,  80},
    {"Pst.KingMgCenter",      -12,  -60,  40},
    {"Pst.KingEgCenter",       12,  -40,  60},

    // ------------------------------------------------------------- mobility
    {"Mobility.Knight",         4,    0,  20},
    {"Mobility.Bishop",         5,    0,  20},
    {"Mobility.Rook",           2,    0,  20},
    {"Mobility.Queen",          1,    0,  20},

    // ---------------------------------------------------------- king safety
    {"KingSafety.AttackUnits",       100,    0,   400},
    {"KingSafety.Saturation",        256,   64, 4096},
    {"KingSafety.PawnStorm",         -10,  -80,     0},
    {"KingSafety.ShieldMissing",     -22, -120,     0},
    {"KingSafety.ShieldPawn",          8,    0,    60},
    {"KingSafety.OpenFileNearKing",  -28, -160,     0},
    {"KingSafety.SemiOpenNearKing",  -14, -160,     0},
    {"KingSafety.TropismN",           12,    0,    60},
    {"KingSafety.TropismB",           10,    0,    60},
    {"KingSafety.TropismR",            8,    0,    60},
    {"KingSafety.TropismQ",            6,    0,    60},
    {"KingSafety.SafeCheck",          18,    0,    80},
    {"KingSafety.UncastledMg",       -30, -200,     0},
    {"KingSafety.KingRingAttack",      6,    0,    40},

    // --------------------------------------------------------- pawn structure
    {"Pawns.Doubled",          -12, -120,  20},
    {"Pawns.Isolated",         -15, -120,  20},
    {"Pawns.Backward",          -8, -120,  20},
    {"Pawns.Connected",          4,    0,  60},
    {"Pawns.PassedMg",         100,  -40, 200},
    {"Pawns.PassedEg",         100,  -40, 300},
    {"Pawns.Candidate",          8,    0,  80},
    {"Pawns.DoubledVsKing",    -18, -120,  20},

    // ------------------------------------------------------------------ rooks
    {"Rooks.OpenFile",          25,  -40, 160},
    {"Rooks.SemiOpenFile",      12,  -40, 160},
    {"Rooks.OnSeventh",          8,  -40, 120},
    {"Rooks.Connected",         10,    0,  80},
    {"Rooks.Trapped",          -40, -200,   0},
    {"Rooks.OpenFileVsKing",    30,  -40, 200},

    // --------------------------------------------------- pieces and threats
    {"Pieces.KnightOutpost",    25,    0, 120},
    {"Pieces.BishopPair",       30,    0, 120},
    {"Pieces.BadBishop",        -3,  -20,   0},
    {"Pieces.Fianchetto",       12,    0,  80},
    {"Threats.AttackedByLower", 20,    0, 120},
    {"Threats.Hanging",         45,    0, 200},
    {"Threats.Pinned",          15,    0, 120},
    {"Threats.Discovered",      18,    0, 120},

    // ------------------------------------------------------- space / tempo
    {"Space.PerSafeSquare",      8,    0,  40},
    {"Initiative.Tempo",        10,    0,  60},
    {"Initiative.Progress",     12,    0,  80},

    // -------------------------------------------- aggression (style driving)
    {"Aggression.KingAttack",         100,    0, 400},
    {"Aggression.PawnStorm",          100,    0, 400},
    {"Aggression.Initiative",         100,    0, 400},
    {"Aggression.SacrificeTolerance", 100,    0, 400},
    {"Aggression.AvoidQueenTrade",    100,    0, 400},

    // ------------------------------------------------------------ complexity
    {"Complexity.Weight",       100,    0, 400},

    // -------------------------------------------------------------- drawish
    {"Drawish.OppositeBishops",  60,    0, 200},
    {"Drawish.RookMinorVsRook",  12,    0, 100},
    {"Drawish.RookVsMinor",      20,    0, 100},
    {"Drawish.TwoKnights",        8,    0, 100},
    {"Drawish.Insufficient",      0,    0, 200},

    // -------------------------------------------------------------- endgame
    {"Endgame.KingActivity",         100,    0, 400},
    {"Endgame.PassedKingProximity",  100,    0, 400},

    // ------------------------------------------------- group multipliers (%)
    {"Group.Material",     100,    0, 400},
    {"Group.Pst",          100,    0, 400},
    {"Group.Mobility",     100,    0, 400},
    {"Group.KingSafety",   100,    0, 400},
    {"Group.Pawns",        100,    0, 400},
    {"Group.Rooks",        100,    0, 400},
    {"Group.Pieces",       100,    0, 400},
    {"Group.Threats",      100,    0, 400},
    {"Group.Space",        100,    0, 400},
    {"Group.Initiative",   100,    0, 400},
    {"Group.Aggression",   100,    0, 400},
    {"Group.Complexity",   100,    0, 400},
    {"Group.Drawish",      100,    0, 400},
    {"Group.Endgame",      100,    0, 400},

    // ----------------------------------------------------------------- misc
    {"Misc.Contempt",        0, -100, 100},
    {"Misc.MoveNoise",       0,    0, 400},

    // ------------------------------------------------------------ Elo profile
    // Minimum knowledge for the weakest bots: a 700 Elo bot that knows nothing
    // at all would just shuffle, so every term keeps at least this percentage.
    {"Elo.KnowledgeFloor",  25,    0, 100},
};

const int WEIGHT_COUNT = int(sizeof(WEIGHT_DEFS) / sizeof(WEIGHT_DEFS[0]));

namespace {

std::vector<int> g_values;

std::string trimmed(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return "";
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

}  // namespace

EvalWeights EW;
TermWeights TW;

void init_weights() {
    g_values.assign(size_t(WEIGHT_COUNT), 0);
    reset_weights();
}

void reset_weights() {
    for (int i = 0; i < WEIGHT_COUNT; ++i)
        g_values[size_t(i)] = WEIGHT_DEFS[i].def;
    refresh_eval_weights();
}

int weight_index(const std::string& name) {
    for (int i = 0; i < WEIGHT_COUNT; ++i)
        if (name == WEIGHT_DEFS[i].name)
            return i;
    return -1;
}

int weight_value(const std::string& name) {
    const int i = weight_index(name);
    if (i < 0)
        return 0;
    // Defensive: before init_weights() ran, fall back to the registry default.
    return (size_t(i) < g_values.size()) ? g_values[size_t(i)] : WEIGHT_DEFS[i].def;
}

bool set_weight_index(int index, int value) {
    if (index < 0 || index >= WEIGHT_COUNT)
        return false;
    const WeightDef& d = WEIGHT_DEFS[index];
    g_values[size_t(index)] = std::max(d.min, std::min(d.max, value));
    refresh_eval_weights();
    return true;
}

bool set_weight(const std::string& name, int value) {
    const int i = weight_index(name);
    return i < 0 ? false : set_weight_index(i, value);
}

int weight_count() { return WEIGHT_COUNT; }

const WeightDef& weight_def(int index) { return WEIGHT_DEFS[index]; }

std::string weights_report() {
    std::ostringstream os;
    for (int i = 0; i < WEIGHT_COUNT; ++i)
        os << WEIGHT_DEFS[i].name << " = " << g_values[size_t(i)] << "   # "
           << WEIGHT_DEFS[i].min << ".." << WEIGHT_DEFS[i].max
           << ", default " << WEIGHT_DEFS[i].def << "\n";
    return os.str();
}

bool load_weights_file(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open '" + path + "'";
        return false;
    }

    std::string line;
    int lineNo = 0;
    int applied = 0;
    std::vector<std::string> unknown;

    while (std::getline(in, line)) {
        ++lineNo;
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos)
            line = line.substr(0, comment);
        line = trimmed(line);
        if (line.empty() || line[0] == '[')
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            if (error) *error = "line " + std::to_string(lineNo) + ": expected 'name = value'";
            return false;
        }

        const std::string name = trimmed(line.substr(0, eq));
        const std::string valueText = trimmed(line.substr(eq + 1));
        const int index = weight_index(name);
        if (index < 0) {
            unknown.push_back(name);
            continue;
        }
        set_weight_index(index, std::atoi(valueText.c_str()));
        ++applied;
    }

    if (error && !unknown.empty()) {
        std::string msg = "applied " + std::to_string(applied) + " weights, ignored unknown:";
        for (const std::string& n : unknown)
            msg += " " + n;
        *error = msg;
    }
    return true;
}

bool save_weights_file(const std::string& path, std::string* error) {
    std::ofstream out(path);
    if (!out) {
        if (error) *error = "cannot write '" + path + "'";
        return false;
    }
    out << "# Virgin weights file - generated " << WEIGHT_COUNT << " parameters\n";
    out << weights_report();
    return true;
}

void refresh_eval_weights() {
    const auto w = [](const char* name) { return weight_value(name); };

    EW.matMg[PAWN]   = w("Material.Pawn.Mg");
    EW.matMg[KNIGHT] = w("Material.Knight.Mg");
    EW.matMg[BISHOP] = w("Material.Bishop.Mg");
    EW.matMg[ROOK]   = w("Material.Rook.Mg");
    EW.matMg[QUEEN]  = w("Material.Queen.Mg");
    EW.matMg[KING]   = 0;

    EW.matEg[PAWN]   = w("Material.Pawn.Eg");
    EW.matEg[KNIGHT] = w("Material.Knight.Eg");
    EW.matEg[BISHOP] = w("Material.Bishop.Eg");
    EW.matEg[ROOK]   = w("Material.Rook.Eg");
    EW.matEg[QUEEN]  = w("Material.Queen.Eg");
    EW.matEg[KING]   = 0;

    EW.pstPawnMgAdvance  = w("Pst.PawnMgAdvance");
    EW.pstPawnEgAdvance  = w("Pst.PawnEgAdvance");
    EW.pstKnightCenter   = w("Pst.KnightCenter");
    EW.pstKnightRim      = w("Pst.KnightRim");
    EW.pstBishopCenter   = w("Pst.BishopCenter");
    EW.pstBishopDiagonal = w("Pst.BishopDiagonal");
    EW.pstRookRank7      = w("Pst.RookRank7");
    EW.pstRookFileCenter = w("Pst.RookFileCenter");
    EW.pstQueenCenter    = w("Pst.QueenCenter");
    EW.pstKingMgShelter  = w("Pst.KingMgShelter");
    EW.pstKingMgCenter   = w("Pst.KingMgCenter");
    EW.pstKingEgCenter   = w("Pst.KingEgCenter");

    EW.groupMaterial   = w("Group.Material");
    EW.groupPst        = w("Group.Pst");
    EW.groupMobility   = w("Group.Mobility");
    EW.groupKingSafety = w("Group.KingSafety");
    EW.groupPawns      = w("Group.Pawns");
    EW.groupRooks      = w("Group.Rooks");
    EW.groupPieces     = w("Group.Pieces");
    EW.groupThreats    = w("Group.Threats");
    EW.groupSpace      = w("Group.Space");
    EW.groupInitiative = w("Group.Initiative");
    EW.groupAggression = w("Group.Aggression");
    EW.groupComplexity = w("Group.Complexity");
    EW.groupDrawish    = w("Group.Drawish");
    EW.groupEndgame    = w("Group.Endgame");

    EW.tempo     = w("Initiative.Tempo");
    EW.contempt  = w("Misc.Contempt");
    EW.moveNoise = w("Misc.MoveNoise");

    // Knowledge is owned by the Elo profile; keep the current values, but make
    // sure they are sane on the very first call.
    for (int g = 0; g < GROUP_NB; ++g)
        if (EW.knowledge[g] <= 0 || EW.knowledge[g] > 100)
            EW.knowledge[g] = 100;

    // ------------------------------------------------ term weights (cached)
    TW.mobility[0] = w("Mobility.Knight");
    TW.mobility[1] = w("Mobility.Bishop");
    TW.mobility[2] = w("Mobility.Rook");
    TW.mobility[3] = w("Mobility.Queen");

    TW.attackUnits       = w("KingSafety.AttackUnits");
    TW.saturation        = std::max(1, w("KingSafety.Saturation"));
    TW.pawnStorm          = w("KingSafety.PawnStorm");
    TW.shieldMissing     = w("KingSafety.ShieldMissing");
    TW.shieldPawn        = w("KingSafety.ShieldPawn");
    TW.openFileNearKing  = w("KingSafety.OpenFileNearKing");
    TW.semiOpenNearKing  = w("KingSafety.SemiOpenNearKing");
    TW.tropismN          = w("KingSafety.TropismN");
    TW.tropismB          = w("KingSafety.TropismB");
    TW.tropismR          = w("KingSafety.TropismR");
    TW.tropismQ          = w("KingSafety.TropismQ");
    TW.safeCheck         = w("KingSafety.SafeCheck");
    TW.uncastledMg       = w("KingSafety.UncastledMg");
    TW.kingRingAttack    = w("KingSafety.KingRingAttack");

    TW.pawnsDoubled       = w("Pawns.Doubled");
    TW.pawnsIsolated      = w("Pawns.Isolated");
    TW.pawnsBackward      = w("Pawns.Backward");
    TW.pawnsConnected     = w("Pawns.Connected");
    TW.pawnsPassedMg      = w("Pawns.PassedMg");
    TW.pawnsPassedEg      = w("Pawns.PassedEg");
    TW.pawnsCandidate     = w("Pawns.Candidate");
    TW.pawnsDoubledVsKing = w("Pawns.DoubledVsKing");

    TW.rooksOpenFile       = w("Rooks.OpenFile");
    TW.rooksSemiOpenFile   = w("Rooks.SemiOpenFile");
    TW.rooksOnSeventh      = w("Rooks.OnSeventh");
    TW.rooksConnected      = w("Rooks.Connected");
    TW.rooksTrapped        = w("Rooks.Trapped");
    TW.rooksOpenFileVsKing = w("Rooks.OpenFileVsKing");

    TW.piecesKnightOutpost = w("Pieces.KnightOutpost");
    TW.piecesBishopPair    = w("Pieces.BishopPair");
    TW.piecesBadBishop     = w("Pieces.BadBishop");
    TW.piecesFianchetto    = w("Pieces.Fianchetto");

    TW.threatsAttackedByLower = w("Threats.AttackedByLower");
    TW.threatsHanging         = w("Threats.Hanging");
    TW.threatsPinned          = w("Threats.Pinned");
    TW.threatsDiscovered      = w("Threats.Discovered");

    TW.spacePerSafeSquare = w("Space.PerSafeSquare");

    TW.initiativeTempo    = w("Initiative.Tempo");
    TW.initiativeProgress = w("Initiative.Progress");

    TW.aggressionKingAttack     = w("Aggression.KingAttack");
    TW.aggressionPawnStorm      = w("Aggression.PawnStorm");
    TW.aggressionInitiative     = w("Aggression.Initiative");
    TW.aggressionSacrifice      = w("Aggression.SacrificeTolerance");
    TW.aggressionAvoidQueenTrade = w("Aggression.AvoidQueenTrade");

    TW.complexityWeight = w("Complexity.Weight");

    TW.drawishOppositeBishops = w("Drawish.OppositeBishops");
    TW.drawishRookMinorVsRook = w("Drawish.RookMinorVsRook");
    TW.drawishRookVsMinor = w("Drawish.RookVsMinor");
    TW.drawishTwoKnights = w("Drawish.TwoKnights");
    TW.drawishInsufficient    = w("Drawish.Insufficient");

    TW.endgameKingActivity        = w("Endgame.KingActivity");
    TW.endgamePassedKingProximity = w("Endgame.PassedKingProximity");

    build_pst_tables();
}


