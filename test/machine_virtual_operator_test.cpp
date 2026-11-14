#ifndef TEST_DEFAULT_VIRTUAL_PROMOTION
#define CHESSLINK_VIRTUAL_OPERATOR_V2 1
#endif
#define main legacy_main
#include "machine_gateway_test.cpp"
#undef main

char expectedBoard[64];
bool sourceIsSoftware = false, forceRejectSource = false;
bool respondSoftware = true, respondCable = true;
uint64_t lastSoftwareMask = 0;
bool softwareMaskKnown = false;
uint64_t intendedFields = 0;

uint64_t diff(const Frame& f) {
  uint64_t mask = 0;
  for (int i = 0; i < 64; ++i) if (piece(f, i) != expectedBoard[i]) mask |= uint64_t(1) << i;
  return mask;
}

void sendFields(bool software, uint64_t fields) {
  uint8_t squares[64]; size_t count = 0;
  for (int i = 0; i < 64; ++i) if (fields & (uint64_t(1) << i)) squares[count++] = uint8_t(i);
  uint8_t frame[167]; encodeLedFrame(squares, count, frame, false);
  if (software) machineGatewaySoftwareLed(frame); else machineGatewayCommand(frame, 167);
}

// Mock the two external engines' FULL-position comparison. The moving
// engine owns its expected promotion piece; the implementation under test
// gets neither that piece nor this array. M5 sends LED changes only once.
void simulate(unsigned ms) {
  for (unsigned elapsed = 0; elapsed < ms; elapsed += 100) {
    fakeNow += 100;
    if (respondCable) {
      uint64_t fields = cableStatuses.empty() ? intendedFields : diff(cableStatuses.back());
      if (!sourceIsSoftware && forceRejectSource) fields = intendedFields;
      sendFields(false, fields);
    }
    if (respondSoftware && subscribed) {
      uint64_t fields = diff(sentStatus);
      if (sourceIsSoftware && fields) fields = intendedFields;
      if (sourceIsSoftware && forceRejectSource) fields = intendedFields;
      if (!softwareMaskKnown || fields != lastSoftwareMask) {
        sendFields(true, fields); lastSoftwareMask = fields; softwareMaskKnown = true;
      }
      if (fakeNow % 500 == 0) machineGatewaySoftwarePoll();
    }
    machineGatewayPoll();
  }
}

void knownMove(bool software, const char* from, const char* to, unsigned stages = 2) {
  assert(machinePreview(sq(from), sq(to), expectedBoard));
  sourceIsSoftware = software; softwareMaskKnown = false;
  intendedFields = (uint64_t(1) << sq(from)) | (uint64_t(1) << sq(to));
  const size_t before = notifications.size(), recorded = completePositions.size();
  suggest(software, from, to); simulate(8000);
  assert(!lifting && !halted && !virtualOperator.active());
  assert(notifications.size() == before + stages && completePositions.size() == recorded + 1);
  assert(memcmp(virtualGame.board, expectedBoard, 64) == 0);
}

