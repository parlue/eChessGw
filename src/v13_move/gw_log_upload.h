#pragma once
#include <NimBLEDevice.h>

// Log upload to the "eChessGateway" tag app (2026-10-12). The app advertises the name below and runs a
// small GATT server; the gateway (which scans for boards anyway) connects to it as a client and pushes
// the stored log. Protocol, all ASCII except the log bytes themselves:
//   service  e5c40000-6c4f-4a3b-9d2e-6b1a7f0c0001
//   LOG char e5c40001-...  (gateway -> app, write without response)
//       "GWLOG1 <bytes> rec=<0|1> fw=<build> up=<ms>\n", the log text in chunks, "\n#GWLOG-END\n",
//       later "#OK <command>\n" replies
//   CMD char e5c40002-...  (app -> gateway, notify): lines "REC1", "REC0", "CLR", "BYE"
//   "PGN" (2026-10-16): the gateway answers on LOG with "PGN1 <games> <bytes>\n", every saved game as plain PGN text
//        (each followed by a line break) and "#PGN-END\n"; the games stay stored
//   "PGNDEL": deletes all saved games (the app sends it only after it has saved the file and the user agreed), reply "#OK PGNDEL\n"
//   any unknown line (the app sends "PING") only keeps the session open; after "PGN" the gateway waits 30 s for the next line
// After the log the gateway waits up to 20 s for commands (10 s more after each one), then disconnects.
namespace gwlog {
extern const char kTagName[];                    // "eChessGateway"
bool uploadTo(const NimBLEAddress& address);     // blocking; call from the BLE connect task, never loop()
}
