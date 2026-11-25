// brokerharness.c - the POSIX broker's client message handling (src/broker/vtibroker_posix.c) without a Virus, for
// test_brokermsg.cpp (Linux, own binary brokertests): the whole broker is compiled in here so the tests reach its
// static client thread; the device (vticore.c), the libusb audio stream and the MIDI ports are stubs below, so
// nothing opens USB. A test serves one end of a socketpair as a broker client and talks protocol over the other.
#define main vtibroker_main
#include "../../src/broker/vtibroker_posix.c"
#undef main

static int g_fake_dev;
static int g_writes;              // g_cs_dev
static uint8_t g_last[VTIMSG_MAX]; // g_cs_dev: the last write's bytes
static int g_last_len;

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
int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len) // g_cs_dev held by the broker
{
    (void)v, (void)ep;
    g_writes++;
    memcpy(g_last, data, (size_t)len);
    g_last_len = len;
    return 0;
}
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
    g_writes = 0;
    pthread_mutex_unlock(&g_cs_dev);
}

void bh_serve(int fd) // as main() does for an accepted connection
{
    client *c = calloc(1, sizeof *c);
    c->fd = fd;
    pthread_mutex_init(&c->wr, NULL);
    pthread_t t;
    pthread_create(&t, NULL, client_thread, c);
    pthread_detach(t);
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
    pthread_mutex_lock(&g_cs_dev);
    int n = g_writes;
    memcpy(last, g_last, (size_t)g_last_len);
    *len = g_last_len;
    pthread_mutex_unlock(&g_cs_dev);
    return n;
}
