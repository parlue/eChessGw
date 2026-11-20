#pragma once
#include "virgin/movegen.h"
#include "virgin/position.h"
#include <algorithm>
#include <array>
#include <memory>
#include <vector>

namespace xmas {
using Board = std::array<char,64>;
inline constexpr char initial[] = "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr";
inline constexpr char startFen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
inline constexpr uint64_t centre = (1ULL<<27)|(1ULL<<28)|(1ULL<<35)|(1ULL<<36);
inline constexpr uint64_t corners = 1ULL|(1ULL<<7)|(1ULL<<56)|(1ULL<<63);
inline Board diagram(const Position& p) {
    Board b{};constexpr char codes[]="PNBRQKpnbrqk";
    for(int s=0;s<64;++s){Piece v=p.pieceOn(Square(s));b[s]=v==NO_PIECE?'.':codes[int(v)];}
    return b;
}
inline Board startBoard(){Board b;std::copy_n(initial,64,b.begin());return b;}
inline bool isInitial(const Board& b){return std::equal(b.begin(),b.end(),initial);}
inline uint64_t diff(const Board& a,const Board& b){uint64_t v=0;for(int i=0;i<64;++i)if(a[i]!=b[i])v|=1ULL<<i;return v;}
// Scores are already in the HUMAN's perspective, in centipawns.
inline uint64_t evaluationMask(int cp) {
    if(cp>-100 && cp<100)return (1ULL<<3)|(1ULL<<4);
    const int step=std::min(3,std::abs(cp)/100);
    return 1ULL<<(cp<0?3-step:4+step);
}
inline int gesture(const Board& b) {
    Board copy=b;const char queen=copy[29];copy[29]='.'; // F4, a1-first
    return isInitial(copy)?(queen=='Q'?0:queen=='q'?1:-1):-1;
}
enum class Result { Ongoing, WhiteWins, BlackWins, Draw };
inline uint64_t resultMask(Result result) {
    return result==Result::WhiteWins ? (1ULL<<28)|(1ULL<<35) :
           result==Result::BlackWins ? (1ULL<<27)|(1ULL<<36) :
           result==Result::Draw ? centre : 0;
}
struct Game {
    Position pos;
    Color human;
    Move pending=0;
    unsigned ply=0;
    bool ended=false;
    Result result=Result::Ongoing;
    std::vector<Key> history;
    explicit Game(Color c):human(c){pos.setFen(startFen);history.push_back(pos.key());}
    Board expected()const{Position next=pos;if(pending)next.applyMove(pending);return diagram(next);}
    bool needsEngine()const{return !ended && !pending && pos.sideToMove()!=human;}
    void commit(Move m){
        pos.applyMove(m);pending=0;++ply;
        if(pos.halfmoveClock()==0)history.clear();history.push_back(pos.key());
        MoveList moves;generateLegal(pos,moves);
        result=Result::Ongoing;
        if(!moves.count)result=pos.inCheck() ?
            (pos.sideToMove()==WHITE?Result::BlackWins:Result::WhiteWins) : Result::Draw;
        else if(pos.halfmoveClock()>=100 || std::count(history.begin(),history.end(),pos.key())>=3)result=Result::Draw;
        if(!(pos.pieces(PAWN)|pos.pieces(ROOK)|pos.pieces(QUEEN))){
            Bitboard b=pos.pieces(BISHOP),n=pos.pieces(KNIGHT);
            if(result==Result::Ongoing && (popcount(b|n)<=1 || (!n && (!(b&DarkSquares)||!(b&LightSquares)))))result=Result::Draw;
        }
        ended=result!=Result::Ongoing;
    }
    bool observe(const Board& b,bool searching){
        if(ended)return false;
        if(pending){if(b==expected()){commit(pending);return true;}return false;}
        if(searching || pos.sideToMove()!=human || b==diagram(pos))return false;
        MoveList moves;generateLegal(pos,moves);
        for(Move m:moves){Position p=pos;p.applyMove(m);if(diagram(p)==b){commit(m);return true;}}
        return false;
    }
    bool engineMove(Move m){
        if(!needsEngine())return false;
        MoveList legal;generateLegal(pos,legal);
        for(Move v:legal)if(v==m){pending=m;return true;}
        return false;
    }
};
enum class Phase{Idle,Loading,Signal,RemoveQueen,Playing,ReturnSetup};
class Controller {
    std::unique_ptr<Game> game_;
    Board physical_{};
    Phase phase_=Phase::Idle;
    uint32_t changedAt_=0, signalDue_=0, evalAt_=0;
    bool haveBoard_=false, eval_=false;
    unsigned signalStep_=0;
    int score_=0;
public:
    Phase phase()const{return phase_;}
    bool active()const{return phase_!=Phase::Idle;}
    Game* game(){return game_.get();}
    const Game* game()const{return game_.get();}
    const Board& physical()const{return physical_;}
    bool showingEvaluation(uint32_t now)const{return eval_ && game_ && !game_->ended && uint32_t(now-evalAt_)<1000;}
    bool needsEngine(uint32_t now)const{return phase_==Phase::Playing && game_->needsEngine() &&
        physical_==diagram(game_->pos) && !showingEvaluation(now);}
    void cancel(){game_.reset();phase_=Phase::Idle;eval_=false;haveBoard_=false;}
    void failed(){game_.reset();phase_=Phase::ReturnSetup;eval_=false;}
    void prepared(uint32_t now){if(phase_==Phase::Loading){phase_=Phase::Signal;signalStep_=0;signalDue_=now+250;}}
    // Pure timing/state logic. Called from the owner loop, including for boards
    // that notify only on change. Transport glue supplies only decoded snapshots.
    bool observe(const Board& b,uint32_t now,bool searching){
        if(!haveBoard_ || physical_!=b){physical_=b;changedAt_=now;haveBoard_=true;}
        const uint32_t stable=now-changedAt_;
        if(phase_==Phase::Idle){
            int trigger=gesture(b);
            if(trigger>=0 && stable>=600){game_.reset(new Game(trigger==0?WHITE:BLACK));phase_=Phase::Loading;}
        }else if(phase_==Phase::RemoveQueen){
            if(isInitial(b) && stable>=200)phase_=Phase::Playing;
        }else if(phase_==Phase::Playing){
            // Initial setup/duplicate scans never cancel Virgin's first move.
            if(game_->ply>0 && isInitial(b)){
                if(stable>=400)cancel();
                return false;
            }
            if(stable>=100)return game_->observe(b,searching);
        }else if(phase_==Phase::ReturnSetup && isInitial(b) && stable>=400)cancel();
        return false;
    }
    void evaluation(int humanScore,uint32_t now){score_=humanScore;evalAt_=now;eval_=true;}
    void tick(uint32_t now){
        // Never skip blink edges after a slow loop tick; keep each on/off visible.
        if(phase_==Phase::Signal && int32_t(now-signalDue_)>=0){
            if(++signalStep_==8)phase_=Phase::RemoveQueen;
            else signalDue_=now+250;
        }
    }
    uint64_t leds(uint32_t now)const{
        if(!active())return 0;
        if(phase_==Phase::Signal)return signalStep_%2?0:(signalStep_<4?centre:corners);
        if(phase_==Phase::Playing){
            // Hold the actual colour's result through piece lifting/resetting.
            // Only a confirmed complete starting position clears the game.
            if(game_->ended)return resultMask(game_->result);
            // A lifted/corrected piece always takes priority over a score.
            if(showingEvaluation(now) && physical_==diagram(game_->pos))return evaluationMask(score_);
            uint64_t mask=diff(physical_,game_->expected());
            if(game_->pending)mask|=(1ULL<<from_sq(game_->pending))|(1ULL<<to_sq(game_->pending));
            return mask;
        }
        if(phase_==Phase::Loading)return 0;
        return diff(physical_,startBoard());
    }
};
} // namespace xmas
