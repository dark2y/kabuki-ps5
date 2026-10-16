// SPDX-License-Identifier: GPL-3.0-or-later
// The native ShellUI control centre on the PC: the console's own model and layout
// (native_model.hpp, native_layout.hpp) drive a recorded widget tree instead of PUI, and
// ui/tools/native_preview.py draws that tree in a browser.
//
// Usage: native_preview DATA_DIR FONT_DIR
// Each line read: "held pressed circle open" (pad bits as the console's, circle/open 0 or 1).
// Each line written: the frame as JSON (widgets, commands sent, now_ms). Widgets carry their
// skin pixels the first time they are sent.
#define _CRT_SECURE_NO_WARNINGS
#include "native_layout.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <chrono>
#include <iostream>
#include <map>

namespace {
using overlay::native::Box;
using overlay::native::Rgb;

struct Node {
    int parent = -1;
    std::string type; // panel, skin, image, label
    Box box{};
    bool visible = true, clip = false;
    float alpha = 1, scale = 1;
    float tint[4] = {1, 1, 1, 1};
    std::string text, uri;
    float size = 0;
    int weight = 0, align = 0, trim = 0;
    Rgb colour{1, 1, 1};
    int kind = 0, pw = 0, ph = 0;
    std::string pixels; // base64 RGBA, sent once
};

std::string base64(const std::vector<unsigned char>& data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        const unsigned v = data[i] << 16 | (i + 1 < data.size() ? data[i + 1] << 8 : 0) | (i + 2 < data.size() ? data[i + 2] : 0);
        out += table[v >> 18 & 63];
        out += table[v >> 12 & 63];
        out += i + 1 < data.size() ? table[v >> 6 & 63] : '=';
        out += i + 2 < data.size() ? table[v & 63] : '=';
    }
    return out;
}

struct Font {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ok = false;
    void load(const std::string& path) {
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            bytes.resize(std::size_t(std::ftell(f)));
            std::fseek(f, 0, SEEK_SET);
            ok = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
            std::fclose(f);
        }
        ok = ok && stbtt_InitFont(&info, bytes.data(), stbtt_GetFontOffsetForIndex(bytes.data(), 0));
    }
    float width(const std::string& text, float size) const {
        if (!ok) return text.size() * size * 0.5f;
        const float scale = stbtt_ScaleForMappingEmToPixels(&info, size);
        float w = 0;
        int previous = 0;
        for (std::size_t i = 0; i < text.size();) {
            unsigned c = (unsigned char)text[i++];
            if (c >= 0xC0) { // UTF-8
                const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
                c &= 0x3F >> extra;
                for (int k = 0; k < extra && i < text.size(); ++k) c = c << 6 | ((unsigned char)text[i++] & 0x3F);
            }
            int advance, bearing;
            stbtt_GetCodepointHMetrics(&info, int(c), &advance, &bearing);
            if (previous) w += stbtt_GetCodepointKernAdvance(&info, previous, int(c)) * scale;
            w += advance * scale;
            previous = int(c);
        }
        return w;
    }
};

class Recorder {
public:
    struct Widget { int id = -1; };
    std::vector<Node> nodes;
    std::vector<bool> sent;
    Font fonts[4]; // by weight: normal, bold, light, medium

    Recorder() { nodes.push_back({}); nodes[0].type = "screen"; nodes[0].box = {0, 0, 1920, 1080}; screen_.id = 0; }
    Widget& screen() { return screen_; }

    bool panel(Widget& w, Widget& parent, Box box, bool clip) { add(w, parent, "panel", box).clip = clip; return true; }
    bool skin(Widget& w, Widget& parent, Box box, int kind, float radius) {
        auto& n = add(w, parent, "skin", box);
        n.kind = kind;
        overlay::native::skin_size(kind, box.w, box.h, 2, n.pw, n.ph); // as on a 4K console
        n.pixels = base64(overlay::native::skin_pixels(kind, box.w, box.h, radius, n.pw, n.ph));
        return true;
    }
    bool image(Widget& w, Widget& parent, Box box, const std::string& uri) { add(w, parent, "image", box).uri = uri; return true; }
    bool label(Widget& w, Widget& parent, Box box, float size, int weight, Rgb colour, int align, int trim, const std::string& text) {
        auto& n = add(w, parent, "label", box);
        n.size = size; n.weight = weight; n.colour = colour; n.align = align; n.trim = trim; n.text = text;
        return true;
    }
    void geometry(Widget& w, Box box) { at(w).box = box; }
    void show(Widget& w, bool on) { at(w).visible = on; }
    void alpha(Widget& w, float a) { at(w).alpha = a; }
    void words(Widget& w, const std::string& text) { at(w).text = text; }
    void tint(Widget& w, Rgb c, float a) { auto& n = at(w); n.tint[0] = c.r; n.tint[1] = c.g; n.tint[2] = c.b; n.tint[3] = a; }
    void picture(Widget& w, const std::string& uri) { at(w).uri = uri; }
    void scale(Widget& w, float s) { at(w).scale = s; }
    float text_width(Widget& w, const std::string& text) {
        const auto& n = at(w);
        static const int face[] = {0, 1, 2, 3, 2, 1, 3}; // UI3 light/bold/medium: the nearest SST face
        return fonts[face[std::clamp(n.weight, 0, 6)]].width(text, n.size);
    }
    std::string icon(const char* name) { return std::string("cxml://custom_overlay/") + name; }
    std::string fallback_art() { return "cxml://custom_common/system_assets_Music_Fallback_01-500x500"; }
    // The console's glass focus is PUI's own; the preview keeps the layout's rings.
    bool focus(const overlay::native::Focus& f, int) { focused = f; return false; }
    overlay::native::Focus focused{};

