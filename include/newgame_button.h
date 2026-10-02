#pragma once
#ifdef CHESSLINK_ENABLE_MACHINE
#include <NimBLEDevice.h>
void newgameButtonPoll(bool boardSearchBusy);
bool newgameButtonBusy();
bool newgameButtonKnownBoard(const NimBLEAdvertisedDevice* device);
#endif
