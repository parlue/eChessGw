// Virgin - UCI chess engine
// Board representation, FEN handling, Zobrist hashing and move making.
#include "position.h"

#include <cctype>
#include <cstring>
#include <ostream>
#include <sstream>

Key ZobristPsq[PIECE_NB][SQUARE_NB];
Key ZobristSide;
Key ZobristCastling[16];
Key ZobristEpFile[8];
int CastlingRightsMask[SQUARE_NB];

namespace {

uint64_t zobristState = 0xD1B54A32D192ED03ULL;

uint64_t zrnd() {
    zobristState ^= zobristState >> 12;
    zobristState ^= zobristState << 25;
    zobristState ^= zobristState >> 27;
    return zobristState * 0x2545F4914F6CDD1DULL;
}

int pieceTypeFromChar(char c) {
    switch (std::tolower((unsigned char)c)) {
    case 'p': return PAWN;
    case 'n': return KNIGHT;
    case 'b': return BISHOP;
    case 'r': return ROOK;
    case 'q': return QUEEN;
    case 'k': return KING;
    default:  return -1;
    }
}

char pieceChar(Piece pc) {
    static const char chars[6] = {'p', 'n', 'b', 'r', 'q', 'k'};
    char c = chars[type_of(pc)];
    if (color_of(pc) == WHITE)
        c = char(std::toupper((unsigned char)c));
    return c;
}

}  // namespace

void init_zobrist() {
    for (int p = 0; p < PIECE_NB; ++p)
        for (int s = 0; s < SQUARE_NB; ++s)
            ZobristPsq[p][s] = zrnd();
    ZobristSide = zrnd();
    for (int i = 0; i < 16; ++i)
        ZobristCastling[i] = zrnd();
    for (int f = 0; f < 8; ++f)
        ZobristEpFile[f] = zrnd();

    for (int s = 0; s < SQUARE_NB; ++s)
        CastlingRightsMask[s] = CR_WK | CR_WQ | CR_BK | CR_BQ;
    CastlingRightsMask[SQ_E1] &= ~(CR_WK | CR_WQ);
    CastlingRightsMask[SQ_A1] &= ~CR_WQ;
    CastlingRightsMask[SQ_H1] &= ~CR_WK;
    CastlingRightsMask[SQ_E8] &= ~(CR_BK | CR_BQ);
    CastlingRightsMask[SQ_A8] &= ~CR_BQ;
    CastlingRightsMask[SQ_H8] &= ~CR_BK;
}

Position::Position()
    : occ(0), side(WHITE), castling(0), ep(SQ_NONE), halfmove(0), fullmove(1), zobrist(0) {
    std::memset(byType, 0, sizeof(byType));
    std::memset(byColor, 0, sizeof(byColor));
    for (int s = 0; s < SQUARE_NB; ++s)
        board[s] = NO_PIECE;
}

void Position::putPiece(Piece pc, Square s) {
    const Bitboard b = square_bb(s);
    board[s] = pc;
    byType[type_of(pc)] |= b;
    byColor[color_of(pc)] |= b;
    occ |= b;
    zobrist ^= ZobristPsq[pc][s];
}

void Position::removePiece(Square s) {
    const Piece pc = board[s];
    const Bitboard b = square_bb(s);
    byType[type_of(pc)] &= ~b;
    byColor[color_of(pc)] &= ~b;
    occ &= ~b;
    board[s] = NO_PIECE;
    zobrist ^= ZobristPsq[pc][s];
}

void Position::movePiece(Square from, Square to) {
    const Piece pc = board[from];
    const Bitboard m = square_bb(from) | square_bb(to);
    board[from] = NO_PIECE;
    board[to] = pc;
    byType[type_of(pc)] ^= m;
    byColor[color_of(pc)] ^= m;
    occ ^= m;
    zobrist ^= ZobristPsq[pc][from] ^ ZobristPsq[pc][to];
}

