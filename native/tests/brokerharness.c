// brokerharness.c - the POSIX broker's client message handling (src/broker/vtibroker_posix.c) without a Virus, for
// test_brokermsg.cpp (Linux, own binary brokertests): the whole broker is compiled in here so the tests reach its
// static client thread; the device (vticore.c), the libusb audio stream and the MIDI ports are stubs below, so
// nothing opens USB. A test serves one end of a socketpair as a broker client and talks protocol over the other.
#define main vtibroker_main
#include "../../src/broker/vtibroker_posix.c"
#undef main

static int g_fake_dev;
// the stub device's own lock: the broker no longer holds g_cs_dev while it writes (T5)
static pthread_mutex_t g_bh = PTHREAD_MUTEX_INITIALIZER;
static int g_writes;               // g_bh
static uint8_t g_last[VTIMSG_MAX]; // g_bh: the last write's bytes
static int g_last_len;
static int g_busy;     // g_bh: 1 = the "Virus" refuses OUT packets, a write waits until 0 (or keep() says stop)
static int g_inside;   // g_bh: writes waiting inside the device now
static int g_stopped;  // g_bh: writes that ended because keep() said stop

vti *vti_open(vti_rx_cb cb, void *user)
{
    (void)cb, (void)user;
    return NULL;
}
void vti_close(vti *v) { (void)v; }
int vti_is_dead(vti *v) { return v == NULL; }
int vti_link(vti *v, int on) { return (void)v, (void)on, 0; }
int vti_linked(vti *v) { return (void)v, 1; }
int vti_reenumerate(vti *v) { return (void)v, -1; }
void vti_libusb(vti *v, struct libusb_context **ctx, struct libusb_device_handle **h)
{
    (void)v;
    *ctx = NULL, *h = NULL;
}
void vti_set_tap(vti_tap_cb cb) { (void)cb; }
// counts the write on entry, then waits while g_busy, asking keep() every ms like the retry loop does per timeout
int vti_write_keep(vti *v, uint8_t ep, const uint8_t *data, int len, vti_keep_fn keep, void *u)
{
    (void)v, (void)ep;
    pthread_mutex_lock(&g_bh);
    g_writes++;
    memcpy(g_last, data, (size_t)len);
    g_last_len = len;
    g_inside++;
    int r = 0;
    while (g_busy) {
        pthread_mutex_unlock(&g_bh);
        int go = !keep || keep(u);
        usleep(1000);
        pthread_mutex_lock(&g_bh);
        if (!go) {
            g_stopped++;
            r = -3;
            break;
        }
    }
    g_inside--;
    pthread_mutex_unlock(&g_bh);
    return r;
}
int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len) { return vti_write_keep(v, ep, data, len, NULL, NULL); }
int vti_control(vti *v, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len)
{
    (void)v, (void)rt, (void)req, (void)val, (void)idx, (void)d, (void)len;
    return -1;
}
int vla_start(struct libusb_context *ctx, struct libusb_device_handle *h, uint32_t rate, vla_in_fn in, vla_out_fn out,
              void *user)
{
    (void)ctx, (void)h, (void)rate, (void)in, (void)out, (void)user;
    return -1;
}
void vla_stop(void) {}
int vla_running(void) { return 0; }
int vla_rate_lost(void) { return 0; }
int vla_switch_rate(uint32_t rate, void (*before_start)(uint32_t rate)) { return (void)rate, (void)before_start, -2; }
uint32_t vla_rate(void) { return 44100; }
void vti_keyport_start(void) {}
void vti_keyport_send(const uint8_t *msg, int n) { (void)msg, (void)n; }
void vti_dinport_send(const uint8_t *msg, int n) { (void)msg, (void)n; }
void vti_keyport_log_counts(void) {}

// ---- what the tests call ----
void bh_device(int open) // a "Virus" for the client thread: only vti_write above is ever called on it
{
    pthread_mutex_lock(&g_cs_dev);
    g_dev = open ? (vti *)&g_fake_dev : NULL;
    pthread_mutex_unlock(&g_cs_dev);
    pthread_mutex_lock(&g_bh);
    g_writes = g_stopped = 0;
    pthread_mutex_unlock(&g_bh);
}

void bh_busy(int on) // the "Virus" refuses (1) or takes (0) OUT packets
{
    pthread_mutex_lock(&g_bh);
    g_busy = on;
    pthread_mutex_unlock(&g_bh);
}

int bh_inside(void)
{
    pthread_mutex_lock(&g_bh);
    int n = g_inside;
    pthread_mutex_unlock(&g_bh);
    return n;
}

int bh_stopped(void)
{
    pthread_mutex_lock(&g_bh);
    int n = g_stopped;
    pthread_mutex_unlock(&g_bh);
    return n;
}

void bh_serve(int fd) // as main() does for an accepted connection
{
    client *c = calloc(1, sizeof *c);
    c->fd = fd;
    pthread_mutex_init(&c->wr, NULL);
    pthread_t t;
    pthread_create(&t, NULL, client_thread, c);
    pthread_detach(t);
    // T22: return once the thread holds its slot, so tests see clients in their slots in creation order (before T23
    // the next owner was chosen by slot, and a late slot handed the Virus to the wrong one: 2 s, -1).
    for (int i = 0; i < 2000; i++) {
        int in = 0;
        pthread_mutex_lock(&g_cs_clients);
        for (int k = 0; k < MAX_CLIENTS; k++)
            in |= g_clients[k] == c;
        pthread_mutex_unlock(&g_cs_clients);
        if (in)
            return;
        usleep(1000);
    }
}

int bh_clients(void)
{
    int n = 0;
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    pthread_mutex_unlock(&g_cs_clients);
    return n;
}

int bh_observers(void) { return __atomic_load_n(&g_obs_count, __ATOMIC_SEQ_CST); }

int bh_writes(uint8_t *last, int *len) // the number of writes that reached the Virus, and the last one
{
    pthread_mutex_lock(&g_bh);
    int n = g_writes;
    memcpy(last, g_last, (size_t)g_last_len);
    *len = g_last_len;
    pthread_mutex_unlock(&g_bh);
    return n;
}
