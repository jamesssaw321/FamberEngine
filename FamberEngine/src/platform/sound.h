// Software mixer on waveOut (winmm): 22050 Hz 16-bit stereo, N small
// buffers refilled by a mixer thread. WAV loader for PCM 8/16 mono/stereo.
#pragma once
#include "../core/files.h"
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace snd {

const int RATE = 22050;
const int FRAMES = 512;   // per buffer
const int NBUF = 4;
const int NCHAN = 32;

struct Sound { std::vector<int16_t> mono; }; // 22050 Hz mono

struct Channel {
    const Sound* snd = nullptr;
    size_t pos = 0;
    float volL = 0, volR = 0;
    bool loop = false;
    bool active = false;
    uint32_t gen = 0; // handle validation: id = (gen<<6)|channel
};

static HWAVEOUT g_wo = nullptr;
static WAVEHDR g_hdr[NBUF];
static int16_t g_buf[NBUF][FRAMES * 2];
static HANDLE g_event = nullptr, g_thread = nullptr;
static volatile bool g_run = false;
static Channel g_chan[NCHAN];
static CRITICAL_SECTION g_cs;
static float g_master = 1.0f; // console "volume" cvar binds here

inline void mixInto(int16_t* out) {
    static int32_t acc[FRAMES * 2];
    memset(acc, 0, sizeof(acc));
    EnterCriticalSection(&g_cs);
    for (Channel& c : g_chan) {
        if (!c.active || !c.snd) continue;
        const std::vector<int16_t>& s = c.snd->mono;
        for (int i = 0; i < FRAMES; i++) {
            if (c.pos >= s.size()) {
                if (c.loop && !s.empty()) c.pos = 0;
                else { c.active = false; break; }
            }
            int16_t v = s[c.pos++];
            acc[i * 2] += (int32_t)(v * c.volL);
            acc[i * 2 + 1] += (int32_t)(v * c.volR);
        }
    }
    LeaveCriticalSection(&g_cs);
    float master = g_master < 0 ? 0 : (g_master > 1 ? 1 : g_master);
    for (int i = 0; i < FRAMES * 2; i++) {
        int32_t v = (int32_t)(acc[i] * master);
        out[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

inline DWORD WINAPI mixThread(LPVOID) {
    while (g_run) {
        WaitForSingleObject(g_event, 100);
        for (int b = 0; b < NBUF; b++) {
            if (!(g_hdr[b].dwFlags & WHDR_DONE)) continue;
            mixInto(g_buf[b]);
            g_hdr[b].dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_wo, &g_hdr[b], sizeof(WAVEHDR));
        }
    }
    return 0;
}

inline bool init() { // safe to re-init after shutdown (changelevel)
    static bool csReady = false;
    if (!csReady) { InitializeCriticalSection(&g_cs); csReady = true; }
    memset(g_hdr, 0, sizeof(g_hdr));
    for (Channel& c : g_chan) c = Channel{};
    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = RATE;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = RATE * 4;
    g_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, (DWORD_PTR)g_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        printf("[snd] waveOutOpen failed\n");
        return false;
    }
    g_run = true;
    for (int b = 0; b < NBUF; b++) {
        g_hdr[b].lpData = (LPSTR)g_buf[b];
        g_hdr[b].dwBufferLength = FRAMES * 4;
        waveOutPrepareHeader(g_wo, &g_hdr[b], sizeof(WAVEHDR));
        mixInto(g_buf[b]);
        waveOutWrite(g_wo, &g_hdr[b], sizeof(WAVEHDR));
    }
    g_thread = CreateThread(nullptr, 0, mixThread, nullptr, 0, nullptr);
    return true;
}

inline void shutdown() {
    if (!g_wo) return;
    g_run = false;
    SetEvent(g_event);
    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = nullptr; }
    waveOutReset(g_wo);
    for (int b = 0; b < NBUF; b++) waveOutUnprepareHeader(g_wo, &g_hdr[b], sizeof(WAVEHDR));
    waveOutClose(g_wo);
    g_wo = nullptr;
    if (g_event) { CloseHandle(g_event); g_event = nullptr; }
}

// returns a handle ((gen<<6)|channel) or -1
inline int play(const Sound& s, float volL, float volR, bool loop) {
    if (!g_wo || s.mono.empty()) return -1;
    static uint32_t genCounter = 0;
    EnterCriticalSection(&g_cs);
    int id = -1;
    for (int i = 0; i < NCHAN; i++)
        if (!g_chan[i].active) {
            g_chan[i] = {&s, 0, volL, volR, loop, true, ++genCounter};
            id = (int)((genCounter << 6) | i);
            break;
        }
    LeaveCriticalSection(&g_cs);
    return id;
}

inline Channel* handleChan(int id) { // caller holds g_cs
    if (id < 0) return nullptr;
    int ch = id & 63;
    if (ch >= NCHAN || g_chan[ch].gen != (uint32_t)(id >> 6)) return nullptr;
    return &g_chan[ch];
}

inline void setVolume(int id, float volL, float volR) {
    EnterCriticalSection(&g_cs);
    if (Channel* c = handleChan(id)) { c->volL = volL; c->volR = volR; }
    LeaveCriticalSection(&g_cs);
}

inline void stop(int id) {
    EnterCriticalSection(&g_cs);
    if (Channel* c = handleChan(id)) c->active = false;
    LeaveCriticalSection(&g_cs);
}

// ---- WAV loader (PCM 8/16, mono/stereo, any rate -> 22050 mono) ----
inline bool loadWav(const std::string& path, Sound& out) {
    std::vector<char> raw;
    if (!fs::read(path, raw)) return false;
    const unsigned char* d = (const unsigned char*)raw.data();
    long n = (long)raw.size();
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return false;

    int rate = 0, bits = 0, chans = 0;
    const unsigned char* pcm = nullptr; uint32_t pcmLen = 0;
    size_t p = 12;
    while (p + 8 <= (size_t)n) {
        uint32_t len; memcpy(&len, d + p + 4, 4);
        if (!memcmp(d + p, "fmt ", 4) && len >= 16) {
            uint16_t fmt, ch; uint32_t sr; uint16_t bps;
            memcpy(&fmt, d + p + 8, 2);
            memcpy(&ch, d + p + 10, 2);
            memcpy(&sr, d + p + 12, 4);
            memcpy(&bps, d + p + 22, 2);
            if (fmt != 1) return false; // PCM only
            rate = sr; bits = bps; chans = ch;
        } else if (!memcmp(d + p, "data", 4)) {
            pcm = d + p + 8;
            pcmLen = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!pcm || !rate || !chans) return false;

    int inFrames = pcmLen / (chans * (bits / 8));
    int outFrames = (int)((int64_t)inFrames * RATE / rate);
    out.mono.resize(outFrames);
    for (int i = 0; i < outFrames; i++) {
        int src = (int)((int64_t)i * rate / RATE);
        int32_t v = 0;
        for (int c = 0; c < chans; c++) {
            if (bits == 16) {
                int16_t s; memcpy(&s, pcm + (src * chans + c) * 2, 2);
                v += s;
            } else {
                v += ((int)pcm[src * chans + c] - 128) << 8;
            }
        }
        out.mono[i] = (int16_t)(v / chans);
    }
    return true;
}

} // namespace snd
