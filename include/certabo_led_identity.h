#pragma once
#include "certabo_protocol.h"

namespace certabo {
// Passive identification only. Accept a standalone L/D token, not letters in
// diagnostics or damaged decimal position payloads. No MTU guesses/probes.
// A bare reply without CRLF is completed by the next ':' position delimiter.
class LedIdentity {
 public:
  void reset() { selected_=tags_=LedType::Unknown; conflict_=false; positionSeen_=false; ambiguousTags_=false; packet64_=positions_=0; monoTagsSeen_=false; resetPending(); }
  // App diagnostic signature: ORIGINAL notification length, not negotiated MTU
  // or chunks made by our RX queue. Require repeated position-start packets.
  void notification(size_t length, uint8_t first) {
    if(first==':' && length==64 && packet64_<3) ++packet64_;
  }
  // Same tag evidence as eChessTactics detectKind (hardware observations,
  // 2026-10-24). Tag evidence overrides L/D: Spectrum may also send L.
  void observe(const RawBoard& board) {
    positionSeen_=true;
    if(positions_<3) ++positions_;
    if(tags_!=LedType::Unknown) return; // locked until reconnect
    unsigned spectrum=0,mono=0;
    for(const auto& id:board) {
      if(id[0]==2 && id[1]==0) ++spectrum;
      if(id[0]==93 || id[0]==94) ++mono;
    }
    ambiguousTags_=spectrum>0; // do not send a mono frame on partial RGB evidence
    if(mono) monoTagsSeen_=true;
    if(spectrum>=8 && mono==0) tags_=LedType::Rgb;
    else if(mono>=8 && spectrum==0) tags_=LedType::Mono;
  }
  bool positionSeen() const { return positionSeen_; }
  bool spectrumTags() const { return tags_==LedType::Rgb; }
  bool packetFallback() const { return tags_==LedType::Unknown && !monoTagsSeen_ && packet64_>=3 && positions_>=3; }
  bool spectrum() const { return spectrumTags() || packetFallback(); }
  void resetPending() { token_=0; length_=0; }
  void feed(uint8_t byte) {
    if (byte=='\r' || byte=='\n' || byte==':') {
      if (length_==1 && (token_=='L' || token_=='D')) {
        const LedType candidate=token_=='L' ? LedType::Mono : LedType::Rgb;
        if (selected_!=LedType::Unknown && selected_!=candidate) conflict_=true;
        else selected_=candidate;
      }
      resetPending();
      // ':' belongs to the position, so its content is never a marker token.
      if (byte==':') length_=2;
      return;
    }
    if (length_==0) token_=byte;
    if (length_<2) ++length_;
  }
  LedType type() const { return tags_!=LedType::Unknown ? tags_ : packetFallback() ? LedType::Rgb : (conflict_ || ambiguousTags_ ? LedType::Unknown : selected_); }
  bool conflict() const { return tags_==LedType::Unknown && !packetFallback() && conflict_; }
 private:
  LedType selected_=LedType::Unknown;
  LedType tags_=LedType::Unknown;
  bool positionSeen_=false, ambiguousTags_=false;
  uint8_t packet64_=0, positions_=0;
  bool monoTagsSeen_=false;
  bool conflict_=false;
  uint8_t token_=0, length_=0;
};
}
