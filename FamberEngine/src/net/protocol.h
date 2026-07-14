// Wire protocol: little-endian packed structs over UDP. The client sends
// usercmds (with redundancy against loss), the server answers with
// snapshots carrying the last acked cmd so the client can re-predict.
#pragma once
#include <cstdint>
#include <cstddef>

namespace proto {

const uint32_t MAGIC = 0x52454246; // "FBER"
const int MAX_PLAYERS = 8;
const int MAX_MOVERS = 64;
const int CMD_REDUNDANCY = 8;
const unsigned short DEFAULT_PORT = 27015;

enum : uint8_t { C2S_CONNECT = 1, S2C_ACCEPT, S2C_REJECT, C2S_CMD, S2C_SNAPSHOT, C2S_LEAVE,
                 C2S_CHAT, S2C_CHAT };

#pragma pack(push, 1)
struct Hdr { uint32_t magic; uint8_t type; };

struct Connect { Hdr h; char name[16]; char map[32]; };
struct Accept  { Hdr h; uint8_t id; char map[32]; float spawn[3]; float yaw; };
struct Reject  { Hdr h; char reason[32]; };

struct UserCmd {
    uint32_t seq;
    uint8_t msec;        // 1..50
    int8_t fwd, side;    // -1/0/1
    uint8_t buttons;
    float yaw, pitch;
};
const uint8_t BTN_JUMP = 1;
const uint8_t BTN_USE = 2;

struct CmdPacket { Hdr h; uint8_t id; uint8_t count; UserCmd cmds[CMD_REDUNDANCY]; };
inline int cmdPacketSize(int count) { return (int)offsetof(CmdPacket, cmds) + count * (int)sizeof(UserCmd); }

const uint8_t PSF_ONGROUND = 1;
struct PlayerState { uint8_t id, active, flags; float pos[3], vel[3]; float yaw; };
struct MoverOff { float off[3]; };
struct Snapshot {
    Hdr h; uint32_t ack;
    uint8_t num, nmovers;
    PlayerState pl[MAX_PLAYERS];  // fixed block, first `num` valid
    MoverOff mv[MAX_MOVERS];      // trailing, `nmovers` sent
};
inline int snapshotSize(int nmovers) { return (int)offsetof(Snapshot, mv) + nmovers * (int)sizeof(MoverOff); }

struct ChatC2S { Hdr h; uint8_t id; char text[112]; };
struct ChatS2C { Hdr h; char line[128]; };
#pragma pack(pop)

} // namespace proto
