// Virgin - UCI chess engine
// Full handcrafted evaluation: material, piece square tables, mobility, king
// safety, pawn structure, rooks, pieces, threats, space, initiative,
// aggression, complexity, endgame and drawish scaling.
// Every term is a registered weight (see weights.h) and can be changed from
// the outside - by UCI option, by weights file or by a bot profile.
#include "eval.h"

#include "bitboard.h"
#include "position.h"
#include "movegen.h"
#include "pst_pesto.h"
#include "weights.h"

#include <algorithm>
#include <cmath>
#include <ostream>

namespace {

// Piece square tables, from the white point of view.
int PST_MG[PIECE_TYPE_NB][SQUARE_NB];
int PST_EG[PIECE_TYPE_NB][SQUARE_NB];

// The published PeSTO tables are scaled so that a pawn is worth 82 (mg) / 94
// (eg); this engine's material scale puts a pawn at 100. Rescaling the tables
// keeps the piece square / material balance of the published evaluation.
constexpr int PESTO_SCALE_NUM = 100;
constexpr int PESTO_SCALE_DEN = 88;
constexpr int pestoScale(int v) { return (v * PESTO_SCALE_NUM) / PESTO_SCALE_DEN; }

// Geometry tables (independent of the weights).
Bitboard PassedMask[COLOR_NB][SQUARE_NB];  // squares in front on own and adjacent files
Bitboard FrontSpan[COLOR_NB][SQUARE_NB];   // squares strictly in front on the own file
Bitboard KingZone[SQUARE_NB];              // king square plus its neighbours

inline int centerDistance(int f, int r) {
    const int fd = std::min(std::abs(f - 3), std::abs(f - 4));
    const int rd = std::min(std::abs(r - 3), std::abs(r - 4));
    return std::max(fd, rd);  // 0 = centre, 3 = corner
}

inline Score tapered(Score mg, Score eg, int phase) {
    return (mg * phase + eg * (24 - phase)) / 24;
}

inline Bitboard pawnAttacksOf(Color c, Bitboard pawns) {
    return c == WHITE ? (shift_ne(pawns) | shift_nw(pawns)) : (shift_se(pawns) | shift_sw(pawns));
}


// ------------------------------------------------------------------ terms
constexpr int TERM_NB = 13;
enum Term {
    T_MATERIAL, T_PST, T_MOBILITY, T_KINGSAFETY, T_PAWNS, T_ROOKS, T_PIECES,
    T_THREATS, T_SPACE, T_INITIATIVE, T_ENDGAME, T_AGGRESSION, T_COMPLEXITY
};

int groupOf(Term t) {
    switch (t) {
    case T_MATERIAL: return G_MATERIAL;
    case T_PST: return G_PST;
    case T_MOBILITY: return G_MOBILITY;
    case T_KINGSAFETY: return G_KINGSAFETY;
    case T_PAWNS: return G_PAWNS;
    case T_ROOKS: return G_ROOKS;
    case T_PIECES: return G_PIECES;
    case T_THREATS: return G_THREATS;
    case T_SPACE: return G_SPACE;
    case T_INITIATIVE: return G_INITIATIVE;
    case T_ENDGAME: return G_ENDGAME;
    case T_AGGRESSION: return G_AGGRESSION;
    default: return G_COMPLEXITY;
    }
}

int groupMultiplier(int group) {
    switch (group) {
    case G_MATERIAL: return EW.groupMaterial;
    case G_PST: return EW.groupPst;
    case G_MOBILITY: return EW.groupMobility;
    case G_KINGSAFETY: return EW.groupKingSafety;
    case G_PAWNS: return EW.groupPawns;
    case G_ROOKS: return EW.groupRooks;
    case G_PIECES: return EW.groupPieces;
    case G_THREATS: return EW.groupThreats;
    case G_SPACE: return EW.groupSpace;
    case G_INITIATIVE: return EW.groupInitiative;
    case G_ENDGAME: return EW.groupEndgame;
    case G_AGGRESSION: return EW.groupAggression;
    default: return EW.groupComplexity;
    }
}

// Scales a value by the group multiplier and the Elo knowledge of that group.
inline Score scaleTerm(Score value, int group) {
    if (value == 0)
        return 0;
    return Score(value * groupMultiplier(group) * EW.knowledge[group] / 10000);
}

}  // namespace

void build_pst_tables() {
    // --------------------------------------------------- PeSTO base tables
    // Published, Texel tuned tables (Ronald Friederich, engine RofChade /
    // PeSTO, see src/pst_pesto.h). They replace the flat hand made shapes of
    // the first milestones. PeSTO indexes a8 = 0, this engine a1 = 0, hence
    // the flip.
    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
        for (int s = 0; s < SQUARE_NB; ++s) {
            const int ps  = s ^ 56;
            PST_MG[pt][s] = pestoScale(PESTO_MG[pt][ps]);
            PST_EG[pt][s] = pestoScale(PESTO_EG[pt][ps]);
        }
    }

    // -------------------------------------------- engine specific deviations
    // The registered Pst.* weights are added on top of the base tables. Their
    // defaults are small, so the base shape dominates; styles and the tuner
    // can still push the shapes around.
    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            const Square s  = make_square(f, r);
            const int    cd = centerDistance(f, r);
            const int    fd = std::min(f, 7 - f);

            PST_MG[PAWN][s] += EW.pstPawnMgAdvance * r;
            PST_EG[PAWN][s] += EW.pstPawnEgAdvance * r;

            const int rim = (fd == 0) ? EW.pstKnightRim : 0;
            PST_MG[KNIGHT][s] += EW.pstKnightCenter * (3 - cd) + rim;
            PST_EG[KNIGHT][s] += EW.pstKnightCenter * (3 - cd) / 2 + rim;

            const int diag = (f > 0 && f < 7 && r > 0 && r < 7) ? 1 : 0;
            PST_MG[BISHOP][s] += EW.pstBishopCenter * (3 - cd) + EW.pstBishopDiagonal * diag;
            PST_EG[BISHOP][s] += EW.pstBishopCenter * (3 - cd) / 2 + EW.pstBishopDiagonal * diag;

            const int rookDelta = (r == 6 ? EW.pstRookRank7 : 0) + (fd == 0 ? -EW.pstRookFileCenter : 0);
            PST_MG[ROOK][s] += rookDelta;
            PST_EG[ROOK][s] += rookDelta;

            PST_MG[QUEEN][s] += EW.pstQueenCenter * (3 - cd);
            // PeSTO's queen endgame table stays as it is.

            const int shelter = (r <= 1) ? EW.pstKingMgShelter : (r >= 4 ? -EW.pstKingMgShelter : 0);
            PST_MG[KING][s] += shelter + EW.pstKingMgCenter * (3 - cd)
                             + (fd == 0 ? -EW.pstKingMgShelter / 2 : 0);
            PST_EG[KING][s] += EW.pstKingEgCenter * (3 - cd);
        }
    }

    // --------------------------------------------------------- geometry
    for (int s = 0; s < SQUARE_NB; ++s) {
        const Square sq = Square(s);
        KingZone[sq]    = king_attacks(sq) | square_bb(sq);

        for (int c = 0; c < COLOR_NB; ++c) {
            const Color  col = Color(c);
            const Square rel = (col == WHITE) ? sq : flip_rank(sq);
            const int    f   = file_of(rel);
            const int    r   = rank_of(rel);

            Bitboard front  = 0;
            Bitboard passed = 0;
            for (int rr = r + 1; rr <= 7; ++rr) {
                const Bitboard line = square_bb(make_square(f, rr));
                front |= line;
                passed |= line;
                if (f > 0)
                    passed |= square_bb(make_square(f - 1, rr));
                if (f < 7)
                    passed |= square_bb(make_square(f + 1, rr));
            }
            FrontSpan[c][sq]  = (col == WHITE) ? front : flip_rank_bb(front);
            PassedMask[c][sq] = (col == WHITE) ? passed : flip_rank_bb(passed);
        }
    }
}

