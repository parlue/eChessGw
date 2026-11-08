#pragma once
#include "board_driver.h"
#include "virtual_operator.h"
int machineFindVirtualMove(uint64_t squares, int& from, int& to);
unsigned machineVirtualPlans(int from, int to, MachineVirtualPlan* plans, unsigned capacity);
int machineFindReply(const MachineVirtualPlan& plan, uint64_t fields, int& from, int& to);
bool machineCommitVirtual(const MachineVirtualPlan& plan);

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
