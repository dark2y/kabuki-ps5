// SPDX-License-Identifier: GPL-3.0-or-later
// Player state and navigation for the native ShellUI widgets. No rendering dependencies.
#pragma once
#include "browse.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace overlay {
// The System tab's readings; a negative or empty value is shown as unavailable.
struct SystemStats {
    float cpu=-1;                       // percent busy, all cores
    float cpu_temp=-1,soc_temp=-1;      // degrees C
    float mem_used_gb=-1,mem_total_gb=-1;
    float fps=-1,fps_min=-1;            // frames a second: average and lowest over the last few seconds
    int uptime_s=-1;
    std::string firmware,address,librespot;
};
// Kabuki's settings: the shortcut that opens the panel (pad bits held together) and how long it is
// held, and whether the interface plays sounds. Kept by the parent in settings.json.
constexpr int kSlots=7; // sidebar tiles at most
constexpr int kTileKinds=5; // Now Playing, Queue, Library, System, Settings (the card each opens)
constexpr const char* kTileCodes="nqlys"; // as settings.json keeps them; '.' an empty slot
struct Settings {
    std::uint32_t chord=6; // L3 + R3
    int hold_ms=500;
    bool sounds=true;
    int tiles[kSlots]={0,1,2,3,4,-1,-1};
    // The tiles shown, in order (empty slots skipped), and a tile's place among them (-1: not shown).
    int shown(int* out) const { int n=0; for(int t:tiles) if(t>=0) out[n++]=t; return n; }
    int place(int tile) const { int n=0; for(int t:tiles) if(t>=0) { if(t==tile) return n; ++n; } return -1; }
    std::string codes() const { std::string c; for(int t:tiles) c+=t>=0&&t<kTileKinds?kTileCodes[t]:'.'; return c; }
    void from_codes(const std::string& c) {
        int read[kSlots]; bool settings=false;
        for(int i=0;i<kSlots;++i) {
            const char* at=i<int(c.size())?std::strchr(kTileCodes,c[i]):nullptr;
            read[i]=at&&*at?int(at-kTileCodes):-1;
            settings|=read[i]==4;
        }
        if(settings) std::copy(read,read+kSlots,tiles); // Settings can never be taken off
    }
};
constexpr int kHoldChoices[]={250,500,750,1000,1500,2000};
inline std::string chord_name(std::uint32_t chord) {
    struct Name { std::uint32_t bit; const char* name; };
    static constexpr Name names[]={{0x400,"L1"},{0x100,"L2"},{0x2,"L3"},{0x800,"R1"},{0x200,"R2"},{0x4,"R3"},
        {0x10,"Up"},{0x40,"Down"},{0x80,"Left"},{0x20,"Right"},{0x1000,"Triangle"},{0x2000,"Circle"},
        {0x4000,"Cross"},{0x8000,"Square"},{0x8,"Options"},{0x100000,"Touchpad"}};
    std::string text;
    for(const auto& n:names) if(chord&n.bit) { if(!text.empty()) text+=" + "; text+=n.name; }
    return text.empty()?"None":text;
}
struct NativeView {
    PlayerState state;
    Browse browse;
    int tab=0, rail=0, control=2;
    bool focused=false, seek=false;
    int seek_ms=-1, popup=0, option=0;
    int presses=0; // Cross presses on the focused item, for the press effect
    int seek_sent=-1; // a seek sent and not yet heard there: the bar and clock hold it
    SystemStats system;
    std::uint64_t opening=0; // the opening this view belongs to
    Settings settings;
    int setting=0;            // the settings row with the cursor
    bool recording=false;     // waiting for the new shortcut
    std::uint32_t recorded=0; // buttons held so far while recording
    int page=0;               // settings: 0 the main list, 1 the sidebar's tiles
    int slot=0;               // the sidebar slot with the cursor
    bool moving=false;        // that slot's tile is being moved
    bool picking=false;       // the tile list for that slot is open
    int pick=0;               // its highlighted entry (0-4 a tile, 5 none)
    bool nested=false;        // in a menu inside the card: Circle steps back instead of closing
    bool choosing=false;      // the hold-time list is open
    int choice=0;             // its highlighted entry
};

