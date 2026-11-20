#pragma once

// Learn the cable checksum convention only from unambiguous valid frames.
// This is a protocol observation, not a hardware model identifier.
class CableStatusPolicy {
 public:
  enum class Convention { Unknown, Plain, Encoded };

  void observe(bool plainMatches, bool encodedMatches) {
    if (encodedMatches && !plainMatches) convention_ = Convention::Encoded;
    else if (plainMatches && !encodedMatches && convention_ == Convention::Unknown)
      convention_ = Convention::Plain;
  }

  bool allowActiveGoStatus() const { return convention_ == Convention::Plain; }
  Convention convention() const { return convention_; }

 private:
  // Encoded evidence remains sticky for this boot, just like the existing
  // cableHostUsesEncodedChecksum flag. Unknown preserves Phoenix timing.
  Convention convention_ = Convention::Unknown;
};
