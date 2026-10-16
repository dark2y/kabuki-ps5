// SPDX-License-Identifier: GPL-3.0-or-later
// List interaction is independent of Borealis so it can be checked without a console.
#pragma once
#include "player.hpp"
#include <algorithm>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace overlay {

class ListRepeat {
    int direction = 0;
    std::uint64_t next = 0;
public:
    void reset() { direction = 0; next = 0; }
    int step(std::uint32_t pressed, std::uint32_t held, std::uint64_t now) {
        const int edge = (pressed & 0x10) ? -1 : (pressed & 0x40) ? 1 : 0;
        const int down = (held & 0x50) == 0x10 ? -1 : (held & 0x50) == 0x40 ? 1 : 0;
        if (edge) { direction = down == edge ? edge : 0; next = now + 400; return edge; }
        if (!down || down != direction) { reset(); return 0; }
        if (now < next) return 0;
        next = now + 110;
        return direction;
    }
};

class BrowseControl {
    Player& player;
    std::function<void(int, int, const char*)> send;
    bool requested = false, waiting = false, saw_pending = false;
    std::uint64_t library_requested_at = 0, started_at = 0;
    std::string starting_title, starting_artist;
public:
    std::uint64_t revision = 0;
    BrowseControl(Player& p, std::function<void(int, int, const char*)> command)
        : player(p), send(std::move(command)) {}

    void cancel() { waiting = false; player.browse.starting_row = -1; }
    void request_library(std::uint64_t now) {
        if (requested && now - library_requested_at < 60000 && player.browse.library_error.empty()) return;
        requested = true; library_requested_at = now;
        send(9, 0, nullptr);
    }
    void clamp_selection() {
        auto& b = player.browse;
        b.queue_selected = std::clamp(b.queue_selected, 0, std::max(0, int(player.state.queue.size()) - 1));
        b.library_selected = std::clamp(b.library_selected, 0, std::max(0, int(b.library.size()) - 1));
        b.track_selected = std::clamp(b.track_selected, 0, std::max(0, int(b.tracks.size()) - 1));
    }
    void move(int tab, int delta) {
        auto& b = player.browse;
        if (tab == 1) b.queue_selected += delta;
        else if (b.library_level == 0) b.library_selected += delta;
        else b.track_selected += delta;
        clamp_selection();
    }
    void choose(int tab, std::uint64_t now) {
        clamp_selection();
        auto& b = player.browse;
        std::string title, artist;
        int row;
        if (tab == 1) {
            if (player.state.queue.empty()) return;
            const auto& item = player.state.queue[b.queue_selected];
            if (item.uri.empty()) return;
            row = b.queue_selected; title = item.title; artist = item.artist;
            send(8, item.index < 0 ? row : item.index, item.uri.c_str());
        } else if (b.library_level == 0) {
            if (b.library.empty()) return;
            cancel();
            b.list_uri = b.library[b.library_selected].uri;
            b.library_level = 1; b.track_selected = 0;
            b.tracks.clear(); b.tracks_known = false; b.tracks_error.clear();
            send(10, 0, b.list_uri.c_str());
            return;
        } else {
            if (b.tracks.empty()) return;
            const auto& item = b.tracks[b.track_selected];
            if (item.uri.empty() || b.list_uri.empty()) return;
            row = b.track_selected; title = item.title; artist = item.artist;
            const std::string text = b.list_uri + " " + item.uri;
            if (text.size() >= 128) return; // the shared command ring's text capacity
            send(11, 0, text.c_str());
        }
        started_at = now; waiting = true; saw_pending = false;
        starting_title = title; starting_artist = artist; b.starting_row = row;
    }
    bool back(int tab) {
        cancel();
        auto& b = player.browse;
        if (tab != 2 || b.library_level != 1) return false;
        b.library_level = 0;
        return true;
    }
    // Returns true once the requested song is heard; the console gives Now Playing focus.
    bool heard(std::uint64_t now) {
        if (!waiting) return false;
        const auto& s = player.state;
        saw_pending |= s.pending;
        if (now - started_at >= 15000) { cancel(); return false; }
        if (!s.pending && s.playing && s.title == starting_title && s.artist == starting_artist &&
            (saw_pending || now - started_at > 2500)) {
            cancel(); return true;
        }
        return false;
    }
    bool apply_library(const nlohmann::json& j) {
        if (!j.is_object() || !j.contains("library") || !j["library"].is_object()) return false;
        const auto& list = j["library"];
        if (!list.contains("items") || !list["items"].is_array()) return false;
        std::vector<LibraryItem> items;
        for (const auto& item : list["items"]) {
            if (!item.is_object()) continue;
            items.push_back({item.value("kind", ""), item.value("uri", ""), item.value("name", ""),
                             item.value("subtitle", ""), item.value("cover", "")});
        }
        const auto error = list.value("error", "");
        auto& b = player.browse;
        const std::string selected = b.library.empty() ? "" : b.library[b.library_selected].uri;
        b.library = std::move(items); b.library_known = true; b.library_error = error;
        for (std::size_t i = 0; i < b.library.size(); ++i)
            if (b.library[i].uri == selected) { b.library_selected = int(i); break; }
        clamp_selection(); ++revision;
        return true;
    }
    bool apply_tracks(const nlohmann::json& j) {
        if (!j.is_object() || !j.contains("tracks") || !j["tracks"].is_object()) return false;
        const auto& list = j["tracks"];
        auto& b = player.browse;
        if (b.library_level != 1 || list.value("uri", "") != b.list_uri ||
            !list.contains("items") || !list["items"].is_array()) return false;
        std::vector<TrackItem> items;
        for (const auto& item : list["items"]) {
            if (!item.is_object()) continue;
            items.push_back({item.value("uri", ""), item.value("title", ""), item.value("artist", "")});
        }
        const auto error = list.value("error", "");
        b.tracks = std::move(items); b.tracks_known = true; b.tracks_error = error;
        clamp_selection(); ++revision;
        return true;
    }
};
} // namespace overlay