namespace {

// Everything that is computed once per evaluation and shared by both sides.
struct EvalContext {
    Bitboard occ = 0;
    Bitboard attackMap[COLOR_NB] = {0, 0};  // geometric attacks, including pinned men
    Bitboard defenseMap[COLOR_NB] = {0, 0}; // usable captures, restricted by absolute pins
    Bitboard attacksByType[COLOR_NB][PIECE_TYPE_NB] = {};   // per piece type
    Bitboard pawnAtt[COLOR_NB] = {0, 0};
    Bitboard passed[COLOR_NB]  = {0, 0};
    Bitboard attackedTwice[COLOR_NB] = {0, 0};
    Bitboard pieceAttacks[SQUARE_NB] = {};
    int      phase = 0;
};

EvalContext buildContext(const Position& pos) {
    EvalContext ctx;
    ctx.occ = pos.occupied();

    for (int c = 0; c < COLOR_NB; ++c) {
        const Color col = Color(c);
        const Square king = pos.kingSquare(col);
        const Bitboard kingAtt = king_attacks(king);
        Bitboard pinned = 0;
        Bitboard pinLines[SQUARE_NB]; // read only for a bit in pinned
        Bitboard snipers = (rook_attacks(king, 0) & (pos.pieces(~col, ROOK) | pos.pieces(~col, QUEEN)))
                         | (bishop_attacks(king, 0) & (pos.pieces(~col, BISHOP) | pos.pieces(~col, QUEEN)));
        while (snipers) {
            const Square sniper = pop_lsb(snipers);
            const Bitboard blockers = BetweenBB[king][sniper] & ctx.occ;
            if (popcount(blockers) == 1 && (blockers & pos.pieces(col))) {
                const Square blocker = lsb_square(blockers);
                pinned |= blockers;
                pinLines[blocker] = LineBB[king][sniper];
            }
        }
        ctx.attackMap[c] = pawnAttacksOf(col, pos.pieces(col, PAWN)) | kingAtt;
        ctx.defenseMap[c] = kingAtt;
        for (Bitboard pawns = pos.pieces(col, PAWN); pawns;) {
            const Square sq = pop_lsb(pawns);
            Bitboard attacks = pawn_attacks(col, sq);
            if (pinned & square_bb(sq))
                attacks &= pinLines[sq];
            ctx.pawnAtt[c] |= attacks;
            ctx.attackedTwice[c] |= ctx.defenseMap[c] & attacks;
            ctx.defenseMap[c] |= attacks;
        }
        ctx.attacksByType[c][PAWN]   = ctx.pawnAtt[c];
        ctx.attacksByType[c][KING]   = king_attacks(pos.kingSquare(col));
        for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
            for (Bitboard b = pos.pieces(col, PieceType(pt)); b;) {
                const Square sq = pop_lsb(b);
                Bitboard attacks = attacks_bb(PieceType(pt), sq, ctx.occ);
                ctx.attackMap[c] |= attacks; // retain geometric king exclusion semantics
                if (pinned & square_bb(sq))
                    attacks &= pinLines[sq];
                ctx.pieceAttacks[sq] = attacks;
                ctx.attacksByType[c][pt] |= attacks;
                ctx.attackedTwice[c] |= ctx.defenseMap[c] & attacks;
                ctx.defenseMap[c] |= attacks;
            }
            ctx.attackMap[c] |= ctx.attacksByType[c][pt];
        }

        Bitboard passed = 0;
        for (Bitboard b = pos.pieces(col, PAWN); b;) {
            const Square s = pop_lsb(b);
            if (!(PassedMask[col][s] & pos.pieces(~col, PAWN))
                && !(FrontSpan[col][s] & pos.pieces(col, PAWN)))
                passed |= square_bb(s);
        }
        ctx.passed[c] = passed;
    }

    for (int pt = KNIGHT; pt <= QUEEN; ++pt)
        ctx.phase += popcount(pos.pieces(PieceType(pt))) * (pt == ROOK ? 2 : pt == QUEEN ? 4 : 1);
    ctx.phase = std::min(ctx.phase, 24);
    return ctx;
}

// Include the next rank of entry squares in front of the defender. A hole
// on h3 matters to a king on g1 even though it lies outside the adjacent ring.
Bitboard kingDangerZone(Color defender, Square king) {
    Bitboard zone = KingZone[king];
    const int rank = rank_of(king) + (defender == WHITE ? 2 : -2);
    if (rank >= 0 && rank < 8)
        for (int file = std::max(0, file_of(king) - 1);
             file <= std::min(7, file_of(king) + 1); ++file)
            zone |= square_bb(make_square(file, rank));
    return zone;
}

