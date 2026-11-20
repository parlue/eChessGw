// Virgin - UCI chess engine
// Move generation (pseudo legal + legality filter) and perft.
#include "movegen.h"
#include "weights.h"

#include <algorithm>

#include <ostream>

namespace {

inline void addPromotions(MoveList& list, Square from, Square to) {
    list.add(make_move(from, to, MF_PROMOTION, QUEEN));
    list.add(make_move(from, to, MF_PROMOTION, ROOK));
    list.add(make_move(from, to, MF_PROMOTION, BISHOP));
    list.add(make_move(from, to, MF_PROMOTION, KNIGHT));
}

inline void addPawnMove(MoveList& list, Square from, Square to) {
    if (rank_of(to) == 7 || rank_of(to) == 0)
        addPromotions(list, from, to);
    else
        list.add(make_move(from, to));
}

void addPawnMoves(MoveList& list, Bitboard targets, int delta) {
    for (Bitboard b = targets; b;) {
        const Square to = pop_lsb(b);
        addPawnMove(list, Square(int(to) - delta), to);
    }
}

}  // namespace

void generatePseudoLegal(const Position& pos, MoveList& list, bool tacticalOnly) {
    const Color    us    = pos.sideToMove();
    const Color    them  = ~us;
    const Bitboard own   = pos.pieces(us);
    const Bitboard enemy = pos.pieces(them);
    const Bitboard occ   = pos.occupied();
    const Bitboard targets = tacticalOnly ? enemy : ~own;

    // ------------------------------------------------------------- pawns
    const Bitboard pawns = pos.pieces(us, PAWN);
    if (us == WHITE) {
        const Bitboard single = shift_north(pawns) & ~occ;
        const Bitboard dbl    = shift_north(single & Rank3BB) & ~occ;
        addPawnMoves(list, tacticalOnly ? single & Rank8BB : single, 8);
        for (Bitboard b = tacticalOnly ? 0 : dbl; b;) {
            const Square to = pop_lsb(b);
            list.add(make_move(Square(int(to) - 16), to));
        }
        addPawnMoves(list, shift_nw(pawns) & enemy, 7);
        addPawnMoves(list, shift_ne(pawns) & enemy, 9);
    } else {
        const Bitboard single = shift_south(pawns) & ~occ;
        const Bitboard dbl    = shift_south(single & Rank6BB) & ~occ;
        addPawnMoves(list, tacticalOnly ? single & Rank1BB : single, -8);
        for (Bitboard b = tacticalOnly ? 0 : dbl; b;) {
            const Square to = pop_lsb(b);
            list.add(make_move(Square(int(to) + 16), to));
        }
        addPawnMoves(list, shift_se(pawns) & enemy, -7);
        addPawnMoves(list, shift_sw(pawns) & enemy, -9);
    }

    if (pos.epSquare() != SQ_NONE) {
        const Bitboard capturers = pawn_attacks(them, pos.epSquare()) & pawns;
        for (Bitboard b = capturers; b;)
            list.add(make_move(pop_lsb(b), pos.epSquare(), MF_ENPASSANT));
    }

    // ------------------------------------------------------ knights, king
    for (Bitboard b = pos.pieces(us, KNIGHT); b;) {
        const Square from = pop_lsb(b);
        for (Bitboard t = knight_attacks(from) & targets; t;)
            list.add(make_move(from, pop_lsb(t)));
    }

    const Square ksq = pos.kingSquare(us);
    for (Bitboard t = king_attacks(ksq) & targets; t;)
        list.add(make_move(ksq, pop_lsb(t)));

    // ------------------------------------------------------------ sliders
    for (Bitboard b = pos.pieces(us, BISHOP); b;) {
        const Square from = pop_lsb(b);
        for (Bitboard t = bishop_attacks(from, occ) & targets; t;)
            list.add(make_move(from, pop_lsb(t)));
    }
    for (Bitboard b = pos.pieces(us, ROOK); b;) {
        const Square from = pop_lsb(b);
        for (Bitboard t = rook_attacks(from, occ) & targets; t;)
            list.add(make_move(from, pop_lsb(t)));
    }
    for (Bitboard b = pos.pieces(us, QUEEN); b;) {
        const Square from = pop_lsb(b);
        for (Bitboard t = queen_attacks(from, occ) & targets; t;)
            list.add(make_move(from, pop_lsb(t)));
    }

    if (tacticalOnly)
        return;

    // ----------------------------------------------------------- castling
    const int cr = pos.castlingRights();
    if (us == WHITE) {
        if ((cr & CR_WK) && !(occ & (square_bb(SQ_F1) | square_bb(SQ_G1)))
            && !pos.isSquareAttacked(SQ_E1, them) && !pos.isSquareAttacked(SQ_F1, them)
            && !pos.isSquareAttacked(SQ_G1, them))
            list.add(make_move(SQ_E1, SQ_G1, MF_CASTLE));
        if ((cr & CR_WQ) && !(occ & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1)))
            && !pos.isSquareAttacked(SQ_E1, them) && !pos.isSquareAttacked(SQ_D1, them)
            && !pos.isSquareAttacked(SQ_C1, them))
            list.add(make_move(SQ_E1, SQ_C1, MF_CASTLE));
    } else {
        if ((cr & CR_BK) && !(occ & (square_bb(SQ_F8) | square_bb(SQ_G8)))
            && !pos.isSquareAttacked(SQ_E8, them) && !pos.isSquareAttacked(SQ_F8, them)
            && !pos.isSquareAttacked(SQ_G8, them))
            list.add(make_move(SQ_E8, SQ_G8, MF_CASTLE));
        if ((cr & CR_BQ) && !(occ & (square_bb(SQ_B8) | square_bb(SQ_C8) | square_bb(SQ_D8)))
            && !pos.isSquareAttacked(SQ_E8, them) && !pos.isSquareAttacked(SQ_D8, them)
            && !pos.isSquareAttacked(SQ_C8, them))
            list.add(make_move(SQ_E8, SQ_C8, MF_CASTLE));
    }
}

