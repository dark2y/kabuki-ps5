// SPDX-License-Identifier: GPL-3.0-or-later
#include "browse.hpp"
#include <cassert>
#include <iostream>

using namespace overlay;
struct Sent { int kind, value; std::string text; };

int main() {
    ListRepeat repeat;
    assert(repeat.step(0x40, 0x40, 0) == 1);
    assert(repeat.step(0, 0x40, 399) == 0);
    assert(repeat.step(0, 0x40, 400) == 1);
    assert(repeat.step(0, 0x40, 509) == 0);
    assert(repeat.step(0, 0x40, 510) == 1);
    assert(repeat.step(0, 0, 520) == 0);
    assert(repeat.step(0, 0x40, 900) == 0); // focus transfer never repeats a held press
    assert(repeat.step(0x10, 0, 1000) == -1); // a short tap between frames still counts
    assert(repeat.step(0, 0, 1500) == 0);

    Player p;
    std::vector<Sent> commands;
    BrowseControl browser(p, [&](int kind, int value, const char* text) {
        commands.push_back({kind, value, text ? text : ""});
    });
    browser.request_library(100);
    browser.request_library(60100 - 1);
    assert(commands.size() == 1 && commands.back().kind == 9);
    browser.request_library(60100);
    assert(commands.size() == 2);
    assert(!browser.apply_library(nlohmann::json::parse("bad", nullptr, false)));
    assert(browser.apply_library(nlohmann::json::parse(R"({"library":{"items":[
        {"kind":"playlist","uri":"spotify:playlist:list","name":"My list"},
        {"kind":"album","uri":"spotify:album:album","name":"Album"}],"error":""}})")));
    browser.move(2, 50);
    assert(p.browse.library_selected == 1);
    browser.move(2, -50);
    assert(p.browse.library_selected == 0);
    browser.choose(2, 70000);
    assert(commands.back().kind == 10 && commands.back().text == "spotify:playlist:list");
    assert(p.browse.library_level == 1 && !p.browse.tracks_known);
    assert(!browser.apply_tracks(nlohmann::json::parse(R"({"tracks":{"uri":"spotify:album:old","items":[]}})")));
    assert(!p.browse.tracks_known);
    assert(browser.apply_tracks(nlohmann::json::parse(R"({"tracks":{"uri":"spotify:playlist:list","items":[
        {"uri":"spotify:track:song","title":"Song","artist":"Artist"}],"error":""}})")));
    p.state.title = "Song"; p.state.artist = "Artist"; p.state.playing = true;
    browser.choose(2, 71000);
    assert(commands.back().kind == 11 && commands.back().text == "spotify:playlist:list spotify:track:song");
    assert(p.browse.starting_row == 0 && !browser.heard(71001)); // stale matching state
    p.state.pending = true;
    assert(!browser.heard(71200));
    p.state.pending = false;
    assert(browser.heard(72000) && p.browse.starting_row == -1);

    p.state.queue = {{"Song", "Artist", "", "spotify:track:song", 7}};
    browser.choose(1, 80000);
    assert(commands.back().kind == 8 && commands.back().value == 7);
    assert(!browser.heard(82500));
    assert(browser.heard(82501)); // no pending state was observed
    browser.choose(1, 90000);
    p.state.artist = "Another Artist";
    assert(!browser.heard(93000));
    assert(!browser.heard(105000) && p.browse.starting_row == -1);
    browser.choose(1, 110000);
    browser.cancel();
    assert(!browser.heard(113000));
    assert(browser.back(2) && p.browse.library_level == 0);
    assert(!browser.back(2));
    p.state.queue.clear();
    browser.move(1, 10);
    assert(p.browse.queue_selected == 0);
    const auto count = commands.size();
    browser.choose(1, 120000);
    assert(commands.size() == count);
    assert(browser.apply_library(nlohmann::json::parse(R"({"library":{"items":[],"error":"Token denied"}})")));
    assert(p.browse.library_known && p.browse.library_error == "Token denied");
    std::cout << "Browse interaction checks passed\n";
}