// Attack potential of `col` against the enemy king, in "attack units".
int attackUnitsOf(const Position& pos, const EvalContext& ctx, Color col) {
    const Color  opp  = ~col;
    const Square eksq = pos.kingSquare(opp);
    const Bitboard zone = kingDangerZone(opp, eksq);


    int units = 0;
    units += TW.tropismN * popcount(ctx.attacksByType[col][KNIGHT] & zone);
    units += TW.tropismB * popcount(ctx.attacksByType[col][BISHOP] & zone);
    units += TW.tropismR * popcount(ctx.attacksByType[col][ROOK] & zone);
    units += TW.tropismQ * popcount(ctx.attacksByType[col][QUEEN] & zone);
    units += TW.kingRingAttack * popcount(pos.pieces(col) & king_attacks(eksq));

    // Count available checking destinations, not merely squares in the ring.
    // This is a static approximation: pins and discovered defences are left
    // to search. Pawn-defended destinations are deliberately excluded.
    const Bitboard safe = ~pos.pieces(col) & ~ctx.pawnAtt[opp]
                        & (~ctx.defenseMap[opp] | ctx.attackedTwice[col]);
    const Bitboard bishopChecks = bishop_attacks(eksq, ctx.occ);
    const Bitboard rookChecks = rook_attacks(eksq, ctx.occ);
    int checks = std::min(2, popcount(ctx.attacksByType[col][KNIGHT] & knight_attacks(eksq) & safe));
    checks += std::min(2, popcount(ctx.attacksByType[col][BISHOP] & bishopChecks & safe));
    checks += std::min(2, popcount(ctx.attacksByType[col][ROOK] & rookChecks & safe));
    checks += std::min(2, popcount(ctx.attacksByType[col][QUEEN] & (bishopChecks | rookChecks) & safe));
    units += TW.safeCheck * checks;

    return std::min(units, 600);
}

// Converts attack units into centipawns (quadratic, saturating).
inline Score dangerValue(int units) {
    return Score(units * units * TW.attackUnits / (TW.saturation * 100));
}

}  // namespace

namespace {

struct SideTerms {
    Score mg[TERM_NB] = {};
    Score eg[TERM_NB] = {};
};

void evalSide(const Position& pos, const EvalContext& ctx, Color col, const int units[COLOR_NB],
              SideTerms& t) {
    const Color    opp          = ~col;
    const Bitboard occ          = ctx.occ;
    const Bitboard own          = pos.pieces(col);
    const Bitboard ownPawns     = pos.pieces(col, PAWN);
    const Bitboard enemyPawns   = pos.pieces(opp, PAWN);
    const Bitboard ownPawnAtt   = ctx.pawnAtt[col];
    const Bitboard enemyPawnAtt = ctx.pawnAtt[opp];
    const Square   ksq          = pos.kingSquare(col);
    const Square   eksq         = pos.kingSquare(opp);

    // ------------------------------------------------- material and PST
    for (int pt = PAWN; pt < KING; ++pt) {
        const int n = popcount(pos.pieces(col, PieceType(pt)));
        if (n == 0)
            continue;
        t.mg[T_MATERIAL] += n * EW.matMg[pt];
        t.eg[T_MATERIAL] += n * EW.matEg[pt];
    }
    for (int pt = PAWN; pt < KING; ++pt)
        for (Bitboard b = pos.pieces(col, PieceType(pt)); b;) {
            const Square s   = pop_lsb(b);
            const Square rel = (col == WHITE) ? s : flip_rank(s);
            t.mg[T_PST] += PST_MG[pt][rel];
            t.eg[T_PST] += PST_EG[pt][rel];
        }
    {
        const Square rel = (col == WHITE) ? ksq : flip_rank(ksq);
        t.mg[T_PST] += PST_MG[KING][rel];
        t.eg[T_PST] += PST_EG[KING][rel] * TW.endgameKingActivity / 100;
    }

    // ---------------------------------------------------------- mobility
    // Shape learned from Arasan 23.4 (ref/arasan-v23.4.0/params.cpp, MIT):
    // their tuned tables (KNIGHT_MOBILITY, BISHOP_MOBILITY, ...) are *not*
    // linear from zero. A piece with about four safe moves is neutral, fewer
    // is clearly bad (a knight with none is worth about -66 cp in their
    // scale), and more grows only slowly. Our first version counted from zero
    // with a straight slope, so active pieces were worth far too much and idle
    // ones cost nothing. The registered weights stay per move - they now count
    // moves above or below the neutral point.
    constexpr int MOBILITY_NEUTRAL = 4;
    for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
        const int per = TW.mobility[pt - KNIGHT];
        if (per == 0)
            continue;
        int value = 0;
        for (Bitboard b = pos.pieces(col, PieceType(pt)); b;) {
            const Square s = pop_lsb(b);
            const int squares = popcount(ctx.pieceAttacks[s] & ~own & ~enemyPawnAtt);
            value += (squares - MOBILITY_NEUTRAL) * per;
            // Judge each minor separately: an active second bishop cannot
            // compensate for a trapped first bishop's lack of escape squares.
            if (pt == KNIGHT || pt == BISHOP) {
                int escapes = squares;
                if (enemyPawnAtt & square_bb(s)) {
                    // A pawn-attacked minor must be able to escape. Opposing
                    // minors can close the exits even when no pawn covers them.
                    // Capturing a rook or queen remains a useful material exit.
                    const Bitboard minorDanger = ctx.attacksByType[opp][KNIGHT]
                        | ctx.attacksByType[opp][BISHOP] | king_attacks(eksq);
                    const Bitboard safe = ctx.pieceAttacks[s] & ~own & ~enemyPawnAtt & ~minorDanger;
                    const Bitboard tradeUp = ctx.pieceAttacks[s]
                        & (pos.pieces(opp, ROOK) | pos.pieces(opp, QUEEN));
                    escapes = std::min(squares, popcount(safe | tradeUp));
                }
                if (escapes == 0)
                    value -= 8 * per;
                else if (escapes == 1)
                    value -= 4 * per;
            }
        }
        t.mg[T_MOBILITY] += value;
        t.eg[T_MOBILITY] += value;
    }

    // ---------------------------------------------------- pawn structure
    int doubled = 0, isolated = 0, backward = 0, connected = 0;
    int passedMg = 0, passedEg = 0, candidate = 0, doubledVsKing = 0;

    for (int f = 0; f < 8; ++f) {
        const Bitboard onFile = ownPawns & file_bb(f);
        const int      n      = popcount(onFile);
        if (n == 0)
            continue;
        if (n > 1) {
            doubled += (n - 1) * TW.pawnsDoubled;
            if (std::abs(f - file_of(ksq)) <= 1)
                doubledVsKing += (n - 1) * TW.pawnsDoubledVsKing;
        }
        const Bitboard adjacent = (f > 0 ? ownPawns & file_bb(f - 1) : 0)
                                | (f < 7 ? ownPawns & file_bb(f + 1) : 0);
        if (!adjacent)
            isolated += n * TW.pawnsIsolated;
    }