Key Position::computeKey() const {
    Key k = 0;
    for (int s = 0; s < SQUARE_NB; ++s)
        if (board[s] != NO_PIECE)
            k ^= ZobristPsq[board[s]][s];
    k ^= ZobristCastling[castling & 15];
    if (ep != SQ_NONE)
        k ^= ZobristEpFile[file_of(ep)];
    if (side == BLACK)
        k ^= ZobristSide;
    return k;
}

bool Position::setFen(const std::string& fen, std::string* error) {
    std::memset(byType, 0, sizeof(byType));
    std::memset(byColor, 0, sizeof(byColor));
    for (int s = 0; s < SQUARE_NB; ++s)
        board[s] = NO_PIECE;
    occ = 0;
    side = WHITE;
    castling = 0;
    ep = SQ_NONE;
    halfmove = 0;
    fullmove = 1;
    zobrist = 0;

    std::istringstream ss(fen);
    std::string placement, sideStr, castleStr, epStr;
    if (!(ss >> placement >> sideStr >> castleStr >> epStr)) {
        if (error) *error = "FEN must contain at least 4 fields";
        return false;
    }

    int rank = 7, file = 0;
    for (char c : placement) {
        if (c == '/') {
            --rank;
            file = 0;
            continue;
        }
        if (c >= '1' && c <= '8') {
            file += c - '0';
            continue;
        }
        const int pt = pieceTypeFromChar(c);
        if (pt < 0 || rank < 0 || rank > 7 || file < 0 || file > 7) {
            if (error) *error = "invalid piece placement in FEN";
            return false;
        }
        const Color col = std::isupper((unsigned char)c) ? WHITE : BLACK;
        putPiece(make_piece(col, PieceType(pt)), make_square(file, rank));
        ++file;
    }

    if (sideStr == "w")      side = WHITE;
    else if (sideStr == "b") side = BLACK;
    else {
        if (error) *error = "invalid side to move in FEN";
        return false;
    }

    if (castleStr != "-") {
        for (char c : castleStr) {
            switch (c) {
            case 'K': castling |= CR_WK; break;
            case 'Q': castling |= CR_WQ; break;
            case 'k': castling |= CR_BK; break;
            case 'q': castling |= CR_BQ; break;
            default:
                if (error) *error = "invalid castling field in FEN";
                return false;
            }
        }
    }

    if (epStr != "-") {
        Square s = SQ_NONE;
        if (!parse_square(epStr, s)) {
            if (error) *error = "invalid en passant square in FEN";
            return false;
        }
        ep = s;
    }

    int hm = 0, fm = 1;
    if (ss >> hm) halfmove = hm < 0 ? 0 : hm;
    if (ss >> fm) fullmove = fm < 1 ? 1 : fm;

    // Normalise castling rights: only keep rights whose king and rook really
    // stand on their home squares (protects against sloppy FEN input).
    if (!(pieces(WHITE, KING) & square_bb(SQ_E1))) castling &= ~(CR_WK | CR_WQ);
    if (!(pieces(WHITE, ROOK) & square_bb(SQ_H1))) castling &= ~CR_WK;
    if (!(pieces(WHITE, ROOK) & square_bb(SQ_A1))) castling &= ~CR_WQ;
    if (!(pieces(BLACK, KING) & square_bb(SQ_E8))) castling &= ~(CR_BK | CR_BQ);
    if (!(pieces(BLACK, ROOK) & square_bb(SQ_H8))) castling &= ~CR_BK;
    if (!(pieces(BLACK, ROOK) & square_bb(SQ_A8))) castling &= ~CR_BQ;

    // Normalise the en passant square: keep it only if a capture is possible.
    if (ep != SQ_NONE && !(pawn_attacks(~side, ep) & pieces(side, PAWN)))
        ep = SQ_NONE;

    zobrist = computeKey();
    return true;
}

