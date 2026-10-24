#include "certabo_protocol.h"

#include <cstring>
#ifdef ARDUINO
#include "gw_serial.h"  // keep last: Serial -> log-recording wrapper
#endif

namespace certabo {
namespace {
constexpr char kStart[] = "rnbqkbnrpppppppp................................PPPPPPPPRNBQKBNR";
bool empty(const PieceId& id) { return id == PieceId{}; }
uint32_t checksum(const uint8_t* data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) hash = (hash ^ data[i]) * 16777619u;
  return hash;
}
}  // namespace

void Parser::resetStream() {
  count_ = size_ = number_ = digits_ = 0;
  active_ = cr_ = false;
}

bool Parser::finishNumber() {
  if (!digits_) return true;
  if (count_ >= 320) return false;
  board_[count_ / 5][count_ % 5] = static_cast<uint8_t>(number_);
  ++count_;
  number_ = digits_ = 0;
  return true;
}

ParseResult Parser::feed(uint8_t byte) {
  if (byte == ':') {
    const bool interrupted = active_;
    resetStream();
    active_ = true;
    return interrupted ? ParseResult::Invalid : ParseResult::None;
  }
  if (byte == 'L' || byte == 'D') leds = byte == 'L' ? LedType::Mono : LedType::Rgb;
  if (!active_) return ParseResult::None;
  if (++size_ > 2048) { resetStream(); return ParseResult::Invalid; }
  if (cr_) {
    const size_t count = count_;
    resetStream();
    if (byte != '\n') return ParseResult::Invalid;
    if (count == 320) return ParseResult::Position;
    if (count == 8) return ParseResult::OccupancyOnly;
    return ParseResult::Invalid;
  }
  // cer2nut removes bare LF line wrapping even inside a decimal token.
  if (byte == '\n' || byte == 'L' || byte == 'D') return ParseResult::None;
  if (byte >= '0' && byte <= '9') {
    number_ = static_cast<uint16_t>(number_ * 10 + byte - '0');
    if (++digits_ > 3 || number_ > 255) { resetStream(); return ParseResult::Invalid; }
    return ParseResult::None;
  }
  if (byte == ' ' || byte == '\r') {
    if (!finishNumber()) { resetStream(); return ParseResult::Invalid; }
    cr_ = byte == '\r';
    return ParseResult::None;
  }
  resetStream();
  return ParseResult::Invalid;
}

void Calibration::reset() {
  count_ = 0;
  resetWindow();
  entries_ = {};
}

bool Calibration::observe(const RawBoard& board, bool requireExtraQueens) {
  if (ready()) return false;
  history_[next_] = board;
  next_ = (next_ + 1) % history_.size();
  if (samples_ < history_.size()) ++samples_;
  if (samples_ < history_.size()) return false;
  std::array<Entry, 34> candidate{};
  size_t count = 0;
  for (size_t square = 0; square < 64; ++square) {
    PieceId id{};
    bool majority = false;
    for (const auto& sample : history_) {
      size_t votes = 0;
      for (const auto& other : history_) if (sample[square] == other[square]) ++votes;
      if (votes > history_.size() / 2) { id = sample[square]; majority = true; break; }
    }
    if (!majority) return false;
    const char piece = square == 19 ? 'q' : square == 43 ? 'Q' : kStart[square];
    if (piece == '.') { if (!empty(id)) return false; continue; }
    if (empty(id)) {
      if (!requireExtraQueens && (square == 19 || square == 43)) continue;
      return false;
    }
    for (size_t i = 0; i < count; ++i) if (candidate[i].id == id) return false;
    candidate[count].id = id;
    candidate[count++].piece = piece;
  }
  entries_ = candidate;
  count_ = count;
  return true;
}

