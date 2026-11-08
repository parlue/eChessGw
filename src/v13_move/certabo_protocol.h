#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

// Transport-independent Certabo protocol. Reference: gkalab/cer2nut,
// 80ce6c8cdf89a60a7940e2233ea0bf85e667237c (see docs/certabo_ble.md).
namespace certabo {
using PieceId = std::array<uint8_t, 5>;
using RawBoard = std::array<PieceId, 64>;  // a8..h8, ..., a1..h1
enum class LedType { Unknown, Mono, Rgb };
enum class ParseResult { None, Position, OccupancyOnly, Invalid };

class Parser {
 public:
  ParseResult feed(uint8_t byte);
  void resetStream();
  void reset() { resetStream(); leds = LedType::Unknown; }
  const RawBoard& board() const { return board_; }
  LedType leds = LedType::Unknown;

 private:
  bool finishNumber();
  RawBoard board_{};
  size_t count_ = 0;
  size_t size_ = 0;
  uint16_t number_ = 0;
  uint8_t digits_ = 0;
  bool active_ = false;
  bool cr_ = false;
};

// Seven-frame majority calibration in the normal starting position,
// optional black queen d6 / white queen d3, as in cer2nut.
class Calibration {
 public:
  static constexpr size_t kBlobSize = 210;
  void reset();
  void resetWindow() { samples_ = next_ = 0; }
  bool observe(const RawBoard& board, bool requireExtraQueens = false,
               bool selectByQueenLayout = false);
  bool ready() const { return count_ >= 32; }
  size_t count() const { return count_; }
  // Set only by live calibration, never restored from a figure archive.
  LedType setupType() const { return setupType_; }
  bool decode(const RawBoard& board, uint8_t frame[67]) const;
  bool save(uint8_t blob[kBlobSize]) const;
  bool load(const uint8_t* blob, size_t length);

 private:
  struct Entry { PieceId id{}; char piece = '.'; };
  std::array<Entry, 34> entries_{};
  std::array<RawBoard, 7> history_{};
  size_t count_ = 0;
  size_t samples_ = 0;
  size_t next_ = 0;
  LedType setupType_ = LedType::Unknown;
};

// Square indices are a1=0 through h8=63, matching boardSquareIndex().
void ledMask(const uint8_t* squares, size_t count, uint8_t mask[8]);
void rgbCommand(const uint8_t mask[8], uint8_t command[247]);
}  // namespace certabo