std::string Position::fen() const {
    std::ostringstream os;
    for (int r = 7; r >= 0; --r) {
        int empty = 0;
        for (int f = 0; f < 8; ++f) {
            const Square s = make_square(f, r);
            if (board[s] == NO_PIECE) {
                ++empty;
                continue;
            }
            if (empty) {
                os << empty;
                empty = 0;
            }
            os << pieceChar(board[s]);
        }
        if (empty)
            os << empty;
        if (r)
            os << '/';
    }

    os << (side == WHITE ? " w " : " b ");

    if (castling == 0)
        os << '-';
    else {
        if (castling & CR_WK) os << 'K';
        if (castling & CR_WQ) os << 'Q';
        if (castling & CR_BK) os << 'k';
        if (castling & CR_BQ) os << 'q';
    }

    os << ' ' << (ep == SQ_NONE ? "-" : square_to_string(ep));
    os << ' ' << halfmove << ' ' << fullmove;
    return os.str();
}

void Position::print(std::ostream& os) const {
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        os << ' ' << (r + 1) << " |";
        for (int f = 0; f < 8; ++f) {
            const Piece pc = board[make_square(f, r)];
            os << ' ' << (pc == NO_PIECE ? ' ' : pieceChar(pc)) << " |";
        }
        os << "\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "    a   b   c   d   e   f   g   h\n\n";
    os << "Fen: " << fen() << "\n";

    std::ostringstream hexKey;
    hexKey << std::hex << zobrist;
    os << "Key: " << hexKey.str() << "\n";

    Bitboard checkers = 0;
    if (inCheck()) {
        const Square ksq = kingSquare(side);
        checkers = (pieces(~side, KNIGHT) & knight_attacks(ksq))
                 | (pieces(~side, PAWN) & pawn_attacks(side, ksq))
                 | (pieces(~side, KING) & king_attacks(ksq))
                 | (pieces(~side, BISHOP) & bishop_attacks(ksq, occ))
                 | (pieces(~side, ROOK) & rook_attacks(ksq, occ))
                 | (pieces(~side, QUEEN) & queen_attacks(ksq, occ));
    }
    os << "Checkers:";
    if (!checkers)
        os << " -";
    for (Bitboard b = checkers; b;)
        os << ' ' << square_to_string(pop_lsb(b));
    os << '\n';
}

bool Position::sanityCheck(std::string* msg) const {
    auto fail = [&](const char* text) {
        if (msg) *msg = text;
        return false;
    };

    for (int pt = 0; pt < PIECE_TYPE_NB; ++pt)
        for (int c = 0; c < COLOR_NB; ++c)
            if (pieces(Color(c), PieceType(pt)) & ~occ)
                return fail("bitboard/occupancy mismatch");

    if ((pieces(WHITE) & pieces(BLACK)) != 0)
        return fail("overlapping colours");
    if ((pieces(WHITE) | pieces(BLACK)) != occ)
        return fail("colour bitboards do not cover occupancy");

    for (int s = 0; s < SQUARE_NB; ++s) {
        const Piece pc = board[s];
        if (pc == NO_PIECE)
            continue;
        if (!(square_bb(s) & pieces(color_of(pc), type_of(pc))))
            return fail("board[] disagrees with bitboards");
    }

    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1)
        return fail("exactly one king per side required");
    if (pieces(PAWN) & (Rank1BB | Rank8BB))
        return fail("pawn on first or last rank");
    if (ep != SQ_NONE && !(pawn_attacks(~side, ep) & pieces(side, PAWN)))
        return fail("en passant square without capturer");
    if (zobrist != computeKey())
        return fail("incremental Zobrist key out of sync");

    return true;
}

bool Position::isSquareAttacked(Square s, Color by) const {
    if (pawn_attacks(~by, s) & pieces(by, PAWN))
        return true;
    if (knight_attacks(s) & pieces(by, KNIGHT))
        return true;
    if (king_attacks(s) & pieces(by, KING))
        return true;

    const Bitboard rooksQueens = pieces(by, ROOK) | pieces(by, QUEEN);
    if (rooksQueens && (rook_attacks(s, occ) & rooksQueens))
        return true;

    const Bitboard bishopsQueens = pieces(by, BISHOP) | pieces(by, QUEEN);
    if (bishopsQueens && (bishop_attacks(s, occ) & bishopsQueens))
        return true;

    return false;
}

