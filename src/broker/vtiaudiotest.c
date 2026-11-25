// vtiaudiotest.c - test client of the broker audio (protocol 2, vtiaudioshm.h): registers like the plug-in,
// follows the shared input ring and prints frames/s against the PC clock, the largest gap between two events and
// the input peak. Several instances at once check the fan-out to more than one client.
// usage: vtiaudiotest [seconds] [tone|ramp]   (tone: 440 Hz sine at -12 dBFS into the output ring; ramp: frame counter)
#include "vticore.h"
#include "vtiaudioshm.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    (void)ep;
    (void)data;
    (void)len;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    vti *v = vti_open(on_rx, NULL);
    if (!v) {
        printf("no broker connection\n");
        return 1;
    }
    char name[64];
    snprintf(name, sizeof name, "Local\\vtiaudiotest_%lu", GetCurrentProcessId());
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, name);
    LARGE_INTEGER f, t0, t, last;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    int r = vti_audio(v, 1, name);
    QueryPerformanceCounter(&t);
    printf("vti_audio -> %d after %.0f ms\n", r, (double)(t.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
    if (r != 1)
        return 1;
    int ramp = argc > 2 && !strcmp(argv[2], "ramp"); // L = frame counter (& 0x7fff), R = its negation
    int tone = ramp || (argc > 2 && !strcmp(argv[2], "tone"));
    DWORD acc = tone ? FILE_MAP_READ | FILE_MAP_WRITE : FILE_MAP_READ;
    HANDLE map = OpenFileMappingA(acc, FALSE, VTI_AUDIO_SHM_NAME);
    vti_audio_shm *shm = map ? MapViewOfFile(map, acc, 0, 0, sizeof *shm) : NULL;
    double phase = 0;
    if (!shm || shm->magic != VTI_AUDIO_MAGIC) {
        printf("shared memory missing\n");
        return 1;
    }
    int64_t pos = shm->in_frames, start = pos;
    QueryPerformanceCounter(&t0);
    last = t0;
    double gap_max = 0;
    int peak = 0, events = 0, chpeak[VTI_AUDIO_IN_CH] = {0};
    for (int s = 1; s <= secs;) {
        DWORD w = WaitForSingleObject(ev, 500);
        QueryPerformanceCounter(&t);
        if (w == WAIT_OBJECT_0) {
            double gap = (double)(t.QuadPart - last.QuadPart) * 1000 / f.QuadPart;
            if (gap > gap_max)
                gap_max = gap;
            last = t;
            events++;
        }
        int64_t wr = shm->in_frames;
        if (tone) { // as many output frames as input frames arrived: same clock
            int64_t ow = shm->out_write;
            for (int64_t i = 0; i < wr - pos; i++) {
                int16_t smp = ramp ? (int16_t)((ow + i) & 0x7fff) : (int16_t)(8192 * sin(phase));
                phase = fmod(phase + 2 * 3.14159265358979 * 440.0 / 44100.0, 2 * 3.14159265358979);
                shm->out[((ow + i) & (VTI_AUDIO_RING - 1)) * 2] = smp;
                shm->out[((ow + i) & (VTI_AUDIO_RING - 1)) * 2 + 1] = smp;
            }
            InterlockedExchange64((volatile LONG64 *)&shm->out_write, ow + (wr - pos));
        }
        for (; pos < wr; pos++)
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++) {
                int x = abs(shm->in[(pos & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH + c]);
                if (x > peak)
                    peak = x;
                if (x > chpeak[c])
                    chpeak[c] = x;
            }
        double el = (double)(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el >= s) {
            printf("%3d s: %lld frames, %.1f frames/s, %d events, max gap %.1f ms, peak %d (ch0-5:", s,
                   (long long)(pos - start), (pos - start) / el, events, gap_max, peak);
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++) {
                printf(" %d", chpeak[c]);
                chpeak[c] = 0;
            }
            printf(")\n");
            fflush(stdout);
            gap_max = 0;
            peak = 0;
            events = 0;
            s++;
        }
    }
    printf("vti_audio off -> %d\n", vti_audio(v, 0, NULL));
    vti_close(v);
    return 0;
}
