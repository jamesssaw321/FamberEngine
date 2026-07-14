// GoldSrc-style drop-down console: `~` toggles, slides over the top half,
// Enter executes via con::exec, Up/Down = history, PgUp/PgDn = scroll.
#pragma once
#include "../core/cvar.h"
#include "../platform/window.h"
#include "../render/font.h"
#include <cmath>
#include <string>
#include <vector>

struct Console {
    bool open = false;
    float frac = 0;   // slide animation 0..1
    float blink = 0;
    std::string input;
    std::vector<std::string> hist;
    int histPos = -1; // -1 = editing a fresh line
    int scroll = 0;   // log lines scrolled up

    void toggle() {
        open = !open;
        plat::escQuits = !open;
        if (open) plat::releaseCursor();
        histPos = -1;
    }

    void submit() {
        con::print("] %s", input.c_str());
        if (!input.empty()) { hist.push_back(input); con::exec(input); }
        histPos = -1; scroll = 0;
        input.clear();
    }

    void update(float dt) {
        int vk; unsigned c;
        while (plat::nextKey(vk)) {
            if (vk == VK_OEM_3) { toggle(); continue; }
            if (!open) continue;
            if (vk == VK_ESCAPE) toggle();
            else if (vk == VK_UP && !hist.empty()) {
                if (histPos < 0) histPos = (int)hist.size() - 1;
                else if (histPos > 0) histPos--;
                input = hist[histPos];
            } else if (vk == VK_DOWN && histPos >= 0) {
                histPos++;
                if (histPos >= (int)hist.size()) { histPos = -1; input.clear(); }
                else input = hist[histPos];
            } else if (vk == VK_PRIOR) scroll += 4;
            else if (vk == VK_NEXT) { scroll -= 4; if (scroll < 0) scroll = 0; }
        }
        while (plat::nextChar(c)) {
            if (!open || c == '`' || c == '~') continue;
            if (c == '\b') { if (!input.empty()) input.pop_back(); }
            else if (c == '\r') submit();
            else if (c >= 32 && c < 127) input.push_back((char)c);
        }
        frac += (open ? 1 : -1) * dt / 0.15f;
        frac = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
        blink += dt;
    }

    void draw(TextGL& t, int w, int h) {
        if (frac <= 0) return;
        const int S = 2, LH = 8 * S + 2;
        int ch = h / 2;
        int top = (int)(frac * ch) - ch;
        t.rect(0, (float)top, (float)w, (float)ch, 0.05f, 0.06f, 0.09f, 0.93f);
        t.rect(0, (float)(top + ch - 2), (float)w, 2, 1.0f, 0.62f, 0.10f, 1.0f);
        const char* tag = "FamberEngine";
        t.text(w - 8.0f * S * 12 - 6, (float)(top + ch - 10 - 8 * S), S, tag, 1.0f, 0.62f, 0.10f, 0.85f);

        int y = top + ch - 8 - LH; // input line
        std::string line = "] " + input;
        if (std::fmod(blink, 0.6f) < 0.35f) line += "_";
        t.text(6, (float)y, S, line.c_str(), 1, 1, 1, 1);

        auto& lg = con::log();
        int maxScroll = (int)lg.size() - 1;
        if (scroll > maxScroll) scroll = maxScroll < 0 ? 0 : maxScroll;
        int i = (int)lg.size() - 1 - scroll;
        for (int ly = y - LH; ly > top - LH && i >= 0; ly -= LH, i--)
            t.text(6, (float)ly, S, lg[i].c_str(), 0.82f, 0.88f, 0.95f, 1);
    }
};
