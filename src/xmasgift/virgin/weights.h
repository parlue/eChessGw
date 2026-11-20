// Virgin - UCI chess engine
// Central weight registry. Every evaluation / style parameter is registered
// here, exposed as a UCI option and can be overridden from a weights file.
#pragma once

#include <string>

struct WeightDef {
    const char* name;  // UCI safe, "Group.Name"
    int         def;
    int         min;
    int         max;
};

extern const WeightDef WEIGHT_DEFS[];
extern const int       WEIGHT_COUNT;

void        init_weights();
int         weight_index(const std::string& name);  // -1 if unknown
int         weight_value(const std::string& name);
bool        set_weight(const std::string& name, int value);
bool        set_weight_index(int index, int value);
int         weight_count();
const WeightDef& weight_def(int index);
std::string weights_report();
void        reset_weights();

bool load_weights_file(const std::string& path, std::string* error);
bool save_weights_file(const std::string& path, std::string* error);

// Evaluation groups. Used for the group multipliers and for the knowledge
// scaling of the Elo profile (a 700 Elo bot simply does not know a term yet).
enum EvalGroup : int {
    G_MATERIAL, G_PST, G_MOBILITY, G_KINGSAFETY, G_PAWNS, G_ROOKS, G_PIECES,
    G_THREATS, G_SPACE, G_INITIATIVE, G_AGGRESSION, G_COMPLEXITY, G_DRAWISH,
    G_ENDGAME, GROUP_NB
};

// Cached values for the hot evaluation path. Refreshed by refresh_eval_weights()
// whenever a weight changes (setoption) or a weights file is reloaded.
struct EvalWeights {
    int matMg[6];
    int matEg[6];

    int pstPawnMgAdvance, pstPawnEgAdvance, pstKnightCenter, pstKnightRim;
    int pstBishopCenter, pstBishopDiagonal, pstRookRank7, pstRookFileCenter;
    int pstQueenCenter, pstKingMgShelter, pstKingMgCenter, pstKingEgCenter;

    int groupMaterial, groupPst, groupMobility, groupKingSafety, groupPawns;
    int groupRooks, groupPieces, groupThreats, groupSpace, groupInitiative;
    int groupAggression, groupComplexity, groupDrawish, groupEndgame;

    int knowledge[GROUP_NB];  // percent, from the Elo profile (100 = full)

    int tempo, contempt;
    int moveNoise;  // centipawn window for random move choice (style / strength)
};

extern EvalWeights EW;
void refresh_eval_weights();

// Cached values of the evaluation terms, filled by refresh_eval_weights().
// The evaluation reads these instead of doing string lookups in the hot path.
struct TermWeights {
    int mobility[4];  // KNIGHT, BISHOP, ROOK, QUEEN

    int pawnStorm;
    int attackUnits, saturation, shieldMissing, shieldPawn;
    int openFileNearKing, semiOpenNearKing;
    int tropismN, tropismB, tropismR, tropismQ, safeCheck, uncastledMg, kingRingAttack;

    int pawnsDoubled, pawnsIsolated, pawnsBackward, pawnsConnected;
    int pawnsPassedMg, pawnsPassedEg, pawnsCandidate, pawnsDoubledVsKing;

    int rooksOpenFile, rooksSemiOpenFile, rooksOnSeventh, rooksConnected;
    int rooksTrapped, rooksOpenFileVsKing;

    int piecesKnightOutpost, piecesBishopPair, piecesBadBishop, piecesFianchetto;

    int threatsAttackedByLower, threatsHanging, threatsPinned, threatsDiscovered;

    int spacePerSafeSquare;

    int initiativeTempo, initiativeProgress;

    int aggressionKingAttack, aggressionPawnStorm, aggressionInitiative;
    int aggressionSacrifice, aggressionAvoidQueenTrade;

    int complexityWeight;

    int drawishOppositeBishops, drawishInsufficient;
    int drawishRookMinorVsRook, drawishRookVsMinor, drawishTwoKnights;

    int endgameKingActivity, endgamePassedKingProximity;
};

extern TermWeights TW;
