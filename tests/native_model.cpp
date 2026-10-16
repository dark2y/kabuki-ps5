// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_model.hpp"
#include <cassert>
#include <iostream>
using namespace overlay;
int main() {
    struct Sent { int kind,value; std::string text; };
    std::vector<Sent> sent;
    NativeModel model([&](int k,int v,const char* t) { sent.push_back({k,v,t?t:""}); });
    const auto state=[](bool loading,const std::string& title) {
        return nlohmann::json({{"at_ms",1000},{"link",true},{"msg",{{"state",{
            {"title",title},{"artist","Artist"},{"playing",true},{"loading",loading},
            {"position_ms",5000},{"duration_ms",180000},{"moving",true},
            {"queue",nlohmann::json::array({{{"title","Queued"},{"artist","Artist"},{"uri","spotify:track:queued"},{"index",7}}})}
        }}}}}).dump();
    };
    model.apply_state(state(false,"Current"));
    model.tick(1000,1,0,0,false);
    assert(model.view().tab==0&&model.view().focused&&model.view().control==2); // opens on play/pause
    model.tick(1005,1,0,0,true); // Circle to the rail
    model.tick(1010,1,0x40,0x40,false); // Queue on the rail
    model.tick(1020,1,0x4000,0x4000,false); // enter Queue
    assert(model.view().tab==1&&model.view().focused);
    model.tick(1030,1,0x4000,0,false); // held Cross must not play the first row
    assert(sent.empty());
    model.tick(1040,1,0,0,false);
    model.tick(1050,1,0x8000,0x8000,false); // no hidden Now Playing shortcut in a list
    assert(sent.empty());
    model.tick(1060,1,0x4000,0x4000,false);
    assert(sent.back().kind==8&&sent.back().value==7&&sent.back().text=="spotify:track:queued");
    assert(model.view().browse.starting_row==0);
    // librespot drops the chosen song from its queue at once; the list holds until Now Playing.
    model.apply_state(nlohmann::json({{"at_ms",1000},{"link",true},{"msg",{{"state",{{"title","Current"},{"artist","Artist"},
        {"playing",true},{"loading",true},{"position_ms",5000},{"duration_ms",180000},{"queue",nlohmann::json::array()}}}}}}).dump());
    model.tick(1070,1,0,0,false);
    assert(model.view().state.queue.size()==1);
    model.apply_state(state(true,"Queued")); model.tick(1100,1,0,0,false);
    assert(model.view().tab==1);
    model.apply_state(state(false,"Queued")); model.tick(1200,1,0,0,false);
    assert(model.view().tab==0&&model.view().focused&&model.view().control==2);
    model.tick(1300,1,0,0,true); // Circle goes to rail
    assert(!model.view().focused);
    model.tick(1400,1,0x4000,0x4000,false);
    model.tick(1500,1,0,0,false);
    model.tick(1600,1,0x10,0x10,false); // focus seek
    model.tick(1700,1,0x20,0x20,false);
    assert(model.view().seek_ms>5000);
    model.tick(1800,1,0x4000,0x4000,false);
    assert(sent.back().kind==7&&sent.back().value==10700);
    assert(model.view().seek_sent==10700); // held until heard there
    model.tick(1900,2,0x4000,0x4000,false); // a reopening suppresses a held Cross
    assert(model.view().focused&&model.view().tab==0&&model.view().control==2&&model.view().seek_ms==-1);
    assert(sent.back().kind==7);
    model.apply_state("{\"link\":false}"); model.tick(2000,2,0,0,false);
    assert(model.view().state.title=="librespot-ps5 isn't running"&&!model.view().state.playing);
    // Settings > Sidebar: Circle in the tile list closes only the list.
    std::uint64_t t=3000;
    const auto press=[&](std::uint64_t session,std::uint32_t bits,bool back=false) {
        model.tick(t+=50,session,bits,bits,back); model.tick(t+=50,session,0,0,false);
    };
    model.apply_state(state(false,"Current"));
    model.tick(t+=50,3,0,0,false); // open
    press(3,0,true); // to the rail
    for(int i=0;i<4;++i) press(3,0x40); // down to Settings
    press(3,0x4000);
    assert(model.view().tab==4&&model.view().focused);
    for(int i=0;i<3;++i) press(3,0x40);
    press(3,0x4000); // the Sidebar page
    assert(model.view().page==1&&model.view().nested);
    press(3,0x4000); // slot 1's tile list
    assert(model.view().picking&&model.view().nested);
    press(3,0,true);
    assert(!model.view().picking&&model.view().page==1&&model.view().focused);
    press(3,0x4000); press(3,0x40); press(3,0x4000); // Queue into the first slot
    assert(model.view().settings.tiles[0]==1&&model.view().settings.tiles[1]==0);
    // The next opening shows the first tile.
    model.tick(t+=50,4,0,0,false);
    assert(model.view().tab==1&&model.view().rail==1&&model.view().focused&&model.view().page==0);
    std::cout<<"Native ShellUI model checks passed\n";
}
