#pragma once
#include "HidChannel.h"
#include "RawHIDReader.h"
#include <cstdint>

enum class HidTransport { Unknown, Usb, Bluetooth };

// How PadsWay talks to one family of controllers (ARCHITECTURE.md, "Protocolos de mando"): the only
// hardware-specific layer. It may activate the pad (write the vendor's requests) and it translates
// the vendor's report into RawHIDState — a stable layout per family, the same over USB and BT — so
// discovery (wizard, Scanner) and mapping above it never see vendor bytes.
//
// Strategy with do-nothing defaults: a family overrides only what it needs. GenericHidProtocol (the
// default for every pad) overrides decode() alone and never writes to the device.
class ControllerProtocol {
public:
    virtual ~ControllerProtocol() = default;

    // Level 1 — keep the pad alive and visible without changing its report format.
    virtual bool hold(HidChannel& /*channel*/, HidTransport /*transport*/)           { return true; }
    // Level 2 — switch the pad to its full report (Sony: extended report; Switch: 0x30 + IMU).
    virtual bool enableFullMode(HidChannel& /*channel*/, HidTransport /*transport*/) { return true; }
    // Called once per read tick; most protocols need nothing here.
    virtual void keepAlive(HidChannel& /*channel*/, uint64_t /*nowMs*/)              {}
    // On close — give the pad back as found where the family needs it.
    virtual void release(HidChannel& /*channel*/)                                    {}

    // Translates one report (report[0] = report ID, len = bytes the OS returned) into out.
    // False = a report this protocol doesn't understand: the caller treats it as "no new report".
    virtual bool decode(const BYTE* report, ULONG len, RawHIDState& out) = 0;
};