    for (Bitboard b = ownPawns; b;) {
        const Square s = pop_lsb(b);
        const int    f = file_of(s);
        const int    r = rank_of(s);

        Bitboard neighbours = 0;
        if (f > 0)
            neighbours |= file_bb(f - 1);
        if (f < 7)
            neighbours |= file_bb(f + 1);

        const bool supported = (pawn_attacks(opp, s) & ownPawns) != 0;
        if (supported || (ownPawns & rank_bb(s) & neighbours))
            connected += TW.pawnsConnected;

        if (!supported && r > 0 && r < 7) {
            const Square front = (col == WHITE) ? Square(int(s) + 8) : Square(int(s) - 8);
            if (enemyPawnAtt & square_bb(front))
                backward += TW.pawnsBackward;
        }

        if (!(PassedMask[col][s] & enemyPawns) && !(FrontSpan[col][s] & ownPawns)) {
            const int rel = (col == WHITE) ? r : 7 - r;
            // Arasan's tuned table (ref/arasan-v23.4.0/params.cpp, MIT) is not a
            // straight line: almost nothing until the fifth rank, then a steep
            // rise (their seventh rank 128 against 43 on the sixth, in a scale
            // where a pawn is 128). The shapes below are that table normalised
            // to the seventh rank, so the weights now mean "bonus for a passed
            // pawn one step from promotion" instead of a flat per rank value.
            static constexpr int PASSED_MG_SHAPE[8] = {0, 0, 0, 0, 0, 33, 100, 174};
            static constexpr int PASSED_EG_SHAPE[8] = {0, 0, 7, 6, 23, 55, 100, 172};
            const Square advance = Square(int(s) + (col == WHITE ? 8 : -8));
            const Bitboard step = square_bb(advance);
            const Bitboard path = FrontSpan[col][s];
            int factor = 100;
            if (occ & step)
                factor = (own & step) ? 75 : 50;
            else if ((ctx.defenseMap[opp] & step) && !(ctx.defenseMap[col] & step))
                factor = 75;
            else if (!(path & occ) && !(path & ctx.defenseMap[opp]))
                factor = 125;
            passedMg += TW.pawnsPassedMg * PASSED_MG_SHAPE[rel] * factor / 10000;
            passedEg += TW.pawnsPassedEg * PASSED_EG_SHAPE[rel] * factor / 10000;
        } else if (!(PassedMask[col][s] & enemyPawns & ~file_bb(f))
                   && (ownPawnAtt & PassedMask[col][s])) {
            candidate += TW.pawnsCandidate;  // no enemy pawn on the adjacent files ahead
        }
    }

    t.mg[T_PAWNS] += doubled + isolated + backward + doubledVsKing + connected + passedMg + candidate;
    t.eg[T_PAWNS] += (doubled + isolated + backward + doubledVsKing) / 2 + connected + passedEg
                   + candidate / 2;

    // -------------------------------------------------------------- rooks
    const Bitboard ownRooks = pos.pieces(col, ROOK);
    for (Bitboard b = ownRooks; b;) {
        const Square   s    = pop_lsb(b);
        const Bitboard file = file_bb(s);
        const bool     open = !(pos.pieces(PAWN) & file);
        if (open)
            t.mg[T_ROOKS] += TW.rooksOpenFile;
        else if (!(ownPawns & file))
            t.mg[T_ROOKS] += TW.rooksSemiOpenFile;

        if (open && file_distance(s, eksq) <= 1)
            t.mg[T_ROOKS] += TW.rooksOpenFileVsKing;

        if (rank_of(s) == (col == WHITE ? 6 : 1)) {
            t.mg[T_ROOKS] += TW.rooksOnSeventh;
            t.eg[T_ROOKS] += TW.rooksOnSeventh;
        }
    }
    if (popcount(ownRooks) >= 2) {
        const Square r1 = lsb_square(ownRooks);
        const Square r2 = msb_square(ownRooks);
        if (rook_attacks(r1, occ) & square_bb(r2))
            t.mg[T_ROOKS] += TW.rooksConnected;
    }
    {
        const Square   cornerA = (col == WHITE) ? SQ_A1 : SQ_A8;
        const Square   cornerH = (col == WHITE) ? SQ_H1 : SQ_H8;
        const Square   kB      = (col == WHITE) ? SQ_B1 : SQ_B8;
        const Square   kG      = (col == WHITE) ? SQ_G1 : SQ_G8;
        const Bitboard king    = pos.pieces(col, KING);
        if ((ownRooks & square_bb(cornerA)) && (king & square_bb(kB)))
            t.mg[T_ROOKS] += TW.rooksTrapped;
        if ((ownRooks & square_bb(cornerH)) && (king & square_bb(kG)))
            t.mg[T_ROOKS] += TW.rooksTrapped;
    }

    // ------------------------------------------------------------- pieces
    if (popcount(pos.pieces(col, BISHOP)) >= 2) {
        t.mg[T_PIECES] += TW.piecesBishopPair;
        t.eg[T_PIECES] += TW.piecesBishopPair / 2;
    }
    for (Bitboard b = pos.pieces(col, KNIGHT); b;) {
        const Square s   = pop_lsb(b);
        const int    rel = (col == WHITE) ? rank_of(s) : 7 - rank_of(s);
        if (rel >= 3 && rel <= 5 && (ownPawnAtt & square_bb(s)) && !(enemyPawnAtt & square_bb(s)))
            t.mg[T_PIECES] += TW.piecesKnightOutpost;
    }
    for (Bitboard b = pos.pieces(col, BISHOP); b;) {
        const Square s  = pop_lsb(b);
        const Square fb = (col == WHITE) ? SQ_B2 : SQ_B7;
        const Square fg = (col == WHITE) ? SQ_G2 : SQ_G7;
        if (s == fb || s == fg)
            t.mg[T_PIECES] += TW.piecesFianchetto;

        const Bitboard sameColour = (square_bb(s) & DarkSquares) ? DarkSquares : LightSquares;
        const int      bad        = popcount(ownPawns & sameColour);
        t.mg[T_PIECES] += bad * TW.piecesBadBishop;
        t.eg[T_PIECES] += bad * TW.piecesBadBishop / 2;
    }

