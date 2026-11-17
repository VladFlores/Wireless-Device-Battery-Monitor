// MCHOSE V9 Pro 2 wireless headset (VID 3837, receiver PID 6011).
// Protocol taken from USBPcap captures of the official MCHOSE software (not from vendor docs).
// The vendor collection is FF07:0222, 64-byte input and output reports, report ID 55.
//
//   Request: 55 AA 01 00 <len> 00 <cmd hi> <cmd lo> <data...>
//   Reply:   55 AA 01 01 <len> 00 <cmd hi> <cmd lo> <data...> <checksum>
//   <len> counts the data bytes plus the checksum. In every captured reply the checksum is the
//   XOR of bytes 2 and 4..(last data byte), but it is not verified here.
//
// Battery: command 01 0B, data [8] = connected (01) / offline (00), [9] = battery value.
//   The headset also pushes the same pair unprompted (command 04 22): 00 FF when it is off,
//   01 57 when it is on. 0x57 = 87, while the official software showed 90%, so the value may be
//   rounded there. NOT YET CONFIRMED beyond one battery level: compare with the official
//   software over a few days of use. Charging state is not known yet.
#include "drivers.h"

namespace {

// Writes `req` and returns the first input report accepted by `match` within `timeoutMs`.
template <class Match>
bool Query(HidDev& d, const std::vector<BYTE>& req, std::vector<BYTE>& resp, int timeoutMs, Match match) {
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(resp, left) < 0) return false;
        if (match(resp)) return true;
    }
}

bool V9Pro2(const HidColl& c, DeviceStatus& s) {
    HidDev d;
    if (!d.Open(c)) return false;

    // Command 01 0B with one data byte (00), exactly as the official software sends it.
    const std::vector<BYTE> req = { 0x55, 0xAA, 0x01, 0x00, 0x01, 0x00, 0x01, 0x0B, 0x00 };
    std::vector<BYTE> r;
    // Reports on this collection can also be unsolicited (04 22), so wait for the 01 0B reply.
    bool ok = Query(d, req, r, 1000, [](const std::vector<BYTE>& r) {
        return r.size() >= 10 && r[0] == 0x55 && r[1] == 0xAA && r[3] == 0x01 && r[6] == 0x01 && r[7] == 0x0B;
    });
    if (!ok) return false;

    s.key = L"mchose:" + Hex4(c.pid);
    s.name = L"MCHOSE V9 Pro 2";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    s.connected = r[8] != 0;
    if (s.connected && r[9] <= 100) s.percent = r[9];
    return true;
}

}  // namespace

void PollMchose(PollContext& ctx) {
    const USHORT vid = 0x3837;
    const USHORT pid = 0x6011;
    if (ctx.IsClaimed(vid, pid)) return;
    for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
             // The receiver also exposes consumer-control and telephony collections; only this one
             // carries the vendor protocol.
             return c.page() == 0xFF07 && c.usage() == 0x0222 && c.outLen() == 64 && c.inLen() == 64;
         })) {
        DeviceStatus s;
        if (V9Pro2(*c, s)) { ctx.Add(s, vid, pid); break; }
    }
}
