#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include "xmas_runtime.h"
#include "xmas_game.h"
#include "virgin/adaptive.h"
#include "virgin/adaptive_params.h"
#include "virgin/botdata.h"
#include "virgin/eval.h"
#include "virgin/search.h"
#include "virgin/tt.h"
#include "virgin/weights.h"
#include "virgin/opening_book.h"
#include "virgin/version.h"
#include "board_driver.h"
#include "millennium_board.h"
#include "chessnut_board.h"
#include "chessnutmove_board.h"
#include "certabo_board.h"
#include "ichessone_board.h"
#include "cynus_board.h"
#include <atomic>
#include <new>
#include <ostream>
#include "gw_serial.h"

namespace {
xmas::Controller controller;
portMUX_TYPE inputMux=portMUX_INITIALIZER_UNLOCKED;
xmas::Board latest{};
bool haveStatus=false;
std::atomic<bool> own{false};
uint64_t wanted=0, lastMask=~uint64_t(0);
uint32_t lastLedAt=0;
bool initialized=false;
// Explicit USB self-test: only available without a physical board or host.
// A leading '?' keeps the PGN tool's '#' acknowledgement bytes untouched.
unsigned diagnostic=0, diagnosticPlies=0;
uint32_t diagnosticAt=0;
xmas::Board diagnosticBoard{};
char diagnosticLine[48]{};
unsigned diagnosticLength=0;
bool diagnosticReading=false;
AdaptiveState adaptive;
Move commanded=0;
struct Job {
    Position pos;
    std::vector<Key> history;
    AdaptiveState state;
    unsigned ply;
    Move move=0;
    int score=0;
    bool ok=false;
    std::atomic<bool> cancel{false},done{false};
    Job(const xmas::Game& g):pos(g.pos),history(g.history),state(adaptive),ply(g.ply){}
};
std::unique_ptr<Job> job;
Job* searching=nullptr;
uint32_t lastYield=0;
// GCC stack reports bound the compact 20-ply recursion below this budget.
// Device diagnostics additionally check the actual high-water mark.
constexpr uint32_t stackBytes=56*1024;
extern "C" const uint32_t c3SearchMemoryBytes;
bool memoryReady(){
    const size_t heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    const size_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    Serial.printf("[XMAS MEMORY] heap=%u largest=%u stack=%u\r\n",unsigned(heap),unsigned(largest),unsigned(stackBytes));
    return heap>=stackBytes+c3SearchMemoryBytes+8*1024+12*1024 && largest>=stackBytes;
}
void initTables(){
    if(initialized)return;
    init_bitboards();init_zobrist();init_weights();initSearch();
    applyStyle("Fire");clearStrength();setThreads(1);initialized=true;
}
class NullBuffer:public std::streambuf{
    int_type overflow(int_type c)override{return traits_type::not_eof(c);}
    std::streamsize xsputn(const char*,std::streamsize n)override{return n;}
};
void searchPoll(){
    if(searching->cancel.load(std::memory_order_relaxed))stopSearch();
    uint32_t now=millis();if(uint32_t(now-lastYield)>=10){lastYield=now;vTaskDelay(1);}
}
void worker(void* arg){
    Job& j=*static_cast<Job*>(arg);searching=&j;
    try{
        const Move bookMove=j.cancel ? 0 : OpeningBook::choose(j.pos,double(esp_random())/4294967296.0);
        if(bookMove){
            j.move=bookMove;Position after=j.pos;after.applyMove(bookMove);
            j.score=evaluate(after);j.ok=true;
            Serial.printf("[XMAS BOOK] Virgin %s bestmove=%s\r\n",VIRGIN_VERSION,move_to_uci(bookMove).c_str());
        }else if(!j.cancel && allocateSessionSearch()){
            TT.resizeKiB(8);
            if (!TT.entries()) {
                Serial.println("[XMAS] hash reserve unavailable; search safely cancelled");
            } else {
            newSearchGame();setSearchPoll(searchPoll);
            SearchLimits limits;limits.movetime=TalParams::suche_denkzeit_ms;limits.multiPV=TalParams::suche_kandidaten;
            NullBuffer buffer;std::ostream sink(&buffer);
            auto result=searchPosition(j.pos,limits,j.history,sink);
            if(!j.cancel){
                j.move=result.best;j.score=-result.score;
                if(!result.candidates.empty()){
                    auto choice=chooseAdaptive(j.pos,result.candidates,int(j.ply),j.state,
                        double(esp_random())/4294967296.0,double(esp_random())/4294967296.0);
                    j.move=result.candidates[choice.index].move;j.score=choice.afterEval;
                }
                j.ok=j.move!=0;
                Serial.printf("[XMAS SEARCH] bestmove=%s depth=%d candidates=%u nodes=%llu ms=%lld humanCp=%d stackFree=%u\r\n",
                    move_to_uci(j.move).c_str(),result.depth,unsigned(result.candidates.size()),
                    (unsigned long long)result.nodes,(long long)result.timeMs,j.score,unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            }
            }
        }
    }catch(const std::bad_alloc&){Serial.println("[XMAS] search allocation failed");}
    TT.release();releaseSessionSearch();searching=nullptr;
    j.done.store(true,std::memory_order_release);vTaskDelete(nullptr);
}
bool beginSearch(){
    if(!memoryReady())return false;
    job.reset(new(std::nothrow) Job(*controller.game()));if(!job)return false;
    if(xTaskCreate(worker,"xmas_virgin",stackBytes,job.get(),1,nullptr)!=pdTRUE){job.reset();return false;}
    return true;
}
void diagnosticInput(bool allowed,uint32_t now){
    for(unsigned budget=0;budget<48 && Serial.available() && (diagnosticReading || Serial.peek()=='?');++budget){
        char c=char(Serial.read());diagnosticReading=true;
        if(c=='\n'){
            diagnosticLine[diagnosticLength]=0;
            if(!strcmp(diagnosticLine,"?xmas status")){
                Serial.printf("[XMAS STATUS] phase=%u own=%d job=%d heap=%u largest=%u\r\n",unsigned(controller.phase()),int(own.load()),int(bool(job)),unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
            }else if(allowed && !controller.active() && !job &&
                     (!strcmp(diagnosticLine,"?xmas test white") || !strcmp(diagnosticLine,"?xmas test black") || !strcmp(diagnosticLine,"?xmas test cancel"))){
                diagnostic=strstr(diagnosticLine,"black")?2:strstr(diagnosticLine,"cancel")?3:1;
                diagnosticBoard=xmas::startBoard();diagnosticBoard[29]=diagnostic==2?'q':'Q';
                diagnosticAt=now;diagnosticPlies=0;
                Serial.printf("[XMAS TEST] started mode=%u; synthetic sensors, no board output\r\n",diagnostic);
            }else Serial.println("[XMAS TEST] command rejected (requires idle standalone, no real board/host)");
            diagnosticLength=0;diagnosticReading=false;
        }else if(c!='\r' && diagnosticLength+1<sizeof(diagnosticLine))diagnosticLine[diagnosticLength++]=c;
    }
}
void show(uint64_t mask,bool evaluation){
    uint8_t squares[64];SquareHighlight highlights[64];size_t n=0;
    for(int i=0;i<64;++i)if(mask&(1ULL<<i)){squares[n]=uint8_t(i);highlights[n++]={uint8_t(i),SquareHighlightRole::Generic};}
    switch(currentBoardType()){
    case BoardType::Millennium:{
        uint8_t frame[167];encodeLedFrame(squares,n,frame,false);
        if(evaluation){
            // Native 9x9 corner grid: bottom border only. D1/E1 share
            // their middle corner, hence three distinct physical LEDs.
            for(int i=3;i<165;++i)frame[i]='0';
            for(size_t i=0;i<n;++i)for(int corner:{7-int(squares[i]%8),8-int(squares[i]%8)}){
                frame[3+2*(corner*9)]='F';frame[4+2*(corner*9)]='F';
            }
            computeModeBChecksumHex(frame+165,frame,165,false);
        }
        millenniumRelayLedFrame(frame,167);break;
    }
    case BoardType::Chessnut:chessnutSetHighlightedSquares(highlights,n);break;
    case BoardType::ChessnutMove:chessnutmoveSetHighlightedSquares(highlights,n);break;
    case BoardType::Certabo:certaboSetHighlightedSquares(highlights,n);break;
    case BoardType::IChessOne:ichessoneSetHighlightedSquares(highlights,n);break;
    case BoardType::Cynus:{
        if(evaluation){char text[8];int cp=controller.game()?evaluate(controller.game()->pos):0;
            if(controller.game() && controller.game()->pos.sideToMove()!=controller.game()->human)cp=-cp;
            snprintf(text,sizeof(text),"%+.1f",cp/100.0);cynusShowText(text);
        }else if(controller.phase()==xmas::Phase::Signal)cynusShowText(mask==xmas::centre?"XMas":mask?"Gift":"");
        else if(!controller.game() || !controller.game()->pending)cynusShowText("");
        break;
    }
    default:break;
    }
}
}

void xmasStatus(const uint8_t frame[67]){
    xmas::Board b;for(int sq=0;sq<64;++sq)b[sq]=char(frame[1+modeBStatusWireIndex(sq%8,sq/8+1)]&127);
    portENTER_CRITICAL(&inputMux);latest=b;haveStatus=true;portEXIT_CRITICAL(&inputMux);
}
bool xmasOwnsBoard(){return own.load(std::memory_order_acquire);}
bool xmasLedOverride(uint64_t& mask){if(!xmasOwnsBoard())return false;mask=wanted;return true;}
bool xmasPoll(bool standalone,bool masqueradeReady,bool online,bool external,uint32_t now){
    diagnosticInput(standalone && !online && !external,now);
    if(diagnostic && (online || external || !standalone)){
        diagnostic=0;controller.cancel();if(job)job->cancel=true;
        Serial.println("[XMAS TEST] interrupted by real board/host");
    }
    const bool wasOwner=xmasOwnsBoard();
    xmas::Board b;bool have;
    portENTER_CRITICAL(&inputMux);b=latest;have=haveStatus;
    if(!online)haveStatus=false;
    portEXIT_CRITICAL(&inputMux);
    if(diagnostic){b=diagnosticBoard;have=true;online=true;masqueradeReady=true;}
    if(!standalone || external || !online || !masqueradeReady){
        controller.cancel();if(job)job->cancel=true;
    }
    if(job && job->done.load(std::memory_order_acquire)){
        if(controller.game() && !job->cancel){
            if(job->ok && controller.game()->engineMove(job->move)){
                adaptive=std::move(job->state);
            }else controller.failed();
        }
        job.reset();
    }
    if(standalone && masqueradeReady && !external && online && have){
        try{
            if(!initialized && xmas::gesture(b)>=0)initTables();
            if(controller.observe(b,now,bool(job))){
                int score=evaluate(controller.game()->pos);
                if(controller.game()->pos.sideToMove()!=controller.game()->human)score=-score;
                controller.evaluation(score,now);commanded=0;
                Serial.printf("[XMAS MOVE] ply=%u humanCp=%d\r\n",controller.game()->ply,score);
            }
            if(controller.phase()==xmas::Phase::Loading){
                initTables();adaptive=AdaptiveState{};commanded=0;
                if(memoryReady()){
                    controller.prepared(now);
                    Serial.printf("[XMAS] Virgin Adaptive ready; human=%s; centre x2, corners x2\r\n",
                        controller.game()->human==WHITE?"white":"black");
                }else{Serial.println("[XMAS] insufficient search reserve; restore starting position");controller.failed();}
            }
            controller.tick(now);
            if(controller.needsEngine(now) && !job && !beginSearch())controller.failed();
            // Robot commands are exact, once per confirmed legal engine move.
            if(!diagnostic && controller.game() && controller.game()->pending && commanded!=controller.game()->pending){
                auto* g=controller.game();const auto target=g->expected();bool ok=true;
                if(currentBoardType()==BoardType::ChessnutMove)ok=chessnutmoveSetTargetPosition(target.data());
                else if(currentBoardType()==BoardType::Cynus){
                    const std::string uci=move_to_uci(g->pending);
                    extern bool cynusXmasMove(const std::string&);
                    ok=cynusXmasMove(uci);
                }
                commanded=g->pending;if(!ok){Serial.println("[XMAS] robot write failed; no retry");controller.failed();}
            }
        }catch(const std::bad_alloc&){controller.failed();Serial.println("[XMAS] allocation failed; restore starting position");}
    }
    if(!controller.game() && job)job->cancel=true;
    const bool owner=controller.active() || bool(job);
    own.store(owner,std::memory_order_release);
    wanted=controller.leds(now);
    if(!diagnostic && online && (owner || wasOwner)){
        bool evaluation=controller.showingEvaluation(now) && controller.game() && controller.physical()==xmas::diagram(controller.game()->pos);
        if(wanted!=lastMask || uint32_t(now-lastLedAt)>=200){show(wanted,evaluation);lastMask=wanted;lastLedAt=now;}
    }
    if(wasOwner && !owner){
        adaptive=AdaptiveState{};commanded=0;lastMask=~uint64_t(0);
        Serial.printf("[XMAS] ended; gesture required for new game; heap=%u\r\n",unsigned(ESP.getFreeHeap()));
    }
    if(diagnostic){
        if(controller.phase()==xmas::Phase::ReturnSetup){
            Serial.println("[XMAS TEST] FAIL: engine initialization/search failed");
            diagnostic=0;controller.cancel();if(job)job->cancel=true;
        }else if(controller.phase()==xmas::Phase::Signal || controller.phase()==xmas::Phase::RemoveQueen){
            diagnosticBoard=xmas::startBoard();
        }else if(controller.game() && controller.phase()==xmas::Phase::Playing){
            auto* g=controller.game();diagnosticPlies=std::max(diagnosticPlies,g->ply);
            if(g->ply>=4 || (diagnostic==3 && job && uint32_t(now-diagnosticAt)>4500))diagnosticBoard=xmas::startBoard();
            else if(g->pending)diagnosticBoard=g->expected();
            else if(g->pos.sideToMove()==g->human && !g->ended){
                MoveList moves;generateLegal(g->pos,moves);
                if(moves.count){Position next=g->pos;next.applyMove(moves[0]);diagnosticBoard=xmas::diagram(next);}
            }
        }else if(!owner && uint32_t(now-diagnosticAt)>2000){
            Serial.printf("[XMAS TEST] PASS mode=%u plies=%u heap=%u\r\n",diagnostic,diagnosticPlies,unsigned(ESP.getFreeHeap()));
            diagnostic=0;
        }
    }
    return owner;
}
