#pragma once
#ifdef CHESSLINK_ENABLE_MACHINE
#include "board_driver.h"
#include "machine/selection.h"

bool machineGatewayActive();
bool machineGatewayWantsBoard();
void machineGatewayBoardLink(bool connected);
void machineGatewayBoardStatus(const uint8_t frame[67]);
void machineGatewayStopBoardSearch();
void machineGatewayCancelBoardCommand();
bool machineGatewaySelectPhysical();
bool machineGatewayAcceptSoftware();
void machineGatewayCableActivity();
void machineGatewayServiceSelection();
void machineGatewayCommand(const uint8_t* frame, size_t n);
void machineGatewaySoftwareLed(const uint8_t frame[167]);
void machineGatewaySoftwareClear();
void machineGatewayResetGame();
bool machineGatewayNewgameAllowed();
bool machineGatewaySoftNewgame();
bool machineGatewayNewgamePublished();
void machineGatewayNewgameFinish();
void machineGatewayPoll();
const uint8_t* machineGatewayStatus();
// Implemented by main; keeps UART ownership and parity in one place.
size_t machineGatewayWriteCable(const uint8_t* frame, size_t n);

// Narrow server hooks, present only in the integrated gateway build.
void chesslinkServerStopMachineOffer();
bool chesslinkServerMachineSubscribed();
bool chesslinkServerMachineStatusSent(const uint8_t frame[67]);
#endif
