// Included inside machine_gateway.cpp's namespace, after publish/log helpers.
int virtualFindMove(uint64_t fields, int& from, int& to) {
  return machineFindVirtualMove(fields, from, to);
}

void virtualFrame(const char board[64], uint8_t out[67]) {
  out[0] = 's';
  for (int sq = 0; sq < 64; ++sq) out[1 + modeBStatusWireIndex(sq % 8, sq / 8 + 1)] = board[sq];
  computeModeBChecksumHex(out + 65, out, 65, false);
}

void virtualSourceView(const char board[64], MachineVirtualOperator::Phase phase) {
  auto& op = virtualOperator;
  virtualFrame(board, op.sourceFrame);
  op.sourceDelivery.reset(); op.ack.reset();
  op.phase = phase; op.phaseAt = millis();
  // The receiving engine must never commit an unconfirmed promotion trial.
  op.cableOverride = !op.sourceSoftware;
  if (op.sourceSoftware) chesslinkServerPublishStatus(op.sourceFrame);
  else if (timing.allowActiveGoStatus()) sendCable(op.sourceFrame, 67);
}

void virtualPlace() {
  auto& op = virtualOperator;
  op.ack.reset(); op.phaseAt = millis();
  if (op.planCount > 1) {
    virtualSourceView(op.plan().target(), MachineVirtualOperator::ChoosingFigure);
    Serial.printf("[VBOARD] figure trial %c; awaiting moving engine confirmation\r\n", op.plan().promotion);
  } else {
    op.phase = MachineVirtualOperator::Confirming;
    publish(op.plan().target(), false);
    Serial.println("[VBOARD] full target placed; awaiting delivery to both engines");
  }
}

bool virtualStart() {
  auto& op = virtualOperator;
  op.reset();
  op.planCount = machineVirtualPlans(candidateFrom, candidateTo, op.plans, 4);
  if (!op.planCount) return false;
  op.sourceSoftware = players.accepts(true, machineWhiteToMove());
  op.phase = MachineVirtualOperator::Moving; op.phaseAt = millis();
  lifting = true; movementStage = 0;
  publish(op.plan().steps[0], false);
  Serial.printf("[VBOARD] move %c%d-%c%d: %u possible figure(s), lift published\r\n",
    'a' + op.plan().from % 8, op.plan().from / 8 + 1,
    'a' + op.plan().to % 8, op.plan().to / 8 + 1, op.planCount);
  return true;
}

void virtualRememberReply(uint64_t mask) {
  auto& op = virtualOperator;
  if (!mask) return;
  int from = -1, to = -1;
  bool found = machineFindReply(op.plan(), mask, from, to) == 1;
  if (!found) {
    // Never combine an old destination/correction with a future source.
    // The NEXT mover's piece in the full target anchors a split command.
    // E.g. the current ...Bf8-d6 correction cannot become Qa6xd6 just
    // because Qa6 is highlighted later. Its real destination is a5.
    uint64_t sources = 0;
    const bool nextWhite = !machineWhiteToMove();
    for (int sq = 0; sq < 64; ++sq) if (mask & (uint64_t(1) << sq)) {
      const char piece = op.plan().target()[sq];
      if (nextWhite ? (piece >= 'A' && piece <= 'Z') : (piece >= 'a' && piece <= 'z'))
        sources |= uint64_t(1) << sq;
    }
    if (sources) {
      if (sources != op.replySourceFields) {
        op.replySourceFields = sources;
        op.nextFound = false; op.nextFields = 0;
      }
      return;
    }
    if (!op.replySourceFields) return;
    found = machineFindReply(op.plan(), op.replySourceFields | mask, from, to) == 1;
  }
  if (found) {
    // A later complete command/destination supersedes an earlier tentative
    // pair; never latch the first legal combination for the entire move.
    const bool changed = !op.nextFound || from != op.nextFrom || to != op.nextTo;
    op.replySourceFields = uint64_t(1) << from;
    op.nextFields = (uint64_t(1) << from) | (uint64_t(1) << to);
    op.nextFound = true; op.nextFrom = from; op.nextTo = to;
    if (changed) Serial.printf("[VBOARD] queued reply %c%d-%c%d from fresh move fields\r\n",
      'a'+from%8,from/8+1,'a'+to%8,to/8+1);
  }
}

bool virtualLed(bool software, const uint8_t frame[167], uint64_t mask) {
  auto& op = virtualOperator;
  if (selection.board() || !op.active()) return false;
  bool allOff = true;
  for (unsigned i = 3; i < 165; ++i) if (frame[i] != '0') { allOff = false; break; }
  const int hi = nibble(frame[1]), lo = nibble(frame[2]);
  const uint32_t blinkPeriod = hi >= 0 && lo >= 0 ? uint32_t(hi * 16 + lo) * 33 : 0;
  const bool source = software == op.sourceSoftware;
  if ((op.phase == MachineVirtualOperator::ChoosingFigure && source) ||
      (op.phase == MachineVirtualOperator::Confirming && !source))
    op.ack.leds(allOff, millis(), blinkPeriod);
  if (!source && (op.phase == MachineVirtualOperator::Moving || op.phase == MachineVirtualOperator::Confirming)) {
    // Early replies remain separate from this move's correction fields.
    // A promotion trial has not reached the other engine, so cannot confirm it.
    if (op.planCount == 1 || op.phase == MachineVirtualOperator::Confirming) virtualRememberReply(mask);
  }
  return true;
}