    // ------------------------------------------------------------ threats
    {
        for (int pt = KNIGHT; pt <= QUEEN; ++pt) {
            for (Bitboard b = pos.pieces(opp, PieceType(pt)); b;) {
                const Square   s   = pop_lsb(b);
                const Bitboard bit = square_bb(s);

                bool attackedByLower = false;
                if (ownPawnAtt & bit)
                    attackedByLower = true;
                else if (pt > KNIGHT && (ctx.attacksByType[col][KNIGHT] & bit))
                    attackedByLower = true;
                else if (pt > BISHOP && (ctx.attacksByType[col][BISHOP] & bit))
                    attackedByLower = true;
                else if (pt > ROOK && (ctx.attacksByType[col][ROOK] & bit))
                    attackedByLower = true;
                if (attackedByLower)
                    t.mg[T_THREATS] += TW.threatsAttackedByLower;

                // hanging: attacked by us and not defended by them
                if ((ctx.defenseMap[col] & bit) && !(ctx.defenseMap[opp] & bit))
                    t.mg[T_THREATS] += TW.threatsHanging;

                // pinned against the enemy king
                for (Bitboard sl = pos.pieces(col, ROOK) | pos.pieces(col, BISHOP)
                                   | pos.pieces(col, QUEEN);
                     sl;) {
                    const Square slider = pop_lsb(sl);
                    const PieceType sliderType = type_of(pos.pieceOn(slider));
                    const bool aligned = ((sliderType != BISHOP ? rook_attacks(slider, 0) : 0)
                                        | (sliderType != ROOK ? bishop_attacks(slider, 0) : 0))
                                       & square_bb(eksq);
                    if (aligned && (BetweenBB[slider][eksq] & occ) == bit) {
                        t.mg[T_THREATS] += TW.threatsPinned;
                        break;
                    }
                }
            }
        }

        // discovered check potential: own piece between a slider and the enemy king
        bool discovered = false;
        for (Bitboard sl = pos.pieces(col, ROOK) | pos.pieces(col, BISHOP)
                           | pos.pieces(col, QUEEN);
             sl && !discovered;) {
            const Square slider = pop_lsb(sl);
            const PieceType sliderType = type_of(pos.pieceOn(slider));
            const Bitboard rays = (sliderType != BISHOP ? rook_attacks(slider, 0) : 0)
                                | (sliderType != ROOK ? bishop_attacks(slider, 0) : 0);
            const Bitboard blockers = BetweenBB[slider][eksq] & occ;
            if ((rays & square_bb(eksq)) && popcount(blockers) == 1 && (blockers & own))
                discovered = true;
        }
        if (discovered)
            t.mg[T_THREATS] += TW.threatsDiscovered;
    }

    // -------------------------------------------------------------- space
    {
        const Bitboard centre  = FileCBB | FileDBB | FileEBB | FileFBB;
        const Bitboard ownHalf = (col == WHITE) ? (Rank2BB | Rank3BB | Rank4BB | Rank5BB)
                                               : (Rank4BB | Rank5BB | Rank6BB | Rank7BB);
        const int safe = popcount(centre & ownHalf & ~ownPawns & ~enemyPawnAtt & ~pos.pieces(opp));
        t.mg[T_SPACE] += safe * TW.spacePerSafeSquare;
    }

    // ---------------------------------------------------- initiative
    {
        int progress = 0;
        for (int pt = KNIGHT; pt <= QUEEN; ++pt)
            for (Bitboard b = pos.pieces(col, PieceType(pt)); b;) {
                const Square s = pop_lsb(b);
                progress += (7 - distance(s, eksq)) * (pt == QUEEN ? 4 : pt == ROOK ? 3 : 2);
            }
        t.mg[T_INITIATIVE] += progress * TW.initiativeProgress / 10;
        t.eg[T_INITIATIVE] += progress * TW.initiativeProgress / 20;
    }

    // ------------------------------------------------------- king safety
    {
        t.mg[T_KINGSAFETY] -= dangerValue(units[opp]);  // the enemy attacks our king

        const int kf = file_of(ksq);
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
            int shieldQuality = 0;
            for (int dr = 1; dr <= 2 && !shieldQuality; ++dr) {
                const int r = (col == WHITE) ? rank_of(ksq) + dr : rank_of(ksq) - dr;
                if (r < 0 || r > 7)
                    continue;
                if (pos.pieceOn(make_square(f, r)) == make_piece(col, PAWN))
                    shieldQuality = dr == 1 ? 100 : 35;
            }
            // A pawn two ranks ahead no longer counts as an intact home shield.
            t.mg[T_KINGSAFETY] += TW.shieldMissing
                + (TW.shieldPawn - TW.shieldMissing) * shieldQuality / 100;

            // The nearest enemy pawn in front of the king can open the
            // shelter before a piece attack reaches the adjacent king ring.
            const int kingRank = rank_of(ksq);
            const Bitboard frontRanks = col == WHITE
                ? (kingRank == 7 ? 0 : (~0ULL << ((kingRank + 1) * 8)))
                : ((1ULL << (kingRank * 8)) - 1);
            const Bitboard stormPawns = pos.pieces(opp, PAWN) & file_bb(f) & frontRanks;
            const Square nearestPawn = stormPawns
                ? (col == WHITE ? lsb_square(stormPawns) : msb_square(stormPawns)) : SQ_NONE;
            const int nearest = stormPawns ? rank_distance(ksq, nearestPawn) : 8;
            if (nearest <= 4) {
                const int relativeRank = col == WHITE ? rank_of(ksq) + nearest
                                                      : rank_of(ksq) - nearest;
                const Square pawn = make_square(f, relativeRank);
                const Square front = Square(int(pawn) + (opp == WHITE ? 8 : -8));
                const bool blocked = is_ok(front) && pos.pieceOn(front) == make_piece(col, PAWN);
                const int proximity = 5 - nearest;
                t.mg[T_KINGSAFETY] += TW.pawnStorm * proximity * proximity / (blocked ? 8 : 4);
            }

            const Bitboard filePawns = pos.pieces(PAWN) & file_bb(f);
            if (filePawns == 0)
                t.mg[T_KINGSAFETY] += TW.openFileNearKing;
            else if (!(ownPawns & file_bb(f)))
                t.mg[T_KINGSAFETY] += TW.semiOpenNearKing;
        }