Bitboard Position::attackersTo(Square s, Bitboard occ2) const {
    const Bitboard bishopsQueens = pieces(BISHOP) | pieces(QUEEN);
    const Bitboard rooksQueens   = pieces(ROOK) | pieces(QUEEN);

    return (pawn_attacks(BLACK, s) & pieces(WHITE, PAWN))
         | (pawn_attacks(WHITE, s) & pieces(BLACK, PAWN))
         | (knight_attacks(s) & pieces(KNIGHT))
         | (king_attacks(s) & pieces(KING))
         | (bishop_attacks(s, occ2) & bishopsQueens)
         | (rook_attacks(s, occ2) & rooksQueens);
}

bool Position::isCapture(Move m) const {
    return board[to_sq(m)] != NO_PIECE || move_flag(m) == MF_ENPASSANT;
}

bool Position::isLegal(Move m) const {
    Position next = *this;
    next.applyMove(m);
    const Square ksq = next.kingSquare(side);
    return ksq != SQ_NONE && !next.isSquareAttacked(ksq, ~side);
}

bool Position::givesCheck(Move m) const {
    Position next = *this;
    next.applyMove(m);
    return next.inCheck();
}

void Position::applyMove(Move m) {
    const Square   from = from_sq(m);
    const Square   to   = to_sq(m);
    const MoveFlag flag = move_flag(m);
    const Piece    pc   = board[from];
    const Color    us   = side;
    const Color    them = ~us;

    // Leave the old (ep, castling) contributions to the key.
    if (ep != SQ_NONE)
        zobrist ^= ZobristEpFile[file_of(ep)];
    zobrist ^= ZobristCastling[castling & 15];

    if (flag == MF_ENPASSANT) {
        const Square capSq = Square(int(to) + (us == WHITE ? -8 : 8));
        removePiece(capSq);
        movePiece(from, to);
        halfmove = 0;
    } else if (flag == MF_CASTLE) {
        const bool kingSide = (to == (us == WHITE ? SQ_G1 : SQ_G8));
        const Square rookFrom = kingSide ? Square(int(to) + 1) : Square(int(to) - 2);
        const Square rookTo   = kingSide ? Square(int(to) - 1) : Square(int(to) + 1);
        movePiece(from, to);
        movePiece(rookFrom, rookTo);
        ++halfmove;
    } else {
        if (board[to] != NO_PIECE) {
            removePiece(to);
            halfmove = 0;
        } else {
            ++halfmove;
        }

        if (flag == MF_PROMOTION) {
            removePiece(from);
            putPiece(make_piece(us, promotion_type(m)), to);
            halfmove = 0;
        } else {
            movePiece(from, to);
            if (type_of(pc) == PAWN)
                halfmove = 0;
        }
    }

    castling &= CastlingRightsMask[from] & CastlingRightsMask[to];

    ep = SQ_NONE;
    if (type_of(pc) == PAWN && (int(to) - int(from) == 16 || int(from) - int(to) == 16)) {
        const Square candidate = Square((int(from) + int(to)) / 2);
        // Only record the square if the opponent really can capture there.
        if (pawn_attacks(us, candidate) & pieces(them, PAWN))
            ep = candidate;
    }

    if (ep != SQ_NONE)
        zobrist ^= ZobristEpFile[file_of(ep)];
    zobrist ^= ZobristCastling[castling & 15];
    zobrist ^= ZobristSide;

    if (us == BLACK)
        ++fullmove;

    side = them;
}

void Position::applyNullMove() {
    if (ep != SQ_NONE)
        zobrist ^= ZobristEpFile[file_of(ep)];
    ep = SQ_NONE;
    zobrist ^= ZobristSide;
    ++halfmove;
    if (side == BLACK)
        ++fullmove;
    side = ~side;
}



