// Console backend: cvar registry, commands, log. Cvars bind directly to
// engine floats (con::reg("sv_gravity", &pm::GRAVITY, ...)), so setting a
// cvar changes the engine variable with no polling.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace con {

struct Cvar {
    std::string name, help;
    float* bind;
    float get() const { return *bind; }
    void set(float v) { *bind = v; }
};

using Args = std::vector<std::string>;
using CmdFn = std::function<void(const Args&)>;
struct Cmd { std::string name, help; CmdFn fn; };

inline std::deque<Cvar>& cvars() { static std::deque<Cvar> v; return v; }
inline std::deque<Cmd>& cmds() { static std::deque<Cmd> v; return v; }
inline std::deque<std::string>& log() { static std::deque<std::string> v; return v; }

inline void print(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    std::string s = buf, line;
    for (char c : s) {
        if (c == '\n') { log().push_back(line); line.clear(); }
        else line.push_back(c);
    }
    log().push_back(line);
    while (log().size() > 256) log().pop_front();
}

inline std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

inline Cvar* findVar(const std::string& n) {
    std::string k = lower(n);
    for (Cvar& v : cvars()) if (v.name == k) return &v;
    return nullptr;
}

inline Cmd* findCmd(const std::string& n) {
    std::string k = lower(n);
    for (Cmd& c : cmds()) if (c.name == k) return &c;
    return nullptr;
}

inline void reg(const char* name, float* bind, const char* help) {
    if (!findVar(name)) cvars().push_back({lower(name), help, bind});
}

inline void regCmd(const char* name, CmdFn fn, const char* help) {
    if (!findCmd(name)) cmds().push_back({lower(name), help, fn});
}

inline Args tokenize(const std::string& line) {
    Args out; std::string cur; bool q = false, has = false;
    for (char c : line) {
        if (c == '"') { q = !q; has = true; }
        else if (!q && (c == ' ' || c == '\t')) {
            if (has || !cur.empty()) { out.push_back(cur); cur.clear(); has = false; }
        } else cur.push_back(c);
    }
    if (has || !cur.empty()) out.push_back(cur);
    return out;
}

inline void exec(const std::string& line) {
    Args t = tokenize(line);
    if (t.empty()) return;
    if (Cmd* c = findCmd(t[0])) { c->fn(t); return; }
    if (Cvar* v = findVar(t[0])) {
        if (t.size() >= 2) { v->set((float)atof(t[1].c_str())); print("%s = %g", v->name.c_str(), v->get()); }
        else print("%s = %g  -- %s", v->name.c_str(), v->get(), v->help.c_str());
        return;
    }
    print("unknown command: %s", t[0].c_str());
}

} // namespace con
