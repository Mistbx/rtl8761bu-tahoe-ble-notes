#include "Mask.hpp"
#include <cassert>
#include <cstring>
int main() {
    // Exhaust all combinations of the two affected bytes, preserve the others.
    for (unsigned value = 0; value < 65536; ++value) {
        unsigned char p[] = {1,32,8, static_cast<unsigned char>(value),
            static_cast<unsigned char>(value>>8),0xa5,0x5a,0xff,0x80,0x42,0x18};
        unsigned char expected[11]; std::memcpy(expected,p,11);
        expected[3] |= 0x40; expected[4] |= 8;
        assert(enableReadyEvents(p,11));
        assert(std::memcmp(p,expected,11)==0);
        assert(enableReadyEvents(p,11)); // Idempotent.
        assert(std::memcmp(p,expected,11)==0);
    }
    unsigned char initial[] = {1,32,8,31,0,0,0,0,0,0,0};
    assert(enableReadyEvents(initial,11));
    assert(initial[3]==0x5f && initial[4]==8);
    for (unsigned size=0;size<11;++size) {
        unsigned char p[] = {1,32,8,31,0,0,0,0,0,0,0}, before[11];
        std::memcpy(before,p,11);
        assert(!enableReadyEvents(p,size));
        assert(std::memcmp(p,before,11)==0);
    }
    for(unsigned index=0;index<3;++index) {
        unsigned char p[] = {1,32,8,31,0,0,0,0,0,0,0}, before[11];
        p[index]^=1; std::memcpy(before,p,11);
        assert(!enableReadyEvents(p,11));
        assert(std::memcmp(p,before,11)==0);
    }
}
