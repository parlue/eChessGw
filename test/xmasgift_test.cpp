#ifdef NDEBUG
#error Assertions must be enabled
#endif
#include "../src/xmasgift/xmas_game.h"
#include "../src/xmasgift/virgin/eval.h"
#include "../src/xmasgift/virgin/search.h"
#include "../src/xmasgift/virgin/weights.h"
#include "../src/xmasgift/virgin/tt.h"
#include "../src/xmasgift/virgin/adaptive.h"
#include "../src/xmasgift/virgin/opening_book.h"
#include "../src/xmasgift/virgin/version.h"
#include <cassert>
#include <iostream>
using namespace xmas;
void ready(Controller& c,bool black){
    auto board=startBoard();board[29]=black?'q':'Q';
    c.observe(board,0,false);c.observe(board,599,false);assert(!c.active());
    c.observe(board,600,false);assert(c.phase()==Phase::Loading);
    assert(c.game()->human==(black?BLACK:WHITE));
    c.prepared(600);
    const uint64_t expected[]={centre,0,centre,0,corners,0,corners,0};
    for(unsigned i=0;i<8;++i){
        if(i)c.tick(600+250*i);
        assert(c.leds(600+250*i)==expected[i]);
        c.observe(startBoard(),601+250*i,false); // Queen removed during flash.
    }
    c.tick(2600);assert(c.phase()==Phase::RemoveQueen);
    c.observe(startBoard(),2601,false);assert(c.phase()==Phase::Playing);
    for(uint32_t t=2700;t<4000;t+=100)c.observe(startBoard(),t,black);
    assert(c.active() && c.game()->ply==0 && c.needsEngine(4000)==black);
}
void humanMove(Controller& c,const char* text,uint32_t now){
    Move move=find_legal_move(c.game()->pos,text);assert(move);
    auto p=c.game()->pos;p.applyMove(move);
    c.observe(diagram(p),now,false);assert(c.observe(diagram(p),now+100,false));
}
int main(){
    init_bitboards();init_zobrist();init_weights();initSearch();
    assert(std::string(VIRGIN_VERSION)=="0.8.7");
    Position bookRoot;bookRoot.setFen(startFen);
    const auto bookMoves=OpeningBook::moves(bookRoot);assert(bookMoves.size()>1);
    assert(OpeningBook::choose(bookRoot,0)==bookMoves.front());
    assert(OpeningBook::choose(bookRoot,1)==bookMoves.back());
    for(auto move:bookMoves)assert(find_legal_move(bookRoot,move_to_uci(move))==move);
    // Follow each encoded line, checking legal book choices through its end.
    for(size_t offset=0;offset<sizeof(OpeningBook::data)/sizeof(OpeningBook::data[0]);){
        Position line;line.setFen(startFen);
        while(OpeningBook::data[offset]){
            auto move=OpeningBook::data[offset++];
            const auto options=OpeningBook::moves(line);
            assert(std::find(options.begin(),options.end(),move)!=options.end());
            assert(find_legal_move(line,move_to_uci(move))==move);line.applyMove(move);
        }
        ++offset;assert(OpeningBook::choose(line,0.5)==0);
    }
    assert(adaptiveRegressionTests(std::cout));
    auto b=startBoard();assert(gesture(b)==-1);
    b[29]='Q';assert(gesture(b)==0);b[29]='q';assert(gesture(b)==1);
    b[0]='.';assert(gesture(b)==-1);b=startBoard();b[28]='Q';assert(gesture(b)==-1);
    b=startBoard();b[29]='Q';b[3]='.';assert(gesture(b)==-1);
    assert(evaluationMask(0)==24 && evaluationMask(99)==24 && evaluationMask(-99)==24);
    assert(evaluationMask(-100)==(1ULL<<2) && evaluationMask(100)==(1ULL<<5));
    assert(evaluationMask(-200)==2 && evaluationMask(200)==64);
    assert(evaluationMask(-99999)==1 && evaluationMask(99999)==128);
    for(bool black:{false,true}){
        Controller c;ready(c,black);
        if(black){
            auto m=find_legal_move(c.game()->pos,"e2e4");assert(c.game()->engineMove(m));
            assert(!c.needsEngine(4001));auto target=c.game()->expected();
            c.observe(startBoard(),4100,true);assert(c.active()); // Still waiting for execution.
            c.observe(target,4200,false);assert(c.observe(target,4300,false));assert(c.game()->ply==1);
            humanMove(c,"e7e5",4500);assert(c.game()->ply==2);
        }else{
            auto lift=startBoard();lift[12]='.';
            c.observe(lift,4100,false);c.observe(lift,4300,false);
            c.observe(startBoard(),4400,false);c.observe(startBoard(),5000,false);
            assert(c.active() && c.game()->ply==0); // Lift/replace is not an abort.
            humanMove(c,"e2e4",5100);assert(c.game()->ply==1);
        }
        c.evaluation(200,6000);assert(c.showingEvaluation(6999));assert(!c.showingEvaluation(7000));
        assert(!c.needsEngine(6200));assert(c.leds(6200)==64);
        auto lift=c.physical();lift[28]='.';c.observe(lift,6300,false);
        assert(c.leds(6300)!=64); // Correction takes priority over evaluation.
        c.observe(startBoard(),8000,true);c.observe(startBoard(),8399,true);assert(c.active());
        c.observe(startBoard(),8400,true);assert(!c.active() && !c.game());
        c.observe(startBoard(),9999,false);assert(!c.active()); // No auto-restart.
        auto trigger=startBoard();trigger[29]=black?'Q':'q';
        c.observe(trigger,10000,false);c.observe(trigger,10600,false);assert(c.phase()==Phase::Loading);
        c.failed();assert(c.phase()==Phase::ReturnSetup);
        c.observe(startBoard(),11000,false);c.observe(startBoard(),11400,false);assert(!c.active());
    }
    // Real rules: human underpromotion, castling, EP and pending exact target.
    Game g(WHITE);g.pos.setFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    auto p=g.pos;auto m=find_legal_move(p,"a7a8n");assert(m);p.applyMove(m);
    assert(g.observe(diagram(p),false) && g.pos.pieceOn(SQ_A8)==make_piece(WHITE,KNIGHT));
    Game castle(WHITE);castle.pos.setFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    p=castle.pos;m=find_legal_move(p,"e1g1");p.applyMove(m);assert(castle.observe(diagram(p),false));
    assert(castle.pos.pieceOn(SQ_F1)==make_piece(WHITE,ROOK));
    Game ep(WHITE);ep.pos.setFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
    p=ep.pos;m=find_legal_move(p,"e5d6");p.applyMove(m);assert(ep.observe(diagram(p),false));
    assert(ep.pos.pieceOn(SQ_D5)==NO_PIECE);
    // Finished games show the winning COLOUR, independent of human side,
    // and hold the result until the whole starting position is restored.
    struct Ending { const char* fen; const char* move; Result result; uint64_t mask; };
    const Ending endings[]={
        {"7k/8/5KQ1/8/8/8/8/8 w - - 0 1","g6g7",Result::WhiteWins,(1ULL<<28)|(1ULL<<35)},
        {"8/8/8/8/8/5kq1/8/7K b - - 0 1","g3g2",Result::BlackWins,(1ULL<<27)|(1ULL<<36)},
        {"7k/5K2/8/6Q1/8/8/8/8 w - - 0 1","g5g6",Result::Draw,centre},
        {"7k/8/8/8/8/8/R7/K7 w - - 99 1","a2a3",Result::Draw,centre},
        {"7k/8/8/8/8/8/8/KB6 w - - 0 1","b1c2",Result::Draw,centre}
    };
    for(const auto& ending:endings)for(bool humanLast:{false,true}){
        Controller c;ready(c,false);
        auto* game=c.game();game->pos.setFen(ending.fen);
        game->human=humanLast?game->pos.sideToMove():~game->pos.sideToMove();
        game->history.clear();game->history.push_back(game->pos.key());
        auto move=find_legal_move(game->pos,ending.move);assert(move);
        auto target=game->pos;target.applyMove(move);
        if(!humanLast){assert(game->engineMove(move));assert(!game->ended);}
        c.observe(diagram(target),5000,false);assert(c.observe(diagram(target),5100,false));
        assert(game->ended && game->result==ending.result);
        c.evaluation(-500,5100);
        assert(!c.showingEvaluation(5200) && !c.needsEngine(5200));
        assert(c.leds(5200)==ending.mask && c.leds(100000)==ending.mask);
        auto rebuilding=startBoard();rebuilding[0]='.';
        c.observe(rebuilding,100100,false);c.observe(rebuilding,101000,false);
        assert(c.active() && c.leds(101000)==ending.mask);
        c.observe(startBoard(),102000,false);c.observe(startBoard(),102399,false);
        assert(c.active() && c.leds(102399)==ending.mask);
        c.observe(startBoard(),102400,false);assert(!c.active() && c.leds(102400)==0);
        c.observe(startBoard(),103000,false);assert(!c.active());
    }
    assert(allocateSessionSearch());TT.resizeKiB(16);
    assert(searchRegressionTests(std::cout));
    TT.release();releaseSessionSearch();
    std::cout<<"PASS: F4 both colors, exact 2+2 flash sequence, opening protection, correction, score, abort, restart, promotion, castle, en-passant\n";
}