        // Weak squares beside the king matter most when the enemy queen and
        // another piece can exploit them. Pawn protection cannot be replaced
        // by a nominally defended rook or queen.
        if (pos.pieces(opp, QUEEN)) {
            const Bitboard holes = kingDangerZone(col, ksq) & ~ownPawnAtt & ~own;
            const Bitboard pressured = holes & ctx.attackMap[opp];
            t.mg[T_KINGSAFETY] += popcount(pressured) * TW.shieldMissing / 2;
            t.mg[T_KINGSAFETY] += popcount(pressured & ctx.attackedTwice[opp]) * TW.shieldMissing / 2;
        }

        const int  myRights = (col == WHITE) ? (CR_WK | CR_WQ) : (CR_BK | CR_BQ);
        const bool homeKing = (ksq == (col == WHITE ? SQ_E1 : SQ_E8));
        if (homeKing && (pos.castlingRights() & myRights))
            t.mg[T_KINGSAFETY] += TW.uncastledMg;

        // A queen plus a supporting piece still threatens mate after other
        // material has been exchanged. Do not taper all shelter danger away.
        if (pos.pieces(opp, QUEEN)
            && (pos.pieces(opp, KNIGHT) | pos.pieces(opp, BISHOP) | pos.pieces(opp, ROOK)))
            t.eg[T_KINGSAFETY] += t.mg[T_KINGSAFETY] / 3;
    }

    // ------------------------------------------------------------ endgame
    {
        int proximity = 0;
        for (Bitboard b = ctx.passed[col]; b;) {
            const Square s = pop_lsb(b);
            proximity += (7 - distance(ksq, s)) * 4 - (7 - distance(eksq, s)) * 3;
        }
        t.eg[T_ENDGAME] += proximity * TW.endgamePassedKingProximity / 100;
    }

    // ---------------------------------------------------------- aggression
    {
        // The defensive side of an attack is already in KingSafety, so the
        // offensive bonus is deliberately the smaller part (aggression / 400).
        const Score attack = dangerValue(units[col]) * TW.aggressionKingAttack / 400;

        int       storm = 0;
        const int ekf   = file_of(eksq);
        for (Bitboard b = ownPawns; b;) {
            const Square s = pop_lsb(b);
            if (std::abs(file_of(s) - ekf) <= 1) {
                const int rel = (col == WHITE) ? rank_of(s) : 7 - rank_of(s);
                if (rel >= 2 && (file_of(eksq) <= 2 || file_of(eksq) >= 5)) {
                    // Advance counts less when blocked or when it tears down
                    // our own shelter on the same wing.
                    const Square front = Square(int(s) + (col == WHITE ? 8 : -8));
                    int factor = (is_ok(front) && (occ & square_bb(front))) ? 25 : 100;
                    if (file_distance(ksq, eksq) <= 2)
                        factor /= 4;
                    if (std::abs(file_of(s) - file_of(ksq)) <= 1)
                        factor /= 2;
                    storm += TW.aggressionPawnStorm * (rel - 1) * factor / 800;
                }
            }
        }

        t.mg[T_AGGRESSION] += attack + storm;

        // Keeping queens on the board is worth more the stronger our attack is.
        if (pos.pieces(QUEEN) && TW.aggressionAvoidQueenTrade != 100)
            t.mg[T_AGGRESSION] += attack * (TW.aggressionAvoidQueenTrade - 100) / 100;
    }

    // ---------------------------------------------------------- complexity
    // Sharpness from the point of view of the side that has the initiative.
    t.mg[T_COMPLEXITY] += (units[col] - units[opp]) * TW.complexityWeight / 400;
}

}  // namespace

namespace {

// Dead draw: no pawns, rooks or queens and at most one minor piece per side.
bool insufficientMaterial(const Position& pos) {
    if (pos.pieces(PAWN) | pos.pieces(ROOK) | pos.pieces(QUEEN))
        return false;
    const int minorsWhite = popcount(pos.pieces(WHITE, KNIGHT) | pos.pieces(WHITE, BISHOP));
    const int minorsBlack = popcount(pos.pieces(BLACK, KNIGHT) | pos.pieces(BLACK, BISHOP));
    if (minorsWhite + minorsBlack <= 1)
        return true;
    // Bishops only on a single colour cannot give mate. B v N and N v N
    // are not dead positions: opposing pieces can obstruct the king.
    if (!pos.pieces(KNIGHT)) {
        const Bitboard bishops = pos.pieces(BISHOP);
        return !(bishops & DarkSquares) || !(bishops & LightSquares);
    }
    return false;
}

// Both sides have exactly one bishop, on opposite colours.
bool oppositeBishopsOnly(const Position& pos) {
    if (pos.pieces(KNIGHT) | pos.pieces(ROOK) | pos.pieces(QUEEN))
        return false;
    if (!pos.pieces(PAWN))
        return false;  // scaling is for pawn endings with opposite bishops
    if (popcount(pos.pieces(WHITE, BISHOP)) != 1 || popcount(pos.pieces(BLACK, BISHOP)) != 1)
        return false;
    const bool whiteDark = (pos.pieces(WHITE, BISHOP) & DarkSquares) != 0;
    const bool blackDark = (pos.pieces(BLACK, BISHOP) & DarkSquares) != 0;
    return whiteDark != blackDark;
}

// Material-specific winning chances, not a declaration of a dead draw.
// Only reduce the score of the side the evaluation currently favours: a
// hanging rook must still be refuted by search, and mates bypass static eval.
int materialWinningScale(const Position& pos, Score score) {
    const Color strong = score >= 0 ? pos.sideToMove() : ~pos.sideToMove();
    const Color weak = ~strong;
    const int sr = popcount(pos.pieces(strong, ROOK));
    const int wr = popcount(pos.pieces(weak, ROOK));
    const int sb = popcount(pos.pieces(strong, BISHOP));
    const int sn = popcount(pos.pieces(strong, KNIGHT));
    const int wm = popcount(pos.pieces(weak, BISHOP) | pos.pieces(weak, KNIGHT));
    if (!pos.pieces(PAWN) && !pos.pieces(QUEEN)) {
        if (sr == 1 && wr == 1 && sb + sn == 1 && wm == 0)
            return TW.drawishRookMinorVsRook;
        if (sr == 1 && wr == 0 && sb + sn == 0 && wm == 1)
            return TW.drawishRookVsMinor;
        if (sr == 0 && wr == 0 && sb == 0 && sn == 2 && wm == 0)
            return TW.drawishTwoKnights;
    }
    // Wrong-colour bishop and rook pawns versus a bare king already on the
    // promotion corner. Other pawn files or extra pieces invalidate the rule.
    if (!pos.pieces(QUEEN) && sr == 0 && wr == 0 && sn == 0 && sb == 1
        && wm == 0 && !pos.pieces(weak, PAWN) && pos.pieces(strong, PAWN)) {
        const Bitboard pawns = pos.pieces(strong, PAWN);
        for (int file : {0, 7}) {
            if (pawns & ~file_bb(file)) continue;
            const Square corner = make_square(file, strong == WHITE ? 7 : 0);
            const bool bishopDark = (pos.pieces(strong, BISHOP) & DarkSquares) != 0;
            const bool cornerDark = (square_bb(corner) & DarkSquares) != 0;
            if (bishopDark != cornerDark && pos.kingSquare(weak) == corner)
                return 0;
        }
    }
    return 100;
}

}  // namespace

