#pragma once
#include <cstddef>
#include <cstdint>

// CRC32 carried by Sony pads (DS4, DualSense) on their Bluetooth reports: reflected CRC32
// (poly 0xEDB88320) over a 1-byte seed followed by the report bytes, stored little endian in the
// report's last 4 bytes. The seed depends on the report direction — same values on both pads
// (ARCHITECTURE.md, "Protocolos de mando" → resultados de la prueba con hardware).
namespace sony_crc {

inline constexpr uint8_t kSeedInput   = 0xA1;
inline constexpr uint8_t kSeedOutput  = 0xA2;
inline constexpr uint8_t kSeedFeature = 0xA3;

inline uint32_t update(uint32_t crc, const uint8_t* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

// CRC of seed + data[0..len).
inline uint32_t compute(uint8_t seed, const uint8_t* data, size_t len)
{
    uint32_t crc = update(0xFFFFFFFFu, &seed, 1);
    crc = update(crc, data, len);
    return ~crc;
}

// True if the last 4 bytes of report[0..len) hold the CRC of everything before them.
inline bool check(uint8_t seed, const uint8_t* report, size_t len)
{
    if (len < 5) return false;
    const uint32_t stored = uint32_t(report[len - 4])       | uint32_t(report[len - 3]) << 8
                          | uint32_t(report[len - 2]) << 16 | uint32_t(report[len - 1]) << 24;
    return compute(seed, report, len - 4) == stored;
}

// Writes the CRC of report[0..len-4) into its last 4 bytes (outgoing reports).
inline void store(uint8_t seed, uint8_t* report, size_t len)
{
    if (len < 5) return;
    const uint32_t crc = compute(seed, report, len - 4);
    for (size_t i = 0; i < 4; ++i)
        report[len - 4 + i] = uint8_t(crc >> (8 * i));
}

} // namespace sony_crc