// The console's own UI sounds (PUI SystemSoundType), as the control centre plays them.
namespace sound {
enum : int { focus_move=0, back=1, decide=2, list_decide=8, list_move=9, menu_open=10, popup_open=12,
             option_close=19, slider=21, switch_on=22, switch_off=23, tab_enter=28, cursor_end=30,
             menu_close=100 /* the control centre's close */, play_pause=101, skip=102 };
}
class NativeModel {
    Player player;
    BrowseControl browsing;
    ListRepeat repeat;
    std::function<void(int,int,const char*)> send;
    struct Heard { std::uint64_t at; int position; bool moving; };
    std::vector<Heard> points;
    std::uint64_t pending_until=0, last_read=0, last_lists=0, opening=0;
    std::uint64_t seek_deadline=0,seek_held_since=0;
    bool loading=false;
    std::uint32_t suppress=0;
    std::string last_state,last_library,last_tracks,last_system;
    std::uint64_t last_stats=0;
    std::string dir="/user/data/custom-overlays"; // where the parent writes librespot's lines
    int tab=0,rail=0,control=2,popup=0,option=0,seek_ms=-1,presses=0,sent_ms=-1;
    std::uint64_t sent_at=0;
    // The queue as it was when a song in it was chosen: shown until Now Playing takes over,
    // so the list does not change under the buffering row.
    std::vector<QueueItem> frozen;
    std::vector<int> sounds; // played by the console's UI thread, in order
    int volume_asked=-1; std::uint64_t volume_at=0; // shown until librespot reports, or 3 s
    bool freezing=false;
    bool focused=false,seek=false;
    SystemStats stats;

