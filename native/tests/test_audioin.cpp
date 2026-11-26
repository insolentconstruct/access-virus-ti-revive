// AudioIn on a ring of its own (POSIX shm with the broker's layout, vtiaudioshm.h; never the broker's ring): header
// check, stamp-aligned reads, drift correction and re-anchoring, raw reads for the ASIO driver, the output ring.
// Linux only (macOS reads Core Audio, Windows a named mapping).
#include "check.h"
#include <algorithm>
#include <cmath>
#include "device/audioin.h"
#include "device/crossfade.h"
#ifdef __linux__
#include "../../src/broker/vtiaudioshm.h"
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using namespace vti;

namespace {
// frame f carries recognisable samples: channel c = (f * 8 + c) & 0x7FFF
struct Ring {
    std::string name = "/vtitests-audio-" + std::to_string(getpid());
    vti_audio_shm *shm = nullptr;
    explicit Ring(uint32_t magic = VTI_AUDIO_MAGIC, uint32_t version = VTI_AUDIO_VERSION)
    {
        shm_unlink(name.c_str());
        int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, sizeof(vti_audio_shm)))
            return;
        void *p = mmap(nullptr, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (p == MAP_FAILED)
            return;
        shm = (vti_audio_shm *)p;
        shm->magic = magic, shm->version = version, shm->rate = 44100;
        shm->in_ch = VTI_AUDIO_IN_CH, shm->out_ch = VTI_AUDIO_OUT_CH, shm->ring_frames = VTI_AUDIO_RING;
    }
    ~Ring()
    {
        if (shm)
            munmap(shm, sizeof(vti_audio_shm));
        shm_unlink(name.c_str());
    }
    static int16_t sample(int64_t f, int c) { return (int16_t)((f * 8 + c) & 0x7FFF); }
    void produce(int64_t upto) // the synth delivered frames up to `upto` (exclusive)
    {
        for (int64_t f = shm->in_frames; f < upto; f++)
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++)
                shm->in[(f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH + c] = sample(f, c);
        shm->in_frames = upto;
        shm->streaming = 1;
    }
};
const float K = 1.0f / 32768.0f;
} // namespace

TEST(audioin_open_checks_the_ring)
{
    AudioIn a;
    CHECK(!a.open("/vtitests-audio-missing"));
    {
        Ring bad(0x12345678); // not a TI Revive ring
        CHECK(bad.shm && !a.open(bad.name.c_str()));
    }
    Ring r;
    CHECK(r.shm && a.open(r.name.c_str()));
    CHECK(!a.streaming() && a.newest_frame() == -1); // the broker has not started the stream yet
    float l1[4], r1[4], l2[4], r2[4];
    a.read(l1, r1, l2, r2, 4, 100);
    CHECK(l1[0] == 0 && r2[3] == 0); // silence until it streams
    a.close();
    a.close(); // twice is harmless
}

// Rate following (design-rate.md step 2, E323): the broker writes the Virus' current rate into the ring and counts its
// switches in rate_gen; the reader takes the rate and reports each switch once (a new stream: re-anchor, new ping)
TEST(audioin_reports_the_rate_and_its_switches)
{
    AudioIn a;
    CHECK_EQ(a.rate(), 44100); // nothing open yet
    int rate = 0;
    CHECK(!a.rate_changed(rate));
    Ring r;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(1000);
    CHECK_EQ(a.rate(), 44100);
    CHECK(!a.rate_changed(rate)); // the rate the ring was opened with is no switch
    r.shm->rate = 48000;
    r.shm->rate_gen = 1;
    CHECK_EQ(a.rate(), 48000);
    CHECK(a.rate_changed(rate) && rate == 48000);
    CHECK(!a.rate_changed(rate)); // once per switch
    r.shm->rate = 44100;
    r.shm->rate_gen = 2;
    CHECK(a.rate_changed(rate) && rate == 44100);
    r.shm->rate = 0; // an old broker never wrote a rate: 44.1 kHz
    CHECK_EQ(a.rate(), 44100);
}