    nlohmann::json frame() {
        auto list = nlohmann::json::array();
        sent.resize(nodes.size(), false);
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            nlohmann::json j = {{"id", i}, {"parent", n.parent}, {"type", n.type},
                                {"box", {n.box.x, n.box.y, n.box.w, n.box.h}}, {"visible", n.visible}, {"alpha", n.alpha}};
            if (n.scale != 1) j["scale"] = n.scale;
            if (n.clip) j["clip"] = true;
            if (n.type == "skin" || n.type == "image") j["tint"] = {n.tint[0], n.tint[1], n.tint[2], n.tint[3]};
            if (n.type == "image") j["uri"] = n.uri;
            if (n.type == "skin") {
                j["kind"] = n.kind;
                if (!sent[i]) { j["pw"] = n.pw; j["ph"] = n.ph; j["pixels"] = n.pixels; }
            }
            if (n.type == "label") {
                j["text"] = n.text; j["size"] = n.size; j["weight"] = n.weight; j["align"] = n.align; j["trim"] = n.trim;
                j["colour"] = {n.colour.r, n.colour.g, n.colour.b};
            }
            sent[i] = true;
            list.push_back(std::move(j));
        }
        return list;
    }

private:
    Widget screen_;
    Node& add(Widget& w, Widget& parent, const char* type, Box box) {
        w.id = int(nodes.size());
        nodes.push_back({});
        auto& n = nodes.back();
        n.parent = parent.id; n.type = type; n.box = box;
        return n;
    }
    Node& at(Widget& w) { return nodes.at(std::size_t(w.id)); }
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: native_preview DATA_DIR FONT_DIR\n"; return 2; }
    std::ios::sync_with_stdio(false);
    Recorder recorder;
    const std::string fonts = argv[2];
    recorder.fonts[0].load(fonts + "/SST-Roman.otf");
    recorder.fonts[1].load(fonts + "/SST-Bold.otf");
    recorder.fonts[2].load(fonts + "/SST-Light.otf");
    recorder.fonts[3].load(fonts + "/SST-Medium.otf");
    auto commands = nlohmann::json::array();
    overlay::NativeModel model([&commands](int kind, int value, const char* text) {
        commands.push_back({{"kind", kind}, {"value", value}, {"text", text ? text : ""}});
    });
    model.data_dir(argv[1]);
    overlay::native::Layout<Recorder> layout(recorder);
    if (!layout.create()) { std::cerr << "layout create failed\n"; return 1; }
    const auto start = std::chrono::steady_clock::now();
    bool open = false, back = false;
    std::uint64_t opening = 0;
    std::uint32_t held = 0, pressed = 0;
    int circle = 0, want_open = 0;
    while (std::cin >> held >> pressed >> circle >> want_open) {
        const auto now_us = std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                                              std::chrono::steady_clock::now() - start).count()) + 1000000;
        const auto now = now_us / 1000;
        if (want_open && !open) opening = now_us;
        open = want_open;
        back = false;
        // As the console's input guardian: Circle in a card goes back to the rail; on the rail it closes.
        // Circle closes, except inside a menu in the card (a popup, an open playlist's tracks).
        if (circle && open) {
            const auto v = model.view();
            if (v.nested) back = true;
            else open = false;
        }
        if (open) {
            model.poll_files(now);
            // Sample readings for the System tab (the console reads its own).
            overlay::SystemStats st;
            st.cpu = 18 + 9 * float(std::sin(now / 1700.0)); st.cpu_temp = 54; st.soc_temp = 61;
            st.mem_used_gb = 9.4f; st.mem_total_gb = 12.5f; st.fps = 59.8f; st.fps_min = 56;
            st.uptime_s = 2 * 3600 + 14 * 60 + int(now / 1000 % 60); st.firmware = "13.20";
            st.address = "192.168.1.20"; st.librespot = "Connected";
            model.system(st);
            model.tick(now, opening, held, pressed, back);
        }
        layout.frame(model.view(), open, now_us, opening);
        const auto view = model.view();
        nlohmann::json out = {{"now_ms", now}, {"open", open}, {"commands", commands}, {"sounds", model.take_sounds()}, {"widgets", recorder.frame()},
                              {"view", {{"tab", view.tab}, {"rail", view.rail}, {"focused", view.focused},
                                        {"control", view.control}, {"seek", view.seek}, {"popup", view.popup}}}};
        commands = nlohmann::json::array();
        std::cout << out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n' << std::flush;
    }
    return 0;
}
