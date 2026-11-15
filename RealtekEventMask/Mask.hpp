#pragma once
// Packet includes the little-endian opcode and one-byte parameter length.
inline bool enableReadyEvents(unsigned char *packet, unsigned long size) {
    if (size != 11 || packet[0] != 0x01 || packet[1] != 0x20 || packet[2] != 8)
        return false;
    packet[3] |= 0x40;
    packet[4] |= 0x08;
    return true;
}