// step 4: on Linux and Windows the broker owns the rate (Broker::audio); asking AudioIn changes nothing in the ring
#ifndef __APPLE__
TEST(audioin_request_rate_leaves_the_broker_ring)
{
    AudioIn a;
    CHECK_EQ(a.request_rate(48000), 44100); // nothing open
    Ring r;
    CHECK(r.shm && a.open(r.name.c_str(), 48000));
    CHECK_EQ(a.request_rate(48000), 44100);
    CHECK(r.shm->rate_gen == 0);
    int rate = 0;
    CHECK(!a.rate_changed(rate));
    r.shm->rate = 48000;
    CHECK_EQ(a.request_rate(44100), 48000); // the broker's rate stays
}
#endif

// Review 2026-10-19 (H4): a ring someone else created, or one too small, was mapped as is - reading the header of a
// zero-length object raised SIGBUS in the host. Owner and size are checked before mapping.
TEST(audioin_refuses_a_ring_too_small)
{
    std::string name = "/vtitests-audio-empty-" + std::to_string(getpid());
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600); // zero length
    CHECK(fd >= 0);
    if (fd >= 0)
        ::close(fd);
    AudioIn a;
    CHECK(!a.open(name.c_str()));
    shm_unlink(name.c_str());
}

TEST(audioin_read_at_maps_usb1_usb2)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(5000);
    CHECK_EQ(a.newest_frame(), 5000);
    float l1[8], r1[8], l2[8], r2[8];
    a.read_at(l1, r1, l2, r2, 8, 4996); // 4 frames there, 4 not yet
    CHECK(l1[0] == Ring::sample(4996, 2) * K && r1[0] == Ring::sample(4996, 3) * K); // USB 1 = ring channels 2/3
    CHECK(l2[3] == Ring::sample(4999, 4) * K && r2[3] == Ring::sample(4999, 5) * K); // USB 2 = ring channels 4/5
    CHECK(l1[4] == 0 && r2[7] == 0);                                                  // the future is silence
    r.produce(40000);
    a.read_at(l1, r1, l2, r2, 1, 40000 - VTI_AUDIO_RING); // overwritten long ago (outside the safe window)
    CHECK(l1[0] == 0);
    int16_t raw[3 * VTI_AUDIO_IN_CH];
    a.read_raw_at(raw, 3, 39998); // two frames, then the future
    CHECK(raw[0] == Ring::sample(39998, 0) && raw[VTI_AUDIO_IN_CH + 5] == Ring::sample(39999, 5));
    CHECK(raw[2 * VTI_AUDIO_IN_CH] == 0);
    a.read_raw_at(raw, 1, -5);
    CHECK(raw[0] == 0);
    a.read_raw_at(raw, 1, 40000 - VTI_AUDIO_RING + 1023); // the same safe window as read_at: the synth writes there next
    CHECK(raw[0] == 0);
}

TEST(audioin_usb3_and_peaks)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    int pk[6];
    CHECK(!a.raw_peaks(100, pk) && pk[0] == 0); // not streaming yet
    r.produce(6000);
    float l1[8], r1[8], l2[8], r2[8], l3[8], r3[8];
    // USB 3 ("3 Outs / No Input", E158) = ring channels 0/1, only when asked for
    a.read_at(l1, r1, l2, r2, 8, 5996, l3, r3);
    CHECK(l3[0] == Ring::sample(5996, 0) * K && r3[3] == Ring::sample(5999, 1) * K);
    CHECK(l3[4] == 0 && r3[7] == 0); // the future is silence on USB 3 too
    CHECK(l1[0] == Ring::sample(5996, 2) * K);
    a.read(l1, r1, l2, r2, 8, 1024, l3, r3);
    CHECK(l3[0] == Ring::sample(6000 - 1024, 0) * K && r3[7] == Ring::sample(6000 - 1024 + 7, 1) * K);
    a.read(l1, r1, l2, r2, 8, 1024); // without USB 3 as before
    CHECK(l1[0] == Ring::sample(6000 - 1024 + 8, 2) * K);
    // peaks per raw channel over the newest frames
    CHECK(a.raw_peaks(10, pk));
    int want = 0;
    for (int64_t f = 5990; f < 6000; f++)
        want = std::max(want, (int)Ring::sample(f, 5));
    CHECK(pk[5] == want && pk[0] > 0);
}