    static std::string read(const char* path) {
        std::string text;
        if(FILE* f=std::fopen(path,"rb")) {
            char bytes[4096]; std::size_t n;
            while((n=std::fread(bytes,1,sizeof bytes,f))>0 && text.size()<262144) text.append(bytes,n);
            std::fclose(f);
        }
        return text;
    }
    void command(int kind,int value=0) { send(kind,value,nullptr); }
    // A card the cursor can go into: Now Playing always, a list only with rows, System never.
    bool enterable(int t) const {
        const auto& b=player.browse;
        if(t==0||t==4) return true;
        if(t==1) return !(freezing?frozen:player.state.queue).empty();
        // An open playlist keeps the cursor while its songs load.
        if(t==2) return b.library_level==1?(!b.tracks_known||!b.tracks.empty()):(b.library_known&&!b.library.empty());
        return false;
    }
    // Still loading when it was chosen: the cursor goes in once there are rows, if it is still there.
    bool loading_list(int t) const {
        return (t==1&&!player.state.queue_known)||(t==2&&player.browse.library_level==0&&!player.browse.library_known);
    }
    bool enter_when_loaded=false;
    void sound(int type) { if(settings.sounds&&sounds.size()<16) sounds.push_back(type); }
    Settings settings;
    int setting=0,choice=0,page=0,slot=0,pick=0;
    bool recording=false,choosing=false,moving=false,picking=false;
    int tiles_before[kSlots]={}; // the order before a move: put back if the move is not placed
    void unmove() { if(moving) std::copy(tiles_before,tiles_before+kSlots,settings.tiles); moving=false; }
    std::uint32_t recorded=0;
    // Settings are written by the parent (a command carries them): ShellUI only reads them.
    void save_settings() {
        char text[96];
        std::snprintf(text,sizeof text,"chord=%u hold=%d sounds=%d tiles=%s",settings.chord,settings.hold_ms,settings.sounds?1:0,
                      settings.codes().c_str());
        send(12,0,text);
    }
    void commit_seek(std::uint64_t now) {
        if(seek_ms>=0) { command(7,seek_ms); sent_ms=seek_ms; sent_at=now; }
        seek_ms=-1;
    }
public:
    explicit NativeModel(std::function<void(int,int,const char*)> command)
        : browsing(player,command),send(std::move(command)) {
        player.state.title="Connecting to librespot-ps5";
        load_settings();
    }
    void data_dir(std::string path) { dir=std::move(path); load_settings(); } // the PC preview's copy
    void load_settings() {
        const auto j=nlohmann::json::parse(read((dir+"/settings.json").c_str()),nullptr,false);
        if(!j.is_object()) return;
        settings.chord=j.value("chord",settings.chord); settings.hold_ms=j.value("hold_ms",settings.hold_ms);
        settings.sounds=j.value("sounds",settings.sounds);
        settings.from_codes(j.value("tiles",settings.codes()));
        if(__builtin_popcount(settings.chord)<2) settings.chord=6;
        settings.hold_ms=std::clamp(settings.hold_ms,100,5000);
    }
    const Settings& current_settings() const { return settings; }
    void system(const SystemStats& s) { stats=s; }
    std::vector<int> take_sounds() { std::vector<int> out; out.swap(sounds); return out; }
    // Used by the file-reading thread and by host-side checks.
    void apply_state(const std::string& text) {
        const auto j=nlohmann::json::parse(text,nullptr,false);
        if(j.is_discarded()||!j.is_object()) return;
        if(!j.value("link",false)) {
            player.state=PlayerState{};
            player.state.title="librespot-ps5 isn't running";
            player.state.artist="Start librespot-ps5.elf, then reopen this";
            points.clear(); loading=false; pending_until=0; return;
        }
        if(!j.contains("msg")||!j["msg"].contains("state")||!j["msg"]["state"].is_object()) return;
        const auto& st=j["msg"]["state"];
        const auto at=j.value("at_ms",std::uint64_t(0));
        PlayerState next;
        next.title=st.value("title",""); next.artist=st.value("artist",""); next.album=st.value("album","");
        next.cover=st.value("cover",""); next.duration_ms=st.value("duration_ms",0);
        next.playing=st.value("playing",false); next.shuffle=st.value("shuffle",false);
        next.volume=st.value("volume",100);
        const auto mode=st.value("repeat","off");
        next.repeat=mode=="one"?Repeat::one:mode=="all"?Repeat::all:Repeat::off;
        std::vector<Heard> heard{{at,st.value("position_ms",0),st.value("moving",false)}};
        auto until=st.value("pending",false)?at:std::uint64_t(0);
        if(st.contains("changes")&&st["changes"].is_array()) for(const auto& c:st["changes"]) {
            if(!c.is_object()) continue;
            const auto when=at+c.value("in_ms",std::uint64_t(0));
            heard.push_back({when,c.value("position_ms",0),c.value("moving",false)});
            if(until) until=std::max(until,when);
        }
        std::stable_sort(heard.begin(),heard.end(),[](const Heard& a,const Heard& b) { return a.at<b.at; });
        next.queue_known=st.contains("queue")&&st["queue"].is_array();
        if(next.queue_known) for(const auto& q:st["queue"]) {
            if(!q.is_object()) continue;
            next.queue.push_back({q.value("title",""),q.value("artist",""),q.value("cover",""),
                                  q.value("uri",""),q.value("index",-1)});
            if(next.queue.size()>=20) break;
        }
        if(next.title.empty()) {
            next.title=st.value("connected",false)?"Nothing playing":"Waiting for Spotify";
            next.artist="Pick this console in the Spotify app";
        } else if(!st.value("streaming",true)) next.album="Play Librespot Streamer in Music to hear it";
        player.state=std::move(next); points=std::move(heard);
        pending_until=until; loading=st.value("loading",false);
    }
    void poll_files(std::uint64_t now) {
        if(now-last_read>=100) {
            last_read=now;
            const auto text=read((dir+"/player.json").c_str());
            if(text!=last_state) { try { apply_state(text); last_state=text; } catch(...) {} }
        }
        // The System tab's readings (the parent writes them once a second while the panel is open).
        if(tab==3&&now-last_stats>=500) {
            last_stats=now;
            const auto text=read((dir+"/system.json").c_str());
            if(text!=last_system) {
                last_system=text;
                const auto j=nlohmann::json::parse(text,nullptr,false);
                if(j.is_object()) {
                    SystemStats s;
                    s.cpu=j.value("cpu",-1.0f); s.cpu_temp=j.value("cpu_temp",-1.0f); s.soc_temp=j.value("soc_temp",-1.0f);
                    s.mem_used_gb=j.value("mem_used_gb",-1.0f); s.mem_total_gb=j.value("mem_total_gb",-1.0f);
                    s.fps=j.value("fps",-1.0f); s.fps_min=j.value("fps_min",-1.0f); s.uptime_s=j.value("uptime_s",-1);
                    s.firmware=j.value("firmware",""); s.address=j.value("address",""); s.librespot=j.value("librespot","");
                    stats=s;
                }
            }
        }
        if(now-last_lists>=250) {
            last_lists=now;
            const auto library=read((dir+"/library.json").c_str());
            const auto tracks=read((dir+"/tracks.json").c_str());
            try {
                if(library!=last_library&&browsing.apply_library(nlohmann::json::parse(library,nullptr,false))) last_library=library;
                if((tracks!=last_tracks||!player.browse.tracks_known)&&
                   browsing.apply_tracks(nlohmann::json::parse(tracks,nullptr,false))) last_tracks=tracks;
            } catch(...) {}
        }
    }
    void tick(std::uint64_t now,std::uint64_t session,std::uint32_t held,std::uint32_t pressed,bool back) {
        auto& s=player.state;
        const Heard* heard=points.empty()?nullptr:&points.front();
        for(const auto& p:points) if(p.at<=now) heard=&p;
        if(heard) {
            const int moved=heard->moving&&now>heard->at?int(std::min<std::uint64_t>(now-heard->at,86400000)):0;
            s.position_ms=s.duration_ms>0?std::clamp(heard->position+moved,0,s.duration_ms):std::max(0,heard->position+moved);
        }
        s.pending=loading||now<pending_until;
        browsing.clamp_selection();
        // A seek is held until it is heard from there (or librespot-ps5 never gets there).
        if(sent_ms>=0&&((now-sent_at>300&&!s.pending&&std::abs(s.position_ms-sent_ms)<2500)||now-sent_at>12000)) sent_ms=-1;
        if(session!=opening) {
            // Each opening starts on the sidebar's first tile (Now Playing: on play/pause).
            int first[kSlots]; settings.shown(first);
            opening=session; tab=rail=first[0]; control=2; popup=0; seek=false; seek_ms=-1;
            unmove(); recording=choosing=picking=false; recorded=0; page=0;
            focused=enterable(tab); enter_when_loaded=!focused&&loading_list(tab);
            if(tab==2) browsing.request_library(now);
            browsing.cancel(); repeat.reset(); suppress=held;
            pressed=0; back=false;
        }
        suppress&=held; held&=~suppress; pressed&=~suppress;
        if(pressed&0x4000) ++presses;
        if(back) {
            repeat.reset(); suppress=held; sound(sound::back);
            if(recording) { recording=false; recorded=0; }
            else if(choosing) choosing=false;
            else if(picking) picking=false;
            else if(moving) unmove();
            else if(tab==4&&page==1) page=0;
            else if(popup) popup=0;
            else if(seek&&seek_ms>=0) seek_ms=-1;
            else if(!browsing.back(tab)) { focused=false; seek=false; }
        } else if(!focused) {
            // With Now Playing up, Square, L1 and R1 still play/pause and skip from the rail.
            if(tab==0) {
                if(pressed&0x8000) { command(0); sound(sound::play_pause); }
                if(pressed&0x400) { command(1); sound(sound::skip); }
                if(pressed&0x800) { command(2); sound(sound::skip); }
            }
            int order[kSlots]; const int n=settings.shown(order); const int at=std::max(0,settings.place(rail));
            if(pressed&0x10) { sound(n>1?sound::focus_move:sound::cursor_end); rail=order[(at+n-1)%n]; }
            else if(pressed&0x40) { sound(n>1?sound::focus_move:sound::cursor_end); rail=order[(at+1)%n]; }
            else if(pressed&0x4000) {
                sound(sound::tab_enter);
                if(tab!=rail) browsing.cancel();
                tab=rail; focused=enterable(tab); suppress=held; repeat.reset(); // nothing to select: stay on the rail
                enter_when_loaded=!focused&&loading_list(tab);
                if(tab==0) { control=2; seek=false; popup=0; } // always onto play/pause
                if(tab==2) browsing.request_library(now);
            } else if((pressed&0x80)&&enterable(tab)) {
                focused=true; suppress=held; repeat.reset(); sound(sound::focus_move);
                if(tab==0) { control=2; seek=false; popup=0; } // always onto play/pause
            }
        } else if(tab==4) {
            settings_tick(held,pressed,now);
        } else if(tab!=0) {
            if(pressed&0x20) { focused=false; rail=tab; repeat.reset(); suppress=held; sound(sound::focus_move); }
            else {
                if(int delta=repeat.step(pressed,held,now)) {
                    const auto before=player.browse;
                    browsing.move(tab,delta);
                    const auto& b=player.browse;
                    const bool moved=before.queue_selected!=b.queue_selected||before.library_selected!=b.library_selected||
                                     before.track_selected!=b.track_selected;
                    sound(moved?sound::list_move:sound::cursor_end);
                }
                if(pressed&0x4000) {
                    sound(sound::list_decide);
                    browsing.choose(tab,now);
                    if(tab==1&&player.browse.starting_row>=0) { frozen=s.queue; freezing=true; }
                }
            }
        } else {
            if(pressed&0x8000) { command(0); sound(sound::play_pause); }
            if(pressed&0x400) { command(1); sound(sound::skip); }
            if(pressed&0x800) { command(2); sound(sound::skip); }
            if(popup==1) {
                // Left/Right move the slider (Up/Down too).
                const auto as_steps=[](std::uint32_t b) { return ((b&0x80)?0x10U:0U)|((b&0x20)?0x40U:0U)|((b&0x10)?0x40U:0U)|((b&0x40)?0x10U:0U); };
                if(int delta=repeat.step(as_steps(pressed),as_steps(held),now)) {
                    const int from=volume_asked>=0?volume_asked:s.volume;
                    const int volume=std::clamp(from+delta*5,0,100);
                    sound(volume!=from?sound::slider:sound::cursor_end);
                    if(volume!=from) { command(6,volume); volume_asked=volume; volume_at=now; }
                }
                if(pressed&0x4000) { popup=0; sound(sound::option_close); }
            } else if(popup==2) {
                if(pressed&0x10) { sound(option?sound::list_move:sound::cursor_end); option=0; }
                if(pressed&0x40) { sound(option?sound::cursor_end:sound::list_move); option=1; }
                if(pressed&0x4000) {
                    if(option==0) { command(3,!s.shuffle); sound(s.shuffle?sound::switch_off:sound::switch_on); }
                    else {
                        command(4,int(s.repeat==Repeat::off?Repeat::all:s.repeat==Repeat::all?Repeat::one:Repeat::off));
                        sound(sound::decide);
                    }
                }
            } else if(seek) {
                const auto horizontal=[](std::uint32_t b) { return ((b&0x80)?0x10U:0U)|((b&0x20)?0x40U:0U); };
                if(horizontal(pressed)) seek_held_since=now;
                if(int delta=repeat.step(horizontal(pressed),horizontal(held),now)) {
                    const auto duration=now-seek_held_since;
                    const int step=duration>=4000?60000:duration>=1500?15000:5000;
                    seek_ms=std::clamp((seek_ms<0?s.position_ms:seek_ms)+delta*step,0,std::max(0,s.duration_ms));
                    seek_deadline=now+700; sound(sound::slider);
                }
                if(seek_ms>=0&&now>=seek_deadline) commit_seek(now);
                if(pressed&0x4000) { commit_seek(now); sound(sound::decide); }
                if(pressed&0x40) { commit_seek(now); seek=false; repeat.reset(); sound(sound::focus_move); }
            } else {
                if(pressed&0x10) { seek=true; repeat.reset(); sound(sound::focus_move); }
                else if(pressed&0x40) { focused=false; rail=tab; suppress=held; repeat.reset(); sound(sound::focus_move); }
                else if(pressed&0x80) { sound(control>0?sound::focus_move:sound::cursor_end); control=std::max(0,control-1); }
                else if(pressed&0x20) {
                    sound(sound::focus_move);
                    if(control<4) ++control;
                    else { focused=false; rail=tab; suppress=held; }
                } else if(pressed&0x4000) {
                    if(control==0) { popup=1; sound(sound::popup_open); }
                    else if(control==1) { command(1); sound(sound::skip); }
                    else if(control==2) { command(0); sound(sound::play_pause); }
                    else if(control==3) { command(2); sound(sound::skip); }
                    else { popup=2; option=0; sound(sound::popup_open); }
                }
            }
        }
        if(browsing.heard(now)) { tab=rail=0; focused=true; control=2; seek=false; popup=0; suppress=held; repeat.reset(); }
        if(focused&&tab!=0&&!enterable(tab)) { focused=false; rail=tab; suppress=held; repeat.reset(); }
        if(enter_when_loaded&&(focused||rail!=tab||!loading_list(tab))) {
            if(!focused&&rail==tab&&enterable(tab)) { focused=true; repeat.reset(); }
            enter_when_loaded=false;
        }
        player.browse.focused=focused&&tab!=0;
        if(volume_asked>=0&&(volume_asked==s.volume||(now-volume_at>3000&&popup!=1))) volume_asked=-1;
        if(freezing&&player.browse.starting_row<0) freezing=false;
    }
    // Settings: Up/Down rows; Cross records the shortcut (hold the new buttons together, then let go:
    // two or more), opens the hold-time list, or switches sounds. Right goes back to the rail.
    void settings_tick(std::uint32_t held,std::uint32_t pressed,std::uint64_t now) {
        if(recording) {
            recorded|=held; // any buttons, Circle too: closing the panel abandons a recording
            if(!held&&recorded) {
                if(__builtin_popcount(recorded)>=2) { settings.chord=recorded; save_settings(); sound(sound::switch_on); }
                else sound(sound::cursor_end);
                recording=false; recorded=0;
            }
            return;
        }
        if(choosing) {
            constexpr int n=int(sizeof kHoldChoices/sizeof kHoldChoices[0]);
            if(int delta=repeat.step(pressed,held,now)) {
                const int next=std::clamp(choice+delta,0,n-1);
                sound(next!=choice?sound::list_move:sound::cursor_end); choice=next;
            }
            if(pressed&0x4000) { settings.hold_ms=kHoldChoices[choice]; choosing=false; save_settings(); sound(sound::option_close); }
            return;
        }
        if(page==1) { sidebar_tick(held,pressed,now); return; }
        if(pressed&0x20) { focused=false; rail=tab; repeat.reset(); suppress=held; sound(sound::focus_move); return; }
        if(int delta=repeat.step(pressed,held,now)) {
            const int next=std::clamp(setting+delta,0,3);
            sound(next!=setting?sound::list_move:sound::cursor_end); setting=next;
        }
        if(pressed&0x4000) {
            if(setting==0) { recording=true; recorded=0; suppress=held; sound(sound::popup_open); }
            else if(setting==1) {
                choosing=true; choice=0; sound(sound::popup_open);
                for(int i=0;i<int(sizeof kHoldChoices/sizeof kHoldChoices[0]);++i) if(kHoldChoices[i]==settings.hold_ms) choice=i;
            } else if(setting==2) {
                settings.sounds=!settings.sounds; save_settings();
                if(settings.sounds) sound(sound::switch_on);
            } else { page=1; slot=0; moving=picking=false; sound(sound::tab_enter); }
        }
    }
    // The sidebar's tiles: Up/Down a slot; Cross chooses its tile (one already placed swaps with
    // it; Settings stays); Square picks the tile up, Up/Down carry it, Square or Cross puts it down.
    void sidebar_tick(std::uint32_t held,std::uint32_t pressed,std::uint64_t now) {
        auto& t=settings.tiles;
        if(picking) {
            if(int delta=repeat.step(pressed,held,now)) {
                const int next=std::clamp(pick+delta,0,kTileKinds);
                sound(next!=pick?sound::list_move:sound::cursor_end); pick=next;
            }
            if(pressed&0x4000) {
                const int chosen=pick==kTileKinds?-1:pick;
                if(chosen==-1&&t[slot]==4) { sound(sound::cursor_end); return; }
                for(int i=0;i<kSlots;++i) if(i!=slot&&chosen>=0&&t[i]==chosen) t[i]=t[slot]; // swap
                t[slot]=chosen; picking=false; save_settings(); sound(sound::option_close);
            }
            return;
        }
        if(int delta=repeat.step(pressed,held,now)) {
            const int next=std::clamp(slot+delta,0,kSlots-1);
            if(next!=slot&&moving) std::swap(t[slot],t[next]);
            sound(next!=slot?(moving?sound::slider:sound::list_move):sound::cursor_end); slot=next;
        }
        if(pressed&0x8000) {
            if(!moving) std::copy(t,t+kSlots,tiles_before);
            moving=!moving; sound(moving?sound::popup_open:sound::option_close);
            if(!moving) save_settings();
        } else if(pressed&0x4000) {
            if(moving) { moving=false; save_settings(); sound(sound::option_close); }
            else if(t[slot]==4) sound(sound::cursor_end); // Settings is not swapped out from its own page
            else { picking=true; pick=t[slot]<0?kTileKinds:t[slot]; sound(sound::popup_open); }
        } else if(pressed&0x20) { focused=false; rail=tab; repeat.reset(); suppress=held; sound(sound::focus_move); }
    }
    NativeView view() const {
        NativeView v{player.state,player.browse,tab,rail,control,focused,seek,seek_ms,popup,option,presses,sent_ms,stats,
                     opening,settings,setting,recording,recorded,page,slot,moving,picking,pick,
                     focused&&(popup||(tab==2&&player.browse.library_level==1)||recording||choosing||(tab==4&&page==1)),
                     choosing,choice};
        if(freezing) v.state.queue=frozen;
        if(volume_asked>=0) v.state.volume=volume_asked;
        return v;
    }
};
} // namespace overlay