void virtualHostPoll(bool software) {
  auto& op = virtualOperator;
  if (selection.board() || !op.active()) return;
  const bool source = software == op.sourceSoftware;
  if ((op.phase == MachineVirtualOperator::ChoosingFigure && source) ||
      (op.phase == MachineVirtualOperator::Confirming && !source)) op.ack.poll(millis());
}

void virtualHostClear(bool software) {
  auto& op = virtualOperator;
  if (selection.board() || !op.active()) return;
  const bool source = software == op.sourceSoftware;
  if ((op.phase == MachineVirtualOperator::ChoosingFigure && source) ||
      (op.phase == MachineVirtualOperator::Confirming && !source)) op.ack.leds(true, millis());
}

bool virtualFinish() {
  auto& op = virtualOperator;
  char before[64]; machineBoard(before);
  if (!machineCommitVirtual(op.plan())) {
    op.phase = MachineVirtualOperator::Blocked; halted = true;
    Serial.println("[VBOARD] STOP: confirmed target no longer matches the legal position"); return false;
  }
  if (!op.sourceSoftware) {
    uint64_t fields = 0;
    for (int sq = 0; sq < 64; ++sq) if (before[sq] != op.plan().target()[sq]) fields |= uint64_t(1) << sq;
    cableEcho.completed(fields);
  }
  const bool nextFound = op.nextFound;
  const int nextFrom = op.nextFrom, nextTo = op.nextTo;
  const uint64_t nextFields = nextFound ? op.nextFields : op.replySourceFields;
  op.reset(); lifting = false; gameAdvanced = true;
  pendingLeds.clear(); resetHints();
  lastPositionAt = millis(); // Recorder gets its full settle interval after confirmation.
  pgnRecorderOnBoardStatus(statusFrame);
  if (nextFields) {
    hints = nextFields; hintAt = millis();
    if (nextFound) { candidateFrom = nextFrom; candidateTo = nextTo; candidateAt = millis(); }
    for (int sq = 0; sq < 64; ++sq) if (hints & (uint64_t(1) << sq)) fieldSeenAt[sq] = millis();
  }
  Serial.println("[VBOARD] move confirmed; full position committed once");
  return true;
}

void virtualPoll(uint32_t now, bool ready) {
  auto& op = virtualOperator;
  if (!ready) {
    op.ack.reset(); op.nextFound = false; op.nextFields = op.replySourceFields = 0; op.replyLeds.clear(); op.disconnected = true;
    op.sourceDelivery.reset(); return;
  }
  if (op.disconnected) {
    op.disconnected = false; op.phaseAt = now;
    if (op.phase == MachineVirtualOperator::ChoosingFigure || op.phase == MachineVirtualOperator::RemovingChoice) {
      if (op.sourceSoftware) chesslinkServerPublishStatus(op.sourceFrame);
    }
  }
  if (op.phase == MachineVirtualOperator::Moving) {
    if (!delivery.ready(now, op.plan().holdMs) || !softwareStepSent ||
        uint32_t(now - softwareStepSentAt) < op.plan().holdMs) return;
    ++op.step; movementStage = int(op.step);
    if (op.step + 1 < op.plan().count) publish(op.plan().steps[op.step], false);
    else virtualPlace();
  } else if (op.phase == MachineVirtualOperator::ChoosingFigure || op.phase == MachineVirtualOperator::RemovingChoice) {
    bool sourceReady = false;
    if (op.sourceSoftware) {
      if (!op.sourceDelivery.sends && chesslinkServerMachineStatusSent(op.sourceFrame)) op.sourceDelivery.sent(now);
      sourceReady = op.sourceDelivery.sends && uint32_t(now - op.sourceDelivery.firstSentAt) >= 500;
    } else sourceReady = op.sourceDelivery.ready(now, 500);
    if (!sourceReady) return;
    if (op.phase == MachineVirtualOperator::RemovingChoice) {
      ++op.choice;
      virtualSourceView(op.plan().target(), MachineVirtualOperator::ChoosingFigure);
      Serial.printf("[VBOARD] figure trial %c; awaiting moving engine confirmation\r\n", op.plan().promotion);
    } else if (op.ack.accepted(now)) {
      op.cableOverride = false;
      op.phase = MachineVirtualOperator::Confirming; op.ack.reset(); op.phaseAt = now;
      publish(op.plan().target(), false);
      Serial.printf("[VBOARD] moving engine confirmed figure %c; target shared with other engine\r\n", op.plan().promotion);
    } else if (uint32_t(now - op.phaseAt) >= 3000 && op.ack.responsive(now)) {
      if (op.choice + 1 == op.planCount) {
        op.phase = MachineVirtualOperator::Blocked; halted = true;
        Serial.println("[VBOARD] STOP: no promotion figure confirmed; no default selected");
      } else {
        // Lift the unconfirmed trial before trying another piece.
        virtualSourceView(op.plan().steps[op.plan().count - 2], MachineVirtualOperator::RemovingChoice);
      }
    }
  } else if (op.phase == MachineVirtualOperator::Confirming) {
    // A known full target needs delivery, not an extra LED-off command.
    // Phoenix may send only S queries after accepting a move. Waiting for
    // an optional LED acknowledgement deadlocks the following reply.
    // Promotion targets reach this phase only after source figure approval.
    if (delivery.ready(now, 500) && softwareStepSent && uint32_t(now - softwareStepSentAt) >= 500)
      virtualFinish();
  }
}
