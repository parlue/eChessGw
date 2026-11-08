bool physicalPromotionLinkReady() {
  return boardOnline && boardSynchronized && !awaitingStart && chesslinkServerMachineSubscribed();
}
void physicalPromotionFrame(const char board[64]) {
  auto& p=physicalPromotion;
  p.frame[0]='s';
  for(int sq=0;sq<64;++sq) p.frame[1+modeBStatusWireIndex(sq%8,sq/8+1)]=board[sq];
  computeModeBChecksumHex(p.frame+65,p.frame,65,false);
  p.delivery.reset();p.ack.reset();p.phaseAt=millis();
  if(p.sourceSoftware) chesslinkServerPublishStatus(p.frame);
}
void physicalPromotionFail(const char* reason) {
  physicalPromotion.phase=MachinePhysicalPromotion::Blocked;halted=true;
  if(physicalPromotion.sourceSoftware) chesslinkServerPublishStatus(statusFrame);
  Serial.printf("[PROMOTION] STOP: %s; no unconfirmed completion\r\n",reason);
}
bool physicalPromotionDelivered(uint32_t now) {
  return physicalPromotion.delivery.ready(now,500);
}
void physicalPromotionHostPoll(bool software) {
  auto& p=physicalPromotion;
  if(!p.negotiating() || p.sourceSoftware!=software || p.paused || !physicalPromotionLinkReady()) return;
  if(software && chesslinkServerMachineStatusSent(p.frame)) p.delivery.sent(millis());
  if(p.phase==MachinePhysicalPromotion::Choosing && physicalPromotionDelivered(millis())) p.ack.poll(millis());
}
bool physicalPromotionClear(bool software) {
  auto& p=physicalPromotion;
  if(!p.negotiating()) return false;
  if(p.sourceSoftware==software && p.phase==MachinePhysicalPromotion::Choosing && !p.paused &&
      physicalPromotionLinkReady() && physicalPromotionDelivered(millis())) p.ack.leds(true,millis());
  return true;
}
bool physicalPromotionLed(bool software,const uint8_t* frame) {
  auto& p=physicalPromotion;
  if(!p.negotiating()) return p.phase==MachinePhysicalPromotion::Blocked;
  if(p.sourceSoftware==software && p.phase==MachinePhysicalPromotion::Choosing && !p.paused &&
      physicalPromotionLinkReady() && physicalPromotionDelivered(millis())) {
    bool off=true;for(unsigned i=3;i<165;++i) if(frame[i]!='0') {off=false;break;}
    const int hi=nibble(frame[1]),lo=nibble(frame[2]);
    const uint32_t period=hi>=0 && lo>=0?uint32_t(hi*16+lo)*33:0;
    p.ack.leds(off,millis(),period);
  }
  return true; // Feedback never reaches the motor or the other host.
}
void physicalPromotionStart(unsigned count) {
  auto& p=physicalPromotion;
  p.reset();p.count=count;p.sourceSoftware=players.accepts(true,machineWhiteToMove());
  if(count!=4 || (currentBoardType()!=BoardType::Cynus && currentBoardType()!=BoardType::ChessnutMove)) {
    physicalPromotionFail("physical board does not support exact promotion target");return;
  }
  p.phase=MachinePhysicalPromotion::Preparing;
  physicalPromotionFrame(p.plan().steps[0]);
  Serial.println("[PROMOTION] confirming figure with moving host; motors held");
}
void physicalPromotionPoll(uint32_t now,bool ready) {
  auto& p=physicalPromotion;
  if(!ready) {p.ack.reset();p.delivery.reset();p.paused=true;return;}
  if(p.paused) {
    p.paused=false;p.phaseAt=now;p.ack.reset();p.delivery.reset();
    if(p.sourceSoftware) chesslinkServerPublishStatus(p.frame);
  }
  if(p.sourceSoftware && chesslinkServerMachineStatusSent(p.frame) && !p.delivery.sends) p.delivery.sent(now);
  if(uint32_t(now-p.phaseAt)>30000) {physicalPromotionFail("host confirmation timed out");return;}
  if(!physicalPromotionDelivered(now)) return;
  if(p.phase==MachinePhysicalPromotion::Preparing) {
    ++p.step;
    if(p.step+1<p.plan().count) physicalPromotionFrame(p.plan().steps[p.step]);
    else {p.phase=MachinePhysicalPromotion::Choosing;physicalPromotionFrame(p.plan().target());}
  } else if(p.phase==MachinePhysicalPromotion::Removing) {
    ++p.choice;p.phase=MachinePhysicalPromotion::Choosing;physicalPromotionFrame(p.plan().target());
  } else if(p.phase==MachinePhysicalPromotion::Choosing) {
    if(p.ack.accepted(now)) {
      memcpy(physicalExpected,p.plan().target(),64);
      p.phase=MachinePhysicalPromotion::Executing;lifting=true;movementStage=0;castlingMotion=false;
      physicalLedAt=now;pendingLeds.clear();
      const bool sent=currentBoardType()==BoardType::ChessnutMove?
          chessnutmoveSetTargetPosition(physicalExpected):
          cynusExecuteMachinePromotion(p.plan().from,p.plan().to,p.plan().promotion);
      if(!sent) {physicalPromotionFail("motor command write failed; no automatic retry");return;}
      Serial.printf("[PROMOTION] figure %c confirmed; robot commanded once\r\n",p.plan().promotion);
    } else {
      const uint32_t dwell=p.ack.quietMs+1000>3000?p.ack.quietMs+1000:3000;
      if(uint32_t(now-p.phaseAt)>=dwell && p.ack.responsive(now)) {
        if(p.choice+1>=p.count) {physicalPromotionFail("no promotion figure confirmed");return;}
        p.phase=MachinePhysicalPromotion::Removing;
        physicalPromotionFrame(p.plan().steps[p.plan().count-2]);
      }
    }
  }
}
