#pragma once
#include "HidChannel.h"
#include "RawHIDReader.h"
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

enum class HidTransport { Unknown, Usb, Bluetooth };

// Transport of a HID device from its interface path: Bluetooth HID nodes carry the BT enumerator
// or the HID-over-BT service GUID in it. Anything else is treated as USB (dongles included).
// The one place this is decided — HIDScanner (connectionType) and HIDInputSource both use it.
inline HidTransport hidTransportFromPath(std::string_view path)
{
    std::string upper(path);
    for (auto& ch : upper) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (std::string_view marker : { "BTHENUM", "BLUETOOTHHIDDEVICE", "BTH_HID",
                                     "00001124-0000-1000-8000-00805F9B34FB" })
        if (upper.find(marker) != std::string::npos) return HidTransport::Bluetooth;
    return HidTransport::Usb;
}

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