bool Calibration::decode(const RawBoard& board, uint8_t frame[67]) const {
  if (!ready()) return false;
  uint8_t result[67]{};
  result[0] = 's';
  for (size_t square = 0; square < 64; ++square) {
    char piece = '.';
    if (!empty(board[square])) {
      bool found = false;
      for (size_t i = 0; i < count_; ++i) {
        if (board[square] == entries_[i].id) { piece = entries_[i].piece; found = true; break; }
      }
      if (!found) return false;  // Never silently turn an unknown ID into an empty square.
      for (size_t i = 0; i < square; ++i) if (board[i] == board[square]) return false;
    }
    result[64 - square] = static_cast<uint8_t>(piece);  // Mode-B h1..a8
  }
  uint8_t check = 0;
  for (size_t i = 0; i < 65; ++i) check ^= result[i];
  constexpr char hex[] = "0123456789ABCDEF";
  result[65] = hex[check >> 4];
  result[66] = hex[check & 15];
  memcpy(frame, result, sizeof(result));
  return true;
}

bool Calibration::save(uint8_t blob[kBlobSize]) const {
  if (!ready()) return false;
  memset(blob, 0, kBlobSize);
  blob[0] = 1;
  blob[1] = static_cast<uint8_t>(count_);
  for (size_t i = 0; i < count_; ++i) {
    memcpy(blob + 2 + i * 6, entries_[i].id.data(), 5);
    blob[7 + i * 6] = static_cast<uint8_t>(entries_[i].piece);
  }
  const uint32_t hash = checksum(blob, kBlobSize - 4);
  for (size_t i = 0; i < 4; ++i) blob[kBlobSize - 4 + i] = static_cast<uint8_t>(hash >> (8 * i));
  return true;
}

bool Calibration::load(const uint8_t* blob, size_t length) {
  if (!blob || length != kBlobSize || blob[0] != 1 || blob[1] < 32 || blob[1] > 34) return false;
  const uint32_t hash = checksum(blob, kBlobSize - 4);
  for (size_t i = 0; i < 4; ++i) if (blob[kBlobSize - 4 + i] != static_cast<uint8_t>(hash >> (8 * i))) return false;
  std::array<Entry, 34> candidate{};
  unsigned counts[12]{};
  const char* pieces = "PNBRQKpnbrqk";
  for (size_t i = 0; i < blob[1]; ++i) {
    memcpy(candidate[i].id.data(), blob + 2 + i * 6, 5);
    candidate[i].piece = static_cast<char>(blob[7 + i * 6]);
    const char* pos = candidate[i].piece ? strchr(pieces, candidate[i].piece) : nullptr;
    if (!pos || empty(candidate[i].id)) return false;
    ++counts[pos - pieces];
    for (size_t j = 0; j < i; ++j) if (candidate[i].id == candidate[j].id) return false;
  }
  const unsigned expected[6] = {8, 2, 2, 2, 1, 1};
  for (size_t i = 0; i < 12; ++i) {
    if (i % 6 == 4) { if (counts[i] < 1 || counts[i] > 2) return false; }
    else if (counts[i] != expected[i % 6]) return false;
  }
  entries_ = candidate;
  count_ = blob[1];
  resetWindow();
  return true;
}

void ledMask(const uint8_t* squares, size_t count, uint8_t mask[8]) {
  memset(mask, 0, 8);
  if (!squares) return;
  for (size_t i = 0; i < count; ++i) {
    if (squares[i] < 64) mask[7 - squares[i] / 8] |= static_cast<uint8_t>(1u << (squares[i] % 8));
  }
}

void rgbCommand(const uint8_t mask[8], uint8_t command[247]) {
  memset(command, 0, 247);
  command[0] = 0xff; command[1] = 0x55;
  command[245] = 13; command[246] = 10;
  for (int row = 0; row < 8; ++row) for (int bit = 0; bit < 8; ++bit) {
    if (!(mask[row] & (1u << bit))) continue;
    const int corner = (7 - row) * 9 + (7 - bit);
    // cer2nut's fixed color/brightness; four corners per square.
    for (int offset : {0, 1, 9, 10}) command[2 + (corner + offset) * 3 + 2] = 0x40;
  }
}
}  // namespace certabo