int generateLegal(const Position& pos, MoveList& list) {
    MoveList pseudo;
    generatePseudoLegal(pos, pseudo);

    int n = 0;
    for (Move m : pseudo)
        if (pos.isLegal(m))
            list.moves[size_t(n++)] = m;
    list.count = n;
    return n;
}

bool hasLegalMove(const Position& pos) {
    // A legal pawn push is a cheap witness; use the complete generator as a
    // fallback. In particular, pinned pawns still go through the legality test.
    const Color us = pos.sideToMove();
    const Bitboard pawns = pos.pieces(us, PAWN);
    Bitboard pushes = (us == WHITE ? shift_north(pawns) : shift_south(pawns)) & ~pos.occupied();
    while (pushes) {
        const Square to = pop_lsb(pushes);
        const Square from = Square(int(to) + (us == WHITE ? -8 : 8));
        const bool promo = rank_of(to) == 0 || rank_of(to) == 7;
        if (pos.isLegal(make_move(from, to, promo ? MF_PROMOTION : MF_NORMAL, QUEEN)))
            return true;
    }
    MoveList pseudo;
    generatePseudoLegal(pos, pseudo);
    for (Move m : pseudo)
        if (pos.isLegal(m))
            return true;
    return false;
}

Move find_legal_move(const Position& pos, const std::string& text) {
    bool       ok    = false;
    const Move parsed = parse_uci_move(text, ok);
    if (!ok)
        return 0;

    MoveList legal;
    if (generateLegal(pos, legal) == 0)
        return 0;

    const bool parsedPromo = is_promotion(parsed);
    for (Move cand : legal) {
        if (from_sq(cand) != from_sq(parsed) || to_sq(cand) != to_sq(parsed))
            continue;
        if (is_promotion(cand) != parsedPromo)
            continue;
        if (parsedPromo && promotion_type(cand) != promotion_type(parsed))
            continue;
        return cand;
    }
    return 0;
}

uint64_t perft(const Position& pos, int depth) {
    if (depth <= 0)
        return 1;

    MoveList list;
    generatePseudoLegal(pos, list);

    uint64_t nodes = 0;
    for (Move m : list) {
        if (!pos.isLegal(m))
            continue;
        if (depth == 1) {
            ++nodes;
            continue;
        }
        Position next = pos;
        next.applyMove(m);
        nodes += perft(next, depth - 1);
    }
    return nodes;
}

