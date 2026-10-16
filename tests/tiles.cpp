// SPDX-License-Identifier: GPL-3.0-or-later
// Host checks: tiles and their order, settings, the librespot adapter, and the panel's model.
#include "librespot_adapter.hpp"
#include "native_model.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace kabuki;

int main() {
    // Records survive escaping.
    Records r; r.add({"row", "a\tb", "line\nbreak", "back\\slash"});
    const auto parsed = parse_records(r.text());
    assert(parsed.size() == 1 && parsed[0][1] == "a\tb" && parsed[0][2] == "line\nbreak" && parsed[0][3] == "back\\slash");
    assert(parse_records("nonsense\n").empty());

    // Without providers: System, Settings. With librespot: its three first, in its order.
    Settings s;
    assert((arrange(s, {}) == std::vector<std::string>{kSystemTile, kSettingsTile}));
    std::vector<TileInfo> lsp{{"LSPNOW", "librespot", Card::media, "Now Playing", "", 100, 1},
                              {"LSPQUE", "librespot", Card::list, "Queue", "", 100, 2},
                              {"LSPLIB", "librespot", Card::list, "Library", "", 100, 3}};
    assert((arrange(s, lsp) == std::vector<std::string>{"LSPNOW", "LSPQUE", "LSPLIB", kSystemTile, kSettingsTile}));

    // The old "nqlys.." settings, and offline tiles keeping their slots.
    assert(migrate_codes(s, "sqlyn.."));
    assert(s.slots[0] == kSettingsTile && s.slots[4] == "LSPNOW");
    assert((arrange(s, {}) == std::vector<std::string>{kSettingsTile, kSystemTile}));
    assert(arrange(s, lsp).size() == 5 && arrange(s, lsp)[4] == "LSPNOW");
    assert(!migrate_codes(s, "nqly..."));  // no Settings: refused

    // An arrangement without Settings never takes effect.
    auto bad = parse_settings("kabuki\t1\nslots\tLSPNOW\t\t\t\t\t\t\n");
    assert(!bad.arranged);

    // A new tile goes in before Kabuki's own, which move down.
    Settings a; a.arranged = true;
    std::string base[kSlots] = {"LSPNOW", kSystemTile, kSettingsTile, "", "", "", ""};
    for (int i = 0; i < kSlots; ++i) a.slots[i] = base[i];
    a.known = {"LSPNOW"};
    assert(learn_tiles(a, {{"HELLO1", "hello", Card::list, "Hello", "", 500, 4}}));
    assert(a.slots[1] == "HELLO1" && a.slots[2] == kSystemTile && a.slots[3] == kSettingsTile);

    // The daemon's service and adapter: tiles exist only while librespot answers.
    const auto dir = (std::filesystem::temp_directory_path() / "kabuki-tests").string();
    std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
    std::uint64_t now = 1000;
    Service service(dir, [&] { return now; });
    service.load_settings();
    std::vector<std::string> sent;
    LibrespotAdapter adapter(service, [&](const std::string& line) { sent.push_back(line); });
    assert(parse_tiles(read_file(dir + "/" + kTilesFile)).empty());
    adapter.connected();
    assert(parse_tiles(read_file(dir + "/" + kTilesFile)).size() == 3);
    adapter.line(R"({"v":1,"state":{"title":"Song","artist":"Artist","playing":true,"duration_ms":1000,"queue":[{"title":"Next","artist":"A","uri":"spotify:track:x","index":7}]}})");
    const auto np = parse_content(read_file(dir + "/" + content_file("LSPNOW")));
    assert(np.media && np.m.title == "Song" && np.m.playing);
    const auto q = parse_content(read_file(dir + "/" + content_file("LSPQUE")));
    assert(q.list && q.l.top.rows.size() == 1 && q.l.top.rows[0].key == "7|spotify:track:x");
    // A queued song chosen: librespot's own command, with its original index.
    service.command(kTileAction, 0, "LSPQUE\tselect\t7|spotify:track:x");
    assert(!sent.empty() && sent.back().find("play_queued") != std::string::npos && sent.back().find("\"index\":7") != std::string::npos);
    // Settings can't be saved away.
    service.command(kSaveSlots, 0, "LSPNOW,LSPQUE,,,,,");
    assert(!service.settings().arranged);
    adapter.disconnected();
    assert(parse_tiles(read_file(dir + "/" + kTilesFile)).empty());

    // The panel's model on those files: opens on the first tile; without librespot, System.
    std::vector<std::tuple<int, int, std::string>> commands;
    overlay::NativeModel model([&](int k, int v, const char* t) { commands.emplace_back(k, v, t ? t : ""); });
    model.data_dir(dir);
    adapter.connected();
    adapter.line(R"({"v":1,"state":{"title":"Song","artist":"Artist","playing":true,"duration_ms":1000}})");
    model.poll_files(now); model.tick(now, 1, 0, 0, false);
    assert(model.view().tile == "LSPNOW" && model.view().focused && model.view().rail.size() == 5);
    now += 50; model.tick(now, 1, 0x8000, 0x8000, false); // Square: play/pause, to librespot's tile
    assert(std::get<0>(commands.back()) == kTileAction && std::get<2>(commands.back()) == "LSPNOW\tplay_pause\t");
    adapter.disconnected();
    now += 1000; model.poll_files(now); model.tick(now, 2, 0, 0, false);
    assert(model.view().tile == kSystemTile && model.view().rail.size() == 2);
    std::filesystem::remove_all(dir);
    std::cout << "Kabuki tile checks passed\n";
}