static_assert(TERM_NB == EVAL_TERM_NB, "term count mismatch");

const char* const EVAL_TERM_NAMES[EVAL_TERM_NB] = {
    "material", "pst", "mobility", "kingSafety", "pawns", "rooks", "pieces",
    "threats", "space", "initiative", "endgame", "aggression", "complexity",
};

EvalBreakdown evaluateDetailed(const Position& pos) {
    EvalBreakdown e;

    const EvalContext ctx = buildContext(pos);

    int units[COLOR_NB];
    for (int c = 0; c < COLOR_NB; ++c)
        units[c] = attackUnitsOf(pos, ctx, Color(c));

    SideTerms side[COLOR_NB];
    for (int c = 0; c < COLOR_NB; ++c)
        evalSide(pos, ctx, Color(c), units, side[c]);

    // Sacrifice tolerance: the attack counts for more when we are behind in
    // material (a bold bot keeps attacking, a timid one stops).
    for (int c = 0; c < COLOR_NB; ++c) {
        const Score balance = side[c].mg[T_MATERIAL] - side[c ^ 1].mg[T_MATERIAL];
        const int   deficit = std::max(0, -balance);
        const int   factor  = std::clamp(100 + (TW.aggressionSacrifice - 100)
                                            * std::min(deficit, 400) / 400, 0, 150);
        side[c].mg[T_AGGRESSION] = Score(side[c].mg[T_AGGRESSION] * factor / 100);
    }

    const int phase = ctx.phase;
    Score     total = 0;
    Score     mgTotal = 0;
    Score     egTotal = 0;

    for (int t = 0; t < TERM_NB; ++t) {
        const Score mgScore = side[WHITE].mg[t] - side[BLACK].mg[t];
        const Score egScore = side[WHITE].eg[t] - side[BLACK].eg[t];
        mgTotal += mgScore;
        egTotal += egScore;

        const Score value = scaleTerm(tapered(mgScore, egScore, phase), groupOf(Term(t)));
        e.term[t] = value;
        total += value;
    }

    // ------------------------------------------------------ side to move
    if (pos.sideToMove() == BLACK) {
        total   = -total;
        mgTotal = -mgTotal;
        egTotal = -egTotal;
        for (int t = 0; t < TERM_NB; ++t)
            e.term[t] = -e.term[t];  // the breakdown is reported from the mover's view
    }
    total += EW.tempo;

    // -------------------------------------------------- drawish scaling
    // Applied after the tempo bonus so that a dead draw really evaluates to 0.
    int scale = 100;
    // The specific rule wins over the generic one: with exactly one bishop per
    // side on opposite colours the position is only drawish, not dead, so it
    // is scaled by Drawish.OppositeBishops instead of being flattened to zero.
    // (The generic test below also covers this material, so the order matters.)
    if (oppositeBishopsOnly(pos))
        scale = std::min(scale, TW.drawishOppositeBishops);
    else if (insufficientMaterial(pos))
        scale = TW.drawishInsufficient;
    scale = std::min(scale, materialWinningScale(pos, total));
    if (scale != 100)
        total = Score(total * std::max(0, scale) / 100);

    e.phase    = phase;
    e.mgScore  = mgTotal;
    e.egScore  = egTotal;
    e.tempo    = EW.tempo;
    e.total    = total;
    e.material = tapered(side[WHITE].mg[T_MATERIAL] - side[BLACK].mg[T_MATERIAL],
                         side[WHITE].eg[T_MATERIAL] - side[BLACK].eg[T_MATERIAL], phase);
    e.pst      = tapered(side[WHITE].mg[T_PST] - side[BLACK].mg[T_PST],
                         side[WHITE].eg[T_PST] - side[BLACK].eg[T_PST], phase);
    if (pos.sideToMove() == BLACK) {
        e.material = -e.material;
        e.pst      = -e.pst;
    }
    return e;
}

Score evaluate(const Position& pos) { return evaluateDetailed(pos).total; }

// ---------------------------------------------------------------------------
// Lazy evaluation
// ---------------------------------------------------------------------------
Score evaluateMaterial(const Position& pos) {
    Score mg[COLOR_NB] = {0, 0};
    Score eg[COLOR_NB] = {0, 0};
    int   phase        = 0;

    for (int c = 0; c < COLOR_NB; ++c) {
        const Color col = Color(c);
        for (int pt = PAWN; pt < KING; ++pt) {
            const int n = popcount(pos.pieces(col, PieceType(pt)));
            if (n == 0)
                continue;
            mg[c] += n * EW.matMg[pt];
            eg[c] += n * EW.matEg[pt];
            phase += n * (pt == KNIGHT || pt == BISHOP ? 1 : pt == ROOK ? 2 : 4);
        }
        for (int pt = PAWN; pt < KING; ++pt)
            for (Bitboard b = pos.pieces(col, PieceType(pt)); b;) {
                const Square s   = pop_lsb(b);
                const Square rel = (col == WHITE) ? s : flip_rank(s);
                mg[c] += PST_MG[pt][rel];
                eg[c] += PST_EG[pt][rel];
            }
        const Square krel = (col == WHITE) ? pos.kingSquare(col) : flip_rank(pos.kingSquare(col));
        mg[c] += PST_MG[KING][krel];
        eg[c] += PST_EG[KING][krel] * TW.endgameKingActivity / 100;
    }

    phase = std::min(phase, 24);
    Score value = tapered(mg[WHITE] - mg[BLACK], eg[WHITE] - eg[BLACK], phase);
    value = scaleTerm(value, G_MATERIAL);
    if (pos.sideToMove() == BLACK)
        value = -value;
    return value + EW.tempo;
}