TEST(audioin_read_follows_the_synth_clock)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(10000);
    float l1[512], r1[512], l2[512], r2[512];
    a.read(l1, r1, l2, r2, 512, 1024); // first read: anchored `latency` behind the newest frame
    CHECK(l1[0] == Ring::sample(10000 - 1024, 2) * K);
    CHECK_EQ(a.position(), 10000 - 1024 + 512);
    CHECK_EQ(a.drops(), 0);
    // the synth runs ahead (distance 1024 + 300): one frame dropped per block, no jump
    r.produce(10000 + 512 + 300);
    int64_t before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 513);
    // the synth falls behind (distance 1024 - 300): one frame repeated per block (crossfaded since R5-3, not hard)
    r.produce(a.position() + 1024 - 300);
    before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 511);
    CHECK(l1[0] != l1[1]);
    // underrun (the reader caught up with the synth): re-anchored and counted
    a.read(l1, r1, l2, r2, 512, 1024);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 1);
    // a DAW stall: far behind -> re-anchored too
    r.produce(r.shm->in_frames + VTI_AUDIO_RING);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 2);
    CHECK_EQ(a.position(), r.shm->in_frames - 1024 + 512);
}

TEST(audioin_read_large_blocks)
{
    // E187: with a 4096-frame host block the fallback target 2 * 4096 + 256 = 8448 already exceeded the "far behind"
    // limit (half the ring) - every block re-anchored, audible as crackling until the timeline anchored
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    const int n = 4096, latency = 2 * n + 256;
    static float l1[n], r1[n], l2[n], r2[n];
    r.produce(20000);
    for (int i = 0; i < 40; i++) { // steady playback: the synth delivers one block per callback
        a.read(l1, r1, l2, r2, n, latency);
        r.produce(r.shm->in_frames + n);
    }
    CHECK_EQ(a.drops(), 0);
    // a real stall (the DAW stopped calling for a while) still re-anchors instead of playing old audio
    r.produce(r.shm->in_frames + VTI_AUDIO_RING / 2);
    a.read(l1, r1, l2, r2, n, latency);
    CHECK_EQ(a.drops(), 1);
    CHECK_EQ(a.position(), r.shm->in_frames - latency + n);
}

// blocks above 5376 frames: the fallback target 2 * n + 256 left no room in the ring (re-anchored every block), above
// 8064 it reached back past the ring's oldest frame (overwritten audio); the target is kept inside the ring
TEST(audioin_read_blocks_too_large_for_the_target)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    static float l1[8192], r1[8192], l2[8192], r2[8192];
    r.produce(40000);
    for (int i = 0; i < 20; i++) {
        a.read(l1, r1, l2, r2, 6144, 2 * 6144 + 256);
        r.produce(r.shm->in_frames + 6144);
    }
    CHECK_EQ(a.drops(), 0);
    AudioIn b;
    CHECK(b.open(r.name.c_str()));
    const int64_t newest = r.shm->in_frames;
    b.read(l1, r1, l2, r2, 8192, 2 * 8192 + 256);
    CHECK(l1[0] == Ring::sample(newest - 8192, 2) * K && l1[8191] == Ring::sample(newest - 1, 2) * K);
}

// R5-3 (research 2026-11-26): read() is the path before the sync marker and for stretches without one. Its drift steps
// skipped or repeated a frame hard (a tick each, ~4 a second at 90 ppm) and its re-anchors jumped hard; now each move
// fades from the old position over Crossfade::LEN frames, as the stamp-aligned path does (E579, E589)
TEST(audioin_read_steps_are_crossfaded)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(10000);
    float l1[512], r1[512], l2[512], r2[512];
    a.read(l1, r1, l2, r2, 512, 1024);
    auto near = [](float v, int64_t f) { return std::fabs(v - Ring::sample(f, 2) * K) < 1.0f * K; };
    // ahead: a frame skipped. The block starts where the last one ended and reaches the new position after the fade
    r.produce(10000 + 512 + 300);
    int64_t before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 513);
    CHECK(near(l1[0], before));
    CHECK(l1[Crossfade::LEN] == Ring::sample(before + 1 + Crossfade::LEN, 2) * K);
    CHECK(r2[Crossfade::LEN + 50] == Ring::sample(before + 1 + Crossfade::LEN + 50, 5) * K);
    // behind: a frame repeated, the same way
    r.produce(a.position() + 1024 - 300);
    before = a.position();
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.position(), before + 511);
    CHECK(near(l1[0], before) && near(l1[1], before + 1));
    CHECK(l1[Crossfade::LEN] == Ring::sample(before - 1 + Crossfade::LEN, 2) * K);
    // an underrun re-anchors back, faded too while the old position's frames are still there
    before = a.position();
    const int64_t newest = r.shm->in_frames;
    CHECK(newest - before < 512 && newest - before >= Crossfade::LEN);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 1);
    CHECK_EQ(a.position(), newest - 1024 + 512);
    CHECK(near(l1[0], before));
    CHECK(l1[Crossfade::LEN] == Ring::sample(newest - 1024 + Crossfade::LEN, 2) * K);
    // a DAW stall: the old position is gone from the ring, a hard re-anchor as before
    r.produce(r.shm->in_frames + VTI_AUDIO_RING);
    a.read(l1, r1, l2, r2, 512, 1024);
    CHECK_EQ(a.drops(), 2);
    CHECK(l1[0] == Ring::sample(r.shm->in_frames - 1024, 2) * K);
}

