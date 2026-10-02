#pragma once
#include "board_driver.h"

void machineReset();
bool machineWhiteToMove();
// Returns number of legal moves compatible with the accumulated squares.
int machineFindMove(uint64_t squares, int& from, int& to);
void machineBoard(char out[64]);
void machineCommit(int from, int to);
bool machinePreview(int from, int to, char out[64]);
void machineSaveAndExport();
int machineCapturedSquare(int from, int to);
bool machineCouldBeMoveFields(uint64_t fields);
bool machineCaptureRemoved(int from, int to, char out[64]);

// Reserved for a future verified new-game event; not tied to S, X or T.
void machineRecorderRestart();