Score evaluateLazy(const Position& pos, Score alpha, Score beta) {
    // The positional terms rarely exceed this margin, so when the cheap score
    // is already outside the window the expensive part cannot change it.
    constexpr Score LAZY_MARGIN = 400;

    if (!pos.pieces(QUEEN) || popcount(pos.pieces(QUEEN)) == 1)
        return evaluate(pos);
    const Score material = evaluateMaterial(pos);
    if (material - LAZY_MARGIN >= beta || material + LAZY_MARGIN <= alpha)
        return material;
    return evaluate(pos);
}


// Test usable defences separately from geometric attacks: a pinned pawn still
// excludes a king destination under the laws of chess, but cannot recapture a
// queen/bishop/rook that invades on its nominally defended square.
bool evaluationRegressionTests(std::ostream& out) {
    bool passed = true;
    const auto check = [&](bool ok, const char* text) {
        out << (ok ? "ok    " : "FAIL  ") << text << '\n';
        passed &= ok;
    };
    Position p;
    p.setFen("6k1/8/6q1/8/8/7b/6P1/6K1 w - - 0 1");
    EvalContext ctx = buildContext(p);
    check(!(ctx.pawnAtt[WHITE] & square_bb(SQ_H3))
              && !(ctx.defenseMap[WHITE] & square_bb(SQ_H3))
              && find_legal_move(p, "g2h3") == 0,
          "a pinned g2 pawn does not provide usable defence of h3");
    check((ctx.attackMap[WHITE] & square_bb(SQ_H3)) && p.isSquareAttacked(SQ_H3, WHITE),
          "pinned pawn retains geometric king exclusion attacks");
    p.setFen("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1");
    ctx = buildContext(p);
    check((ctx.pieceAttacks[SQ_E2] & square_bb(SQ_E3))
              && !(ctx.pieceAttacks[SQ_E2] & square_bb(SQ_F2)),
          "pinned rook mobility stays on the pin line");
    p.setFen("4r1k1/8/8/8/8/8/4N3/4K3 w - - 0 1");
    ctx = buildContext(p);
    check(ctx.pieceAttacks[SQ_E2] == 0, "absolutely pinned knight has no usable mobility");
    p.setFen("7k/8/8/8/4b3/3P4/2K5/8 w - - 0 1");
    ctx = buildContext(p);
    check((ctx.pawnAtt[WHITE] & square_bb(SQ_E4))
              && !(ctx.pawnAtt[WHITE] & square_bb(SQ_C4))
              && find_legal_move(p, "d3e4") != 0,
          "a diagonally pinned pawn may capture its pinner along the pin line");
    const auto terms = [](const char* fen, Color color = WHITE) {
        Position board;
        board.setFen(fen);
        const EvalContext context = buildContext(board);
        const int units[COLOR_NB] = {0, 0};
        SideTerms result;
        evalSide(board, context, color, units, result);
        return result;
    };
    const SideTerms active = terms("7k/8/8/8/8/8/1P1P2B1/K7 w - - 0 1");
    const SideTerms trapped = terms("7k/8/8/8/8/8/1P1P2B1/K1B5 w - - 0 1");
    check(trapped.mg[T_MOBILITY] <= active.mg[T_MOBILITY] - 8 * TW.mobility[BISHOP - KNIGHT],
          "an active bishop does not hide a second bishop trapped behind its pawns");
    const SideTerms bishopTrap = terms("6k1/5p1p/6b1/5P2/8/6N1/8/6K1 b - - 0 1", BLACK);
    const SideTerms bishopExit = terms("6k1/5p1p/6b1/5P2/8/N7/8/6K1 b - - 0 1", BLACK);
    check(bishopTrap.mg[T_MOBILITY] < bishopExit.mg[T_MOBILITY],
          "a pawn-attacked bishop recognises exits covered by an enemy knight");
    const SideTerms blocked = terms("8/3n4/3P4/8/8/8/8/K6k w - - 0 1");
    const SideTerms free = terms("n7/8/3P4/8/8/8/8/K6k w - - 0 1");
    check(free.mg[T_PAWNS] > blocked.mg[T_PAWNS]
              && free.eg[T_PAWNS] > blocked.eg[T_PAWNS],
          "a free promotion path is worth more than a blocked passed pawn");
    p.setFen("7k/8/3P4/8/3P4/8/8/K7 w - - 0 1");
    ctx = buildContext(p);
    check(ctx.passed[WHITE] == square_bb(SQ_D6),
          "a rear doubled pawn is not counted as a second passer");
    p.setFen("8/8/2k5/2b5/K7/3r4/R7/8 b - - 57 87");
    check(evaluate(p) > 0 && evaluate(p) < 120,
          "RB versus R from calibration has modest winning chances, not a five-pawn bonus");
    p.setFen("7k/8/5K2/8/8/8/8/1NB5 w - - 0 1");
    check(evaluate(p) > 500, "bishop and knight versus bare king retains winning material");
    p.setFen("7k/8/5K2/8/8/8/8/1R6 w - - 0 1");
    check(evaluate(p) > 400, "rook versus bare king is not scaled as drawish");
    p.setFen("k7/P7/2K5/8/8/8/8/2B5 w - - 0 1");
    check(evaluate(p) == 0, "wrong bishop rook pawn fortress on the promotion corner is recognised");
    check(evaluateLazy(p, 1000, 1001) == 0,
          "lazy material cannot bypass a recognised fortress");
    p.setFen("k7/P7/2K5/8/8/8/8/1B6 w - - 0 1");
    check(evaluate(p) > 300, "correct-colour bishop is not mistaken for a rook-pawn fortress");
    const SideTerms nearStorm = terms("3q2k1/5p2/8/8/6p1/8/5PPP/3Q2K1 w - - 0 1");
    const SideTerms distantStorm = terms("3q2k1/5pp1/8/8/8/8/5PPP/3Q2K1 w - - 0 1");
    check(nearStorm.mg[T_KINGSAFETY] < distantStorm.mg[T_KINGSAFETY],
          "an approaching enemy pawn storm increases king danger before contact");
    const SideTerms queenAttack = terms("6k1/8/8/8/8/3q4/8/3r2K1 w - - 0 1");
    const SideTerms noQueen = terms("6k1/8/8/8/8/8/8/3r2K1 w - - 0 1");
    check(queenAttack.eg[T_KINGSAFETY] < noQueen.eg[T_KINGSAFETY],
          "queen and rook attack retains king danger into the endgame phase");
    return passed;
}
