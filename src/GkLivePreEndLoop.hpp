#pragma once
#include <cmath>
#include <cstdint>
namespace GakuMotion::live_pre_end {
    inline bool ValidLead(float value) {return std::isfinite(value)&&value>=0.5f&&value<=3.0f;}
    struct Sample {
        uintptr_t owner{};int scene{};
        bool enabled{},confirmed{},paused{},blocked{};
        float time{},duration{},lead{1.0f};double now{};
    };
    enum class Action { None, SeekStart, Confirmed, Failed };
    class Loop {
        uintptr_t owner_{};int scene_{};bool waiting_{};double sent_{};float duration_{};
        uint64_t cycles_{};
    public:
        void Reset(){owner_=0;scene_=0;waiting_=false;cycles_=0;}
        bool Waiting()const{return waiting_;}
        uint64_t Cycles()const{return cycles_;}
        Action Step(const Sample& s){
            if(!s.enabled||!s.owner||!s.scene||!s.confirmed){Reset();return Action::None;}
            if(owner_!=s.owner||scene_!=s.scene){Reset();owner_=s.owner;scene_=s.scene;}
            if(!std::isfinite(s.now)||!std::isfinite(s.time)||!std::isfinite(s.duration)||!ValidLead(s.lead)||
                s.time<0||s.duration<=s.lead+1.0f||s.time>s.duration){waiting_=false;return Action::Failed;}
            if(s.blocked||s.paused){waiting_=false;return Action::None;}
            if(waiting_){
                if(s.duration!=duration_||s.now<sent_){waiting_=false;return Action::Failed;}
                if(s.time<=0.75f){waiting_=false;++cycles_;return Action::Confirmed;}
                if(s.now-sent_>=3.0){waiting_=false;return Action::Failed;}
                return Action::None;
            }
            if(s.time>=s.duration-s.lead){waiting_=true;sent_=s.now;duration_=s.duration;return Action::SeekStart;}
            return Action::None;
        }
    };
}