uint64_t perftDivide(const Position& pos, int depth, std::ostream& os) {
    if (depth <= 0)
        return 1;

    MoveList list;
    generatePseudoLegal(pos, list);

    uint64_t total = 0;
    for (Move m : list) {
        if (!pos.isLegal(m))
            continue;
        uint64_t nodes = 1;
        if (depth > 1) {
            Position next = pos;
            next.applyMove(m);
            nodes = perft(next, depth - 1);
        }
        total += nodes;
        os << move_to_uci(m) << ": " << nodes << '\n';
    }
    os << "\nTotal: " << total << '\n';
    return total;
}

// ---------------------------------------------------------------------------
// Static exchange evaluation, including en passant, promotions and dynamically
// revealed absolute pins. Geometric attackers are filtered by king safety.
// This is a same-square material model, not a tactical search.
// ---------------------------------------------------------------------------
namespace {
int exchangeValue(PieceType pt) { return pt == KING ? 20000 : EW.matMg[pt]; }
}

bool seeGE(const Position& pos, Move m, int threshold) {
    if (pos.isCastle(m))
        return threshold <= 0;
    const Square from = from_sq(m), target = to_sq(m);
    const Bitboard targetBit = square_bb(target);
    const Color us = pos.sideToMove();
    const PieceType mover = type_of(pos.pieceOn(from));
    const PieceType promoted = is_promotion(m) ? promotion_type(m) : mover;
    int gain[64];
    gain[0] = pos.isEnPassant(m) ? exchangeValue(PAWN)
              : pos.pieceOn(target) != NO_PIECE ? exchangeValue(type_of(pos.pieceOn(target))) : 0;
    if (is_promotion(m))
        gain[0] += exchangeValue(promoted) - exchangeValue(PAWN);
    if (gain[0] < threshold)
        return false;
    int victim = exchangeValue(promoted);
    // Fast bound except where an opposing recapturing pawn could promote.
    const bool promotionRank = rank_of(target) == (us == WHITE ? 0 : 7);
    if (!promotionRank && gain[0] - victim >= threshold)
        return true;

    Bitboard occupied = pos.occupied() & ~square_bb(from) & ~targetBit;
    if (pos.isEnPassant(m))
        occupied &= ~square_bb(Square(int(target) + (us == WHITE ? -8 : 8)));
    Color side = ~us;
    int depth = 0;
    while (depth < 63) {
        const Bitboard attackers = pos.attackersTo(target, occupied) & occupied & pos.pieces(side);
        if (!attackers)
            break;
        Square chosen = SQ_NONE;
        PieceType chosenType = NO_PIECE_TYPE;
        int cheapest = 30000;
        for (int pt = PAWN; pt <= KING; ++pt) {
            if (exchangeValue(PieceType(pt)) >= cheapest)
                continue;
            Bitboard candidates = attackers & pos.pieces(side, PieceType(pt));
            while (candidates) {
                const Square candidate = pop_lsb(candidates);
                const Bitboard after = (occupied & ~square_bb(candidate)) | targetBit;
                const Square king = pt == KING ? target : pos.kingSquare(side);
                // The current target occupant is captured, so its original
                // identity must not participate in this king safety test.
                const Bitboard enemies = pos.pieces(~side) & after & ~targetBit;
                if (pos.attackersTo(king, after) & enemies)
                    continue;
                chosen = candidate;
                chosenType = PieceType(pt);
                cheapest = exchangeValue(chosenType);
                break;
            }
        }
        if (chosen == SQ_NONE)
            break;
        const bool promotes = chosenType == PAWN && rank_of(target) == (side == WHITE ? 7 : 0);
        const int promotionGain = promotes ? exchangeValue(QUEEN) - exchangeValue(PAWN) : 0;
        ++depth;
        gain[depth] = victim + promotionGain - gain[depth - 1];
        victim = promotes ? exchangeValue(QUEEN) : exchangeValue(chosenType);
        occupied &= ~square_bb(chosen);
        if (chosenType == KING)
            break; // a legal king recapture cannot itself be recaptured
        side = ~side;
    }
    while (depth > 0) {
        gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        --depth;
    }
    return gain[0] >= threshold;
}
