#define main legacy_main
#include "machine_gateway_test.cpp"
#undef main

bool sourceSoftware=false, reject=false, blinking=false, silent=false,slowBlink=false,explicitClear=false,wrongHost=false;
char desired[64];uint64_t intended=0;
void sourceStatusPoll() {
  if(sourceSoftware) machineGatewaySoftwarePoll();
  else {
    uint8_t frame[3]={'S',0,0};computeModeBChecksumHex(frame+1,frame,1,false);
    machineGatewayCommand(frame,3);
  }
}
void tick() {
  fakeNow+=100;
  if(physicalPromotion.negotiating()) {
    const bool choosing=physicalPromotion.phase==MachinePhysicalPromotion::Choosing;
    if(silent && choosing) {machineGatewayPoll();return;}
    sourceStatusPoll();
    bool matches=true;
    const uint8_t* frame=sourceSoftware?sentStatus.data():(cableStatuses.empty()?nullptr:cableStatuses.back().data());
    for(int i=0;frame && i<64;++i) if(frame[1+modeBStatusWireIndex(i%8,i/8+1)]!=desired[i]) matches=false;
    if(!frame) matches=false;
    uint64_t mask=matches && !reject?0:intended;
    if(blinking) mask=slowBlink?(fakeNow%8400<1100?intended:0):((fakeNow/200)%2?intended:0);
    uint8_t squares[2];size_t count=0;
    for(int i=0;i<64;++i) if(mask&(uint64_t(1)<<i)) squares[count++]=uint8_t(i);
    uint8_t leds[167];encodeLedFrame(squares,count,leds,false);
    if(slowBlink) {leds[1]=leds[2]='F';computeModeBChecksumHex(leds+165,leds,165,false);}
    if(explicitClear && !mask) {
      if(sourceSoftware) machineGatewaySoftwareClear();
      else {uint8_t clear[3]={'X',0,0};computeModeBChecksumHex(clear+1,clear,1,false);machineGatewayCommand(clear,3);}
    } else if(sourceSoftware) machineGatewaySoftwareLed(leds);else machineGatewayCommand(leds,167);
    if(wrongHost) physicalPromotionClear(!sourceSoftware);
  }
  machineGatewayPoll();
}
int main(int argc,char** argv) {
  assert(argc==7);
  const bool move=!strcmp(argv[1],"move"),white=!strcmp(argv[2],"white"),capture=!strcmp(argv[4],"capture");
  sourceSoftware=!strcmp(argv[3],"software");const char figure=argv[5][0];const std::string mode=argv[6];
  boot();testBoardType=move?BoardType::ChessnutMove:BoardType::Cynus;attachBoard();
  memset(virtualGame.board,'.',64);virtualGame.board[sq("e1")]='K';virtualGame.board[sq("e8")]='k';
  const char* from=white?(capture?"b7":"a7"):(capture?"b2":"a2");
  const char* to=white?(capture?"c8":"a8"):(capture?"c1":"a1");
  virtualGame.board[sq(from)]=white?'P':'p';if(capture) virtualGame.board[sq(to)]=white?'r':'R';
  virtualGame.whiteToMove=white;
  virtualGame.whiteCastleK=virtualGame.whiteCastleQ=virtualGame.blackCastleK=virtualGame.blackCastleQ=false;
  players.assignFirst(sourceSoftware==white);timing.observe(false,true);
  char before[64];machineBoard(before);publish(before);boardReport(before);pump(1500);
  memcpy(desired,before,64);desired[sq(from)]='.';desired[sq(to)]=white?figure:char(figure+32);
  intended=(uint64_t(1)<<sq(from))|(uint64_t(1)<<sq(to));
  const size_t recorded=completePositions.size(),otherStart=sourceSoftware?cableStatuses.size():notifications.size();
  if(mode=="stale") {physicalPromotionClear(sourceSoftware);sourceStatusPoll();reject=true;}
  wrongHost=mode=="wronghost";reject=reject || mode=="reject" || wrongHost;
  slowBlink=mode=="slowblink";blinking=mode=="blink" || slowBlink;silent=mode=="silence";explicitClear=mode=="clear";
  if(mode=="motorfail") {promotionWriteOk=false;moveWriteOk=false;}
  suggest(sourceSoftware,from,to);
  bool intervened=false;
  for(int i=0;i<650 && physicalPromotion.phase!=MachinePhysicalPromotion::Executing && !halted;++i) {
    tick();
    if(!halted && physicalPromotion.phase!=MachinePhysicalPromotion::Executing) assert(promotionCalls==0 && moveTargetCalls==0);
    if(physicalPromotion.phase==MachinePhysicalPromotion::Choosing && !intervened) {
      intervened=true;
      if(mode=="reset") {
        machineGatewayResetGame();assert(!physicalPromotion.active() && !lifting);
        assert(!memcmp(virtualGame.board,startingBoard,64));return 0;
      }
      if(mode=="disconnect") {
        machineGatewayBoardLink(false);
        for(int j=0;j<40;++j) tick();
        assert(physicalPromotion.paused && !promotionCalls && !moveTargetCalls);
        machineGatewayBoardLink(true);boardReport(before);
      }
      if(mode=="unexpected") {
        char wrong[64];memcpy(wrong,before,64);wrong[sq(from)]='.';boardReport(wrong);
      }
      if(mode=="subscription") {
        subscribed=false;for(int j=0;j<40;++j) tick();
        assert(physicalPromotion.paused && !promotionCalls && !moveTargetCalls);
        subscribed=true;
      }
      if(mode=="cablefail") testCableWriteOk=false;
    }
    if(physicalPromotion.phase==MachinePhysicalPromotion::Executing) break;
  }
  assert(completePositions.size()==recorded && !memcmp(virtualGame.board,before,64));
  const auto& other=sourceSoftware?cableStatuses:notifications;
  for(size_t i=otherStart;i<other.size();++i) for(int f=0;f<64;++f) assert(piece(other[i],f)==before[f]);
  const bool failure=reject || blinking || silent || mode=="cablefail" || mode=="unexpected" || mode=="motorfail";
  if(failure) {
    assert(halted && physicalPromotion.phase==MachinePhysicalPromotion::Blocked);
    assert(promotionCalls+moveTargetCalls==unsigned(mode=="motorfail"));
    for(int j=0;j<100;++j) tick();
    assert(promotionCalls+moveTargetCalls==unsigned(mode=="motorfail"));
    if(!move && mode=="motorfail") {
      assert(CynusDecoder::lastSentMoveUci.empty());
      assert(CynusDecoder::moveCycle==CynusDecoder::MoveCycle::WaitHumanMove);
    }
  } else {
    assert(lifting && !halted && physicalPromotion.phase==MachinePhysicalPromotion::Executing);
    assert(physicalPromotion.plan().promotion==figure && !memcmp(physicalExpected,desired,64));
    if(move) assert(moveTargetCalls==1 && !promotionCalls && !memcmp(moveTarget,desired,64));
    else {
      assert(promotionCalls==1 && !moveTargetCalls && promotionFrom==sq(from) && promotionTo==sq(to) && promotionPiece==figure);
      assert(CynusDecoder::motorWrites.size()==1);
      assert(CynusDecoder::motorWrites.back()==std::string("move ")+from+to+char(tolower(figure))+"\n");
      assert(CynusDecoder::cynusExecuteMachinePromotion(sq(from),sq(to),figure));
      assert(CynusDecoder::motorWrites.size()==1); // Duplicate call never repeats the motor command.
    }
    char wrong[64];memcpy(wrong,desired,64);wrong[sq(to)]=white?'P':'p';
    const size_t oldNotifications=notifications.size();boardReport(wrong);
    assert(!robotConfirmedReplay && completePositions.size()==recorded && notifications.size()==oldNotifications);
    boardReport(desired);assert(robotConfirmedReplay);
    pump(6500);
    assert(!lifting && !halted && !physicalPromotion.active());
    assert(!memcmp(virtualGame.board,desired,64) && machineWhiteToMove()!=white);
    assert(completePositions.size()==recorded+1 && piece(completePositions.back(),sq(to))==desired[sq(to)]);
    assert(promotionCalls+moveTargetCalls==1);
  }
  printf("Physical promotion %s/%s/%s/%s/%c/%s PASS\n",argv[1],argv[2],argv[3],argv[4],figure,argv[6]);
}
