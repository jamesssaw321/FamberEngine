// File access with Quake/GoldSrc PAK fallback: loose files win, then any
// pak0..pak9.pak mounted for the game dir the path lives under.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fs {

#pragma pack(push, 1)
struct PakHeader { char id[4]; int32_t dirofs, dirlen; };
struct PakEntry { char name[56]; int32_t ofs, len; };
#pragma pack(pop)

struct Pak {
    std::string file;   // pak path on disk
    std::string dir;    // normalized mount dir
    std::map<std::string, std::pair<int32_t, int32_t>> entries; // rel path -> ofs,len
};

inline std::vector<Pak>& paks() { static std::vector<Pak> v; return v; }

inline std::string norm(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    return s;
}

// scan dir for pak0..pak9.pak
inline void mount(const std::string& dir) {
    if (dir.empty()) return;
    std::string nd = norm(dir);
    for (const Pak& p : paks()) if (p.dir == nd) return; // already mounted
    for (int i = 0; i <= 9; i++) {
        std::string pp = dir + "/pak" + std::to_string(i) + ".pak";
        FILE* f = fopen(pp.c_str(), "rb");
        if (!f) continue;
        PakHeader h{};
        if (fread(&h, 1, sizeof(h), f) == sizeof(h) && !memcmp(h.id, "PACK", 4) && h.dirlen >= 0) {
            Pak pak; pak.file = pp; pak.dir = nd;
            int n = h.dirlen / (int)sizeof(PakEntry);
            fseek(f, h.dirofs, SEEK_SET);
            for (int e = 0; e < n; e++) {
                PakEntry pe;
                if (fread(&pe, 1, sizeof(pe), f) != sizeof(pe)) break;
                pe.name[55] = 0;
                pak.entries[norm(pe.name)] = {pe.ofs, pe.len};
            }
            printf("[fs] mounted %s (%d files)\n", pp.c_str(), (int)pak.entries.size());
            paks().push_back(std::move(pak));
        }
        fclose(f);
    }
}

// loose file first, then mounted paks
inline bool read(const std::string& path, std::vector<char>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        out.resize(n > 0 ? n : 0);
        bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
        fclose(f);
        if (ok) return true;
    }
    std::string np = norm(path);
    for (Pak& p : paks()) {
        if (np.size() <= p.dir.size() + 1 || np.compare(0, p.dir.size(), p.dir) != 0 ||
            np[p.dir.size()] != '/') continue;
        auto it = p.entries.find(np.substr(p.dir.size() + 1));
        if (it == p.entries.end()) continue;
        FILE* pf = fopen(p.file.c_str(), "rb");
        if (!pf) continue;
        fseek(pf, it->second.first, SEEK_SET);
        out.resize(it->second.second);
        bool ok = fread(out.data(), 1, out.size(), pf) == out.size();
        fclose(pf);
        if (ok) return true;
    }
    return false;
}

inline bool exists(const std::string& path) {
    std::vector<char> tmp;
    return read(path, tmp); // small files only; fine for probes
}

} // namespace fs