void sparsePromotion(bool white, bool software, bool capture, char figure) {
  memset(virtualGame.board, '.', 64);
  virtualGame.board[sq("e1")] = 'K'; virtualGame.board[sq("e8")] = 'k';
  // Include the live Phoenix regression a7-a8=Q at the board edge.
  const char* from = white ? "a7" : "b2";
  const char* to = white ? (capture ? "b8" : "a8") : (capture ? "c1" : "b1");
  virtualGame.board[sq(from)] = white ? 'P' : 'p';
  if (capture) virtualGame.board[sq(to)] = white ? 'r' : 'R';
  virtualGame.whiteToMove = white;
  virtualGame.whiteCastleK = virtualGame.whiteCastleQ = false;
  virtualGame.blackCastleK = virtualGame.blackCastleQ = false;
  players.assignFirst(software == white); timing.observe(false, true);
  publish(virtualGame.board); pump(1500);
  memcpy(expectedBoard, virtualGame.board, 64);
  expectedBoard[sq(from)] = '.'; expectedBoard[sq(to)] = white ? figure : char(figure + 32);
  intendedFields = (uint64_t(1) << sq(from)) | (uint64_t(1) << sq(to));
  sourceIsSoftware = software; softwareMaskKnown = false;
  const size_t notifyStart = notifications.size(), cableStart = cableStatuses.size(), recorded = completePositions.size();
  suggest(software, from, to);
  simulate(24000);
  if (forceRejectSource) {
    assert(halted && virtualOperator.phase == MachineVirtualOperator::Blocked);
    assert(virtualGame.board[sq(from)] == (white ? 'P' : 'p'));
    assert(completePositions.size() == recorded);
    return;
  }
  assert(!halted && !lifting && !virtualOperator.active());
  assert(memcmp(virtualGame.board, expectedBoard, 64) == 0 && machineWhiteToMove() != white);
  assert(completePositions.size() == recorded + 1);
  assert(piece(completePositions.back(), sq(to)) == expectedBoard[sq(to)]);
  // The other engine never receives a wrong trial piece. In a capture,
  // the original victim may be present only until it is first lifted.
  bool sawEmpty = false;
  const auto& received = software ? cableStatuses : notifications;
  const size_t start = software ? cableStart : notifyStart;
  for (size_t i = start; i < received.size(); ++i) {
    const char actual = piece(received[i], sq(to));
    if (actual == '.') sawEmpty = true;
    if (sawEmpty) assert(actual == '.' || actual == expectedBoard[sq(to)]);
  }
  assert(sawEmpty && physicalLedCalls == 0);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  const char* scenario = argv[1];
  if (!strncmp(scenario, "physical_", 9)) { argv[1] += 9; return legacy_main(argc, argv); }
  if (!strcmp(scenario, "ack")) {
    MachineVirtualAck ack;
    ack.leds(true, 1000); ack.poll(1500); assert(!ack.accepted(2000));
    ack.leds(false, 1900); ack.leds(true, 2000); ack.poll(2500); assert(!ack.accepted(2600));
    ack.poll(3000); assert(ack.accepted(3000)); assert(!ack.accepted(4500));
    ack.reset(); ack.leds(true, 1000); assert(!ack.accepted(100000));
    ack.reset(); ack.leds(true, 1000, 8000); ack.poll(2000); ack.poll(3000); assert(!ack.accepted(3000));
    puts("Virtual acknowledgement: blink/silence/stale clear cannot confirm."); return 0;
  }
  boot(); timing.observe(false, true);
  if (!strncmp(scenario, "promotion_", 10)) {
    const bool white = strstr(scenario, "white") != nullptr;
    const bool software = strstr(scenario, "software") != nullptr;
    const bool capture = strstr(scenario, "capture") != nullptr;
    const char* last = strrchr(scenario, '_'); assert(last && strchr("QRBN", last[1]));
    forceRejectSource = strstr(scenario, "reject") != nullptr;
    sparsePromotion(white, software, capture, last[1]);
  } else if (!strncmp(scenario, "queen_a1_no_clear", 17)) {
    const char before[]="....R.K." "..P..PP." ".Pq....P" "P..PQ..."
                        "p......." ".pp.r..." "..p.Bppp" "....r.k.";
    memcpy(virtualGame.board,before,64);virtualGame.whiteToMove=true;
    virtualGame.whiteCastleK=virtualGame.whiteCastleQ=virtualGame.blackCastleK=virtualGame.blackCastleQ=false;
    const bool moverSoftware = strstr(scenario,"reverse")==nullptr;
    const bool earlyReply = strstr(scenario,"early")!=nullptr;
    players.assignFirst(moverSoftware);publish(before);pump(1500);
    // Phoenix can keep polling without sending another LED-clear command.
    suggest(moverSoftware,"e1","e3");
    bool replySent=false;
    for(int i=0;i<70;++i) {
      fakeNow+=100;
      const uint8_t query[]={'S','5','3'};
      machineGatewayCommand(query,3);machineGatewayPoll();
      if(earlyReply && !replySent && virtualOperator.phase==MachineVirtualOperator::Confirming) {
        suggest(!moverSoftware,"c3","a1");replySent=true;
      }
      if(earlyReply && replySent && !lifting) break;
    }
    assert(!lifting && !virtualOperator.active());
    assert(virtualGame.board[sq("e3")]=='R' && !machineWhiteToMove());
    assert(machinePreview(sq("c3"),sq("a1"),expectedBoard));
    sourceIsSoftware=!moverSoftware;softwareMaskKnown=false;
    intendedFields=(uint64_t(1)<<sq("c3"))|(uint64_t(1)<<sq("a1"));
    if(!earlyReply) suggest(!moverSoftware,"c3","a1");
    simulate(7000);
    assert(!lifting && !halted && virtualGame.board[sq("a1")]=='q');
    assert(piece(sentStatus,sq("a1"))=='q');
  } else if (!strncmp(scenario, "photo_qa5_stale_d6", 18)) {
    knownMove(false,"d2","d4");knownMove(true,"g8","f6");
    knownMove(false,"c2","c3");knownMove(true,"d7","d5");
    knownMove(false,"g1","f3");knownMove(true,"b8","c6");
    knownMove(false,"b1","d2");knownMove(true,"c8","f5");
    knownMove(false,"d1","b3");knownMove(true,"e7","e6");
    knownMove(false,"b3","b7",3);knownMove(true,"c6","a5");
    knownMove(false,"b7","a6");
    const bool idleReply = strstr(scenario,"idle")!=nullptr;
    if(idleReply) knownMove(true,"f8","d6");
    else {
      suggest(true,"f8","d6");
      for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::Moving;++i)pump(100);
      assert(virtualOperator.phase==MachineVirtualOperator::Moving);
    }
    // A previous bishop correction and the new queen's source can form
    // a different legal capture before the real destination arrives.
    sendFields(false,uint64_t(1)<<sq("d6"));
    sendFields(false,uint64_t(1)<<sq("a6"));
    sendFields(false,uint64_t(1)<<sq("a5"));
    if(!idleReply) {
      assert(virtualOperator.nextFound && virtualOperator.nextTo==sq("a5"));
      for(int i=0;i<60 && lifting;++i)pump(100);
    }
    assert(!lifting && candidateFrom==sq("a6") && candidateTo==sq("a5"));
    assert(machinePreview(sq("a6"),sq("a5"),expectedBoard));
    sourceIsSoftware=false;softwareMaskKnown=false;
    intendedFields=(uint64_t(1)<<sq("a6"))|(uint64_t(1)<<sq("a5"));
    simulate(7000);
    assert(!lifting && !halted);
    assert(virtualGame.board[sq("a5")]=='Q' && virtualGame.board[sq("d6")]=='b');
    assert(virtualGame.board[sq("d8")]=='q' && virtualGame.board[sq("a6")]=='.');
    assert(piece(sentStatus,sq("a5"))=='Q' && piece(sentStatus,sq("d6"))=='b');
  } else if (!strcmp(scenario, "quiet")) {
    knownMove(false,"e2","e4"); knownMove(true,"e7","e6");
    knownMove(false,"d2","d4"); knownMove(true,"f8","d6");
  } else if (!strcmp(scenario, "capture")) {
    knownMove(true,"e2","e4"); knownMove(false,"d7","d5"); knownMove(true,"e4","d5",3);
  } else if (!strcmp(scenario, "ep")) {
    knownMove(false,"e2","e4"); knownMove(true,"a7","a6");
    knownMove(false,"e4","e5"); knownMove(true,"d7","d5"); knownMove(false,"e5","d6",3);
  } else if (!strcmp(scenario, "castle")) {
    knownMove(true,"e2","e4"); knownMove(false,"e7","e5");
    knownMove(true,"g1","f3"); knownMove(false,"b8","c6");
    knownMove(true,"f1","c4"); knownMove(false,"g8","f6"); knownMove(true,"e1","g1",4);
  } else if (!strcmp(scenario, "live")) {
    knownMove(false,"d2","d4"); knownMove(true,"g8","f6");
    knownMove(false,"c2","c3"); knownMove(true,"d7","d5");
    knownMove(false,"g1","f3"); knownMove(true,"b8","c6");
    knownMove(false,"b1","d2"); knownMove(true,"c8","f5");
    knownMove(false,"d1","b3"); knownMove(true,"g7","g6");
    knownMove(false,"b3","b7",3); knownMove(true,"f5","d7");
    knownMove(false,"f3","e5"); knownMove(true,"c6","e5",3);
    knownMove(false,"d4","e5",3); knownMove(true,"a8","b8");
  } else if (!strcmp(scenario, "confirmation")) {
    assert(machinePreview(sq("e2"),sq("e4"),expectedBoard));
    sourceIsSoftware=false; intendedFields=(uint64_t(1)<<sq("e2"))|(uint64_t(1)<<sq("e4"));
    respondSoftware=false;
    const size_t recorded=completePositions.size();
    suggest(false,"e2","e4");simulate(7000);
    // Delivery alone completes an exactly known target even without LED clear.
    assert(!lifting && !virtualOperator.active());
    assert(virtualGame.board[sq("e4")]=='P' && completePositions.size()==recorded+1);
  } else if (!strcmp(scenario, "disconnect")) {
    assert(machinePreview(sq("e2"),sq("e4"),expectedBoard));
    sourceIsSoftware=false; intendedFields=(uint64_t(1)<<sq("e2"))|(uint64_t(1)<<sq("e4"));
    respondSoftware=false; suggest(false,"e2","e4");
    for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::Confirming;++i)simulate(100);
    assert(lifting); subscribed=false;simulate(5000);
    assert(lifting && virtualGame.board[sq("e2")]=='P');
    subscribed=true;chesslinkServerPublishStatus(statusFrame);respondSoftware=true;simulate(3000);
    assert(!lifting && !halted && virtualGame.board[sq("e4")]=='P');
  } else if (!strcmp(scenario, "fast_reply")) {
    assert(machinePreview(sq("e2"),sq("e4"),expectedBoard));
    sourceIsSoftware=false;intendedFields=(uint64_t(1)<<sq("e2"))|(uint64_t(1)<<sq("e4"));
    respondSoftware=false;suggest(false,"e2","e4");
    for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::Confirming;++i)simulate(100);
    assert(virtualOperator.phase==MachineVirtualOperator::Confirming);
    suggest(true,"g8","f6");assert(virtualOperator.nextFound);simulate(700);
    assert(!lifting && candidateFrom==sq("g8") && candidateTo==sq("f6"));
    assert(machinePreview(sq("g8"),sq("f6"),expectedBoard));
    sourceIsSoftware=true;intendedFields=(uint64_t(1)<<sq("g8"))|(uint64_t(1)<<sq("f6"));
    respondSoftware=true;softwareMaskKnown=false;simulate(7000);
    assert(!lifting && !halted && virtualGame.board[sq("f6")]=='n');
  } else if (!strcmp(scenario, "split_reply")) {
    suggest(true,"e2","e4");
    for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::Confirming;++i)pump(100);
    assert(virtualOperator.phase==MachineVirtualOperator::Confirming);
    sendFields(false,uint64_t(1)<<sq("e2")); sendFields(false,uint64_t(1)<<sq("e4"));
    sendFields(false,uint64_t(1)<<sq("g8")); sendFields(false,0);
    sendFields(false,uint64_t(1)<<sq("f6"));
    assert(virtualOperator.nextFound && virtualOperator.nextFrom==sq("g8") && virtualOperator.nextTo==sq("f6"));
    pump(700);assert(!lifting && candidateFrom==sq("g8") && candidateTo==sq("f6"));
  } else if (!strcmp(scenario, "blink")) {
    suggest(false,"e2","e4");
    for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::Confirming;++i)pump(100);
    assert(virtualOperator.phase==MachineVirtualOperator::Confirming);
    const size_t recorded=completePositions.size();
    // Optional, blinking LED feedback cannot prevent delivery completion.
    for(int i=0;i<12;++i) {
      sendFields(true,i%2 ? uint64_t(1)<<sq("e2") : 0);
      fakeNow+=100;cableIdle();machineGatewayPoll();
    }
    assert(!lifting && virtualGame.board[sq("e4")]=='P' && completePositions.size()==recorded+1);
  } else if (!strcmp(scenario, "live_b8")) {
    const char before[]="........" ".......P" "........" ".K.N.P.P"
                        "........" "..P...p." ".P...pk." "R.......";
    memcpy(virtualGame.board,before,64);virtualGame.whiteToMove=true;
    virtualGame.whiteCastleK=virtualGame.whiteCastleQ=virtualGame.blackCastleK=virtualGame.blackCastleQ=false;
    players.assignFirst(false);publish(before);pump(1500);
    memcpy(expectedBoard,before,64);expectedBoard[sq("b7")]='.';expectedBoard[sq("b8")]='Q';
    sourceIsSoftware=false;intendedFields=(uint64_t(1)<<sq("b7"))|(uint64_t(1)<<sq("b8"));
    const size_t recorded=completePositions.size();
    suggest(false,"b7","b8");simulate(24000);
    assert(!halted && !lifting && virtualGame.board[sq("b8")]=='Q' && !machineWhiteToMove());
    assert(completePositions.size()==recorded+1);
  } else if (!strcmp(scenario, "reset_pending")) {
    memset(virtualGame.board,'.',64);virtualGame.board[sq("e1")]='K';virtualGame.board[sq("e8")]='k';
    virtualGame.board[sq("b7")]='P';players.assignFirst(false);
    virtualGame.whiteCastleK=virtualGame.whiteCastleQ=virtualGame.blackCastleK=virtualGame.blackCastleQ=false;
    publish(virtualGame.board);pump(1500);suggest(false,"b7","b8");
    for(int i=0;i<40 && virtualOperator.phase!=MachineVirtualOperator::ChoosingFigure;++i)pump(100);
    assert(virtualOperator.phase==MachineVirtualOperator::ChoosingFigure);
    machineGatewayResetGame();
    assert(!virtualOperator.active() && !virtualOperator.cableOverride && !lifting && !halted);
    assert(!memcmp(virtualGame.board,kStartBoard,64) && !players.assigned());
    knownMove(true,"e2","e4");
  } else { fprintf(stderr,"Unknown virtual operator scenario: %s\n",scenario); return 2; }
  printf("Virtual operator test passed: %s\n",scenario);
}