// below Crossfade::LEN frames per block a step per block restarted the fade half-way (half a frame's step again):
// the next step waits until the fade is over, as TimelineFollow::SPACE on the aligned path
TEST(audioin_read_small_blocks_step_once_per_fade)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    r.produce(10000);
    float l1[32], r1[32], l2[32], r2[32];
    a.read(l1, r1, l2, r2, 32, 768);
    int steps = 0;
    for (int i = 0; i < 8; i++) { // the synth stays 300 ahead of the target: a step is due in every block
        r.produce(a.position() + 768 + 300);
        const int64_t before = a.position();
        a.read(l1, r1, l2, r2, 32, 768);
        steps += a.position() - before != 32;
    }
    CHECK_EQ(steps, 8 * 32 / Crossfade::LEN);
    CHECK_EQ(a.drops(), 0);
}

// R5-1: the broker hands its USB audio over in 4 ms pieces now (176/177 frames, VTI_AUDIO_NP) instead of 10 ms ones
// (441). The newest frame's sawtooth then fits read()'s +-128 band: at equal clocks the reader settles and steps no
// more. With 441-frame pieces it never could (the band is narrower than the sawtooth): it stepped back and forth
static int64_t steps_at_equal_clocks(double piece_ms, int n)
{
    Ring r;
    AudioIn a;
    if (!r.shm || !a.open(r.name.c_str()))
        return -1;
    static float l1[512], r1[512], l2[512], r2[512];
    const double piece = 44.1 * piece_ms, t0 = 20000;
    r.produce((int64_t)t0);
    int64_t steps = 0;
    for (double t = t0; t < t0 + 44100.0 * 10; ) { // 10 s of host blocks; the first second may settle
        t += n;
        r.produce((int64_t)(t0 + std::floor((t - t0) / piece) * piece)); // only whole pieces have arrived
        const int64_t before = a.position();
        a.read(l1, r1, l2, r2, n, 768);
        if (t > t0 + 44100 && a.position() - before != n)
            steps++;
    }
    return a.drops() ? -1 : steps;
}

TEST(audioin_read_settles_with_4_ms_pieces)
{
    CHECK_EQ(steps_at_equal_clocks(VTI_AUDIO_NP, 32), 0);
    CHECK_EQ(steps_at_equal_clocks(VTI_AUDIO_NP, 64), 0);
    CHECK_EQ(steps_at_equal_clocks(VTI_AUDIO_NP, 256), 0);
    CHECK(steps_at_equal_clocks(10, 64) > 10); // the old 10 ms pieces
}

TEST(audioin_output_ring)
{
    Ring r;
    AudioIn a;
    CHECK(r.shm && a.open(r.name.c_str()));
    int16_t st[2 * 3] = {1, -1, 2, -2, 3, -3};
    a.write_out(st, 3);
    CHECK_EQ(a.out_queued(), 3);
    CHECK(r.shm->out[0] == 1 && r.shm->out[5] == -3);
    r.shm->out_read = 2; // the broker played two frames
    CHECK_EQ(a.out_queued(), 1);
    a.close();
    CHECK_EQ(a.out_queued(), 0);
    Ring v1(VTI_AUDIO_MAGIC, 1); // a version 1 ring has no output part
    AudioIn b;
    CHECK(v1.shm && b.open(v1.name.c_str()));
    b.write_out(st, 3);
    CHECK_EQ(b.out_queued(), 0);
}
#else
TEST(audioin_ring)
{
    SKIP("the test ring is POSIX shared memory (Linux)");
}
#endif
