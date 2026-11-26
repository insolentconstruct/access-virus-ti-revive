// The Virus' sample rate for a host rate, like the original (E323: Virus Control 0x1800a9dc0): the host rate is
// doubled while below 44 kHz and halved while above 88 kHz, then 46 kHz and up runs the Virus at 48 kHz, anything
// lower at 44.1 kHz; the plug-in converts the rest.
#include "check.h"
#include "device/virusrate.h"
#include <limits>

using namespace vti;

TEST(virusrate_follows_the_original)
{
    CHECK_EQ(virus_rate_for(44100), 44100);
    CHECK_EQ(virus_rate_for(48000), 48000);
    CHECK_EQ(virus_rate_for(88200), 44100);
    CHECK_EQ(virus_rate_for(96000), 48000);
    CHECK_EQ(virus_rate_for(176400), 44100);
    CHECK_EQ(virus_rate_for(192000), 48000);
    CHECK_EQ(virus_rate_for(22050), 44100);
    CHECK_EQ(virus_rate_for(32000), 48000); // doubled to 64 kHz
    CHECK_EQ(virus_rate_for(24000), 48000);
    CHECK_EQ(virus_rate_for(44000), 44100); // not below 44000: stays, below 46 kHz
    CHECK_EQ(virus_rate_for(45999), 44100);
    CHECK_EQ(virus_rate_for(46000), 48000);
    CHECK_EQ(virus_rate_for(88000), 48000); // not above 88000: stays
    CHECK_EQ(virus_rate_for(384000), 48000);
}

TEST(virusrate_no_host_rate_keeps_44k1)
{
    CHECK_EQ(virus_rate_for(0), 44100);
    CHECK_EQ(virus_rate_for(-1), 44100);
    CHECK_EQ(virus_rate_for(std::numeric_limits<double>::infinity()), 44100); // halving infinity never ends
    CHECK_EQ(virus_rate_for(std::nan("")), 44100);
}
