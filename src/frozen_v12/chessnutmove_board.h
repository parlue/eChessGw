#pragma once

// Driver for the Chessnut MOVE board -- copied from chessnut_board.h/.cpp
// (the passive Chessnut Air/GO sensor-board driver) as the starting point,
// per the user's own instruction 2026-10-06, since Move's position-sensing
// side works the same way. What's actually different about Move: it's a
// robotic/motorized board, like Cynus -- it accepts a complete move and
// physically executes it itself, and exposes some kind of "currently
// moving, don't send another command yet" busy status. Chessnut's own
// official EasyLinkSDK doesn't document Move's motorized side at all (only
// the Air/Pro/Evo/Go passive family); see chessnutmoveSendMove()/
// chessnutmoveIsBusyMoving() below -- STUBBED, not wired to real protocol
// bytes yet, since those aren't verified from any official source and
// guessing wrong is worse than leaving this honestly unfinished.
//
// Inherited from the base Chessnut Air/GO driver (reverse-engineered; no
// official BLE SDK for that side either, only a USB-HID one): translates
// Chessnut's own binary board-status/LED wire format to and from the
// canonical Mode-B `'s'` status frame and SquareHighlight list used
// throughout this project. Whether Move's position/LED wire format is
// actually identical to Air/GO's is UNCONFIRMED -- needs real-hardware
// verification.

#include "board_driver.h"

// Matched as a case-insensitive substring against the BLE advertised name.
// "CHESSNUT-MOVE" confirmed via the community chessnut-move-rs port
// (daymxn/chessnut-move-rs DEVICE_NAME constant) -- user's own question
// 2026-10-06, "wird das grossgeschrieben?": yes. Must be registered in
// main.cpp's kKnownBoards[] BEFORE the generic kChessnutBoardName entry,
// since "chessnut-move" also contains "chessnut" and would otherwise match
// the plain (non-robotic) driver first.
extern const char kChessnutMoveBoardName[];
extern const char kChessnutMoveBoardNameAlt[];  // "chessnut move" space variant, defensive

bool chessnutmoveConnect(const NimBLEAddress& address);
bool chessnutmoveIsConnected();
void chessnutmovePoll();

// Lights exactly the given squares and turns off all others. Pass count=0
// (squares may be nullptr) to turn all LEDs off.
void chessnutmoveSetHighlightedSquares(const SquareHighlight* squares, size_t count);

// True whenever Phoenix's own last-decoded 'L' frame contained at least one
// real highlighted square (see dispatchLedFrameToBoard()) -- lets
// chessnut_board.cpp's own local "show whatever currently differs from the
// settled position" fallback defer to Phoenix's own suggestion whenever it
// has one, and only take over when Phoenix's frame is empty (observed to be
// the case for most squares/most of the time on real hardware, 2026-08-31).
extern bool phoenixHasActiveLedSuggestion;

// Re-applies the local "differs from settled position" highlight. Call this
// (instead of chessnutmoveSetHighlightedSquares(nullptr, 0)) whenever Phoenix's
// own 'L' frame has nothing to show -- Phoenix re-sends an empty frame
// continuously (confirmed on real hardware), and blindly clearing the LEDs
// on every one of those undid the local highlight within about a second of
// it appearing, before the player could act on it.
void chessnutmoveShowLocalBoardDeviations();

// True if any square currently differs from the last settled position --
// i.e. chessnutmoveShowLocalBoardDeviations() would have something real to
// show right now. dispatchLedFrameToBoard() checks this BEFORE deferring to
// Phoenix's own suggestion: a real physical discrepancy (e.g. a captured
// piece just removed from the board) must always win over whatever stale
// suggestion Phoenix might still be blinking from an earlier, already-
// superseded move -- confirmed on real hardware 2026-08-31 that the
// opposite priority (Phoenix always wins) silently ate the "piece missing"
// indicator during exactly the moment it mattered most, a real capture.
bool chessnutmoveHasLocalDeviation();

// Keep the latest host LED frame while local movement takes display priority.
void chessnutmoveDeferLedFrame(const uint8_t frame167[167]);

// The extra mode-selection queen is temporary, not a missing game piece.
void chessnutmoveTrackSelectionQueen(int mode);

// Count of BLE board-data notifications lost because our internal queue was
// full when they arrived -- added 2026-08-31 to test whether "sometimes
// yes, sometimes no" status updates for a given square are being dropped
// here rather than a Chessnut sensor problem (ruled out by the user on real
// hardware: "das Board funktioniert mit allen 100%").
uint32_t chessnutmoveDroppedBoardPackets();

// --- Robotic move execution (the actual difference vs. plain Chessnut) ---
// User's own spec 2026-10-06: "was hier anders ist und punktuell geaendert
// werden muss ist die Zugausfuehrung. Es versteht Positionen wie der Cynus
// und es sendet komplette Zuege [...] die es selbststaendig ausfuehrt...
// er hat auch irgendwo Zugriff auf einen Status das er sich bewegt in dem
// kein weiterer Befehl kommen soll." Followed up: "wesendet keine leds
// sondern zuege" -- same role as cynusExecuteHighlightedMove() (a connected
// client's "light these squares" command means "execute this move
// robotically" here, there's no real LEDs to light), so this takes the same
// shape/signature as that function rather than a from/to pair.
//
// Wire format verified against the community chessnut-move-rs port
// (github.com/daymxn/chessnut-move-rs) 2026-10-06, since Chessnut's own
// official EasyLinkSDK doesn't cover Move's motor side at all:
//   - Same GATT service/characteristic UUIDs as plain Chessnut Air (see
//     kWriteUuid etc. in chessnutmove_board.cpp) -- confirmed identical.
//   - auto_move command = 35 bytes: [0x42,0x21] prefix + 32-byte packed
//     target position (4 bits/square, two squares per byte, low nibble
//     first) + 1 mode byte (0x01 Normal / 0x00 Force).
//   - Wire square order is REVERSED from this project's own canonical
//     a1=0..h8=63 (board_driver.h boardSquareIndex()) order: Move wire
//     slot = 63 - canonicalIndex.
//   - Piece nibble codes (black low, white high): empty=0x0, black
//     Q=0x1 K=0x2 B=0x3 P=0x4 N=0x5, white R=0x6 P=0x7 B=0x9 N=0xa Q=0xb
//     K=0xc (black rook=0x8, no documented white equivalent collision --
//     taken verbatim from the reference source).
//   - No explicit busy/done event exists in that protocol either -- the
//     reference source only notes position notifications stop arriving
//     while the board is physically moving. chessnutmoveIsBusyMoving()
//     below approximates this with a timeout from send time.
// UNVERIFIED (needs real hardware): that this driver's own inherited
// settledBoard[]/currentBoard[64] (copied from chessnut_board.cpp) already
// use that same canonical a1..h8 index order -- encodeAutoMoveCommand() in
// the .cpp assumes so.
void chessnutmoveExecuteHighlightedMove(const SquareHighlight* highlights, size_t count);

// True while the board is still physically carrying out a move that was
// sent to it -- callers must not send another move (or any other command)
// while this is true. Approximated via a timeout (see .cpp), since the
// reference protocol has no explicit busy/done event.
bool chessnutmoveIsBusyMoving();
