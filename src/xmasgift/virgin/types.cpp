// Virgin - UCI chess engine
// Move / square text conversion helpers.
#include "types.h"

#include <cctype>

std::string move_to_uci(Move m) {
    if (m == 0 && move_flag(m) == MF_NORMAL && from_sq(m) == SQ_A1 && to_sq(m) == SQ_A1)
        return "0000";
    std::string r = square_to_string(from_sq(m)) + square_to_string(to_sq(m));
    if (is_promotion(m)) {
        static const char pc[4] = {'n', 'b', 'r', 'q'};
        r += pc[(int(m) >> 14) & 3];
    }
    return r;
}

bool parse_square(const std::string& s, Square& out) {
    if (s.size() < 2)
        return false;
    const char f = char(std::tolower((unsigned char)s[0]));
    const char r = s[1];
    if (f < 'a' || f > 'h' || r < '1' || r > '8')
        return false;
    out = make_square(f - 'a', r - '1');
    return true;
}

Move parse_uci_move(const std::string& s, bool& ok) {
    ok = false;
    Square from = SQ_NONE, to = SQ_NONE;
    if (!parse_square(s.substr(0, 2), from) || !parse_square(s.substr(2, 2), to))
        return 0;

    PieceType promo = KNIGHT;
    if (s.size() >= 5) {
        switch (std::tolower((unsigned char)s[4])) {
        case 'n': promo = KNIGHT; break;
        case 'b': promo = BISHOP; break;
        case 'r': promo = ROOK;   break;
        case 'q': promo = QUEEN;  break;
        default: return 0;
        }
        ok = true;
        return make_move(from, to, MF_PROMOTION, promo);
    }
    ok = true;
    return make_move(from, to);
}
