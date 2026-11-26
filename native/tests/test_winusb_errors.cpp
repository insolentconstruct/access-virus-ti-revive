// When the Windows broker's MIDI reader gives up on the device (src/broker/vtireaderr.h): a removed or vanished
// device must end the reader, so the broker closes it and opens the Virus again when it comes back. ERROR_NO_SUCH_DEVICE
// (433) was missing: the reader logged it every 10 ms and the broker stayed on a stale handle (sim S124, #36's log).
#include "check.h"
#include "vtireaderr.h"

TEST(winusb_reader_gives_up_on_a_gone_device)
{
    CHECK(vti_read_error_dead(22, 0));   // ERROR_BAD_COMMAND
    CHECK(vti_read_error_dead(1167, 0)); // ERROR_DEVICE_NOT_CONNECTED
    CHECK(vti_read_error_dead(6, 0));    // ERROR_INVALID_HANDLE
    CHECK(vti_read_error_dead(433, 0));  // ERROR_NO_SUCH_DEVICE
}

TEST(winusb_reader_keeps_reading_through_stalls)
{
    CHECK(!vti_read_error_dead(31, 1));  // ERROR_GEN_FAILURE: a stalled pipe, reset and go on
    CHECK(!vti_read_error_dead(31, 50));
    CHECK(vti_read_error_dead(31, 51));  // stalled beyond recovery
    CHECK(!vti_read_error_dead(995, 0)); // ERROR_OPERATION_ABORTED: a cancelled read, not a gone device
}

// The libusb reader (vticore.c, macOS and Linux) had no such rule: any error but NO_DEVICE was retried at once, so a
// stalled endpoint spun at ~25000 reads a second and filled the broker log (#37: 6.3 M LIBUSB_ERROR_PIPE lines in 2
// minutes on Catalina). Now as on Windows: a stall is cleared, every error waits, a run of errors ends the reader.
TEST(libusb_reader_gives_up_on_a_gone_device)
{
    CHECK(vti_libusb_read_dead(-4, 1));   // LIBUSB_ERROR_NO_DEVICE
    CHECK(!vti_libusb_read_dead(-9, 1));  // LIBUSB_ERROR_PIPE: clear the halt and go on
    CHECK(!vti_libusb_read_dead(-1, 50)); // LIBUSB_ERROR_IO, still in range
    CHECK(vti_libusb_read_dead(-9, 51));  // stalled beyond recovery: the broker closes and opens the Virus again
    CHECK(vti_libusb_read_dead(-99, 51)); // LIBUSB_ERROR_OTHER, the same
}

TEST(libusb_reader_clears_only_a_stall)
{
    CHECK(vti_libusb_stalled(-9));
    CHECK(!vti_libusb_stalled(-1));
    CHECK(!vti_libusb_stalled(-4));
}

TEST(reader_logs_only_the_start_of_an_error_run)
{
    CHECK(vti_read_error_logged(1));
    CHECK(vti_read_error_logged(3));
    CHECK(!vti_read_error_logged(4));
    CHECK(!vti_read_error_logged(51));
}

// The Windows reader loop (src/broker/winread.h, R3-4, R12-4): overlapped reads stay pending without a timeout, the
// oldest first, each resubmitted once its bytes went on; a failed read's bytes go on too (the old reader dropped what
// a timed-out read got, and with an F0 packet the whole SysEx); an error or the stop cancels and drains the rest.
#include "winread.h"
#include <string>
#include <vector>

namespace {
struct RdStep {
    int stop;               // 1 = the stop signal comes before this read finishes (only a stoppable wait)
    unsigned long got, err; // otherwise: what the read brought and its error (0 = none)
};
struct RdFake {
    std::vector<RdStep> fin;         // one per finish() call, in call order
    std::vector<unsigned long> sub;  // per submit() call: its error (default 0 = pending)
    std::vector<int> naps;           // per nap() call: 1 = the stop signal came (default 0)
    size_t f = 0, s = 0, n = 0;
    std::string trace;               // s<slot> submit, f<slot> finish, d<slot>:<got> deliver, C cancel, E<err>, R, N
};
RdFake *rd(void *c) { return (RdFake *)c; }
unsigned long rd_submit(void *c, int slot)
{
    RdFake &k = *rd(c);
    k.trace += "s" + std::to_string(slot) + " ";
    return k.s < k.sub.size() ? k.sub[k.s++] : (k.s++, 0ul);
}
int rd_finish(void *c, int slot, int stoppable, unsigned long *got, unsigned long *err)
{
    RdFake &k = *rd(c);
    k.trace += "f" + std::to_string(slot) + (stoppable ? " " : "! ");
    RdStep st = k.f < k.fin.size() ? k.fin[k.f++] : RdStep{1, 0, 0};
    if (st.stop && stoppable)
        return 0;
    *got = st.got, *err = st.err;
    if (st.stop) // a drain after a stop: the cancelled read ends with ERROR_OPERATION_ABORTED
        *got = 0, *err = 995;
    return 1;
}
void rd_cancel(void *c) { rd(c)->trace += "C "; }
void rd_deliver(void *c, int slot, unsigned long got) { rd(c)->trace += "d" + std::to_string(slot) + ":" + std::to_string(got) + " "; }
void rd_error(void *c, unsigned long err) { rd(c)->trace += "E" + std::to_string(err) + " "; }
void rd_reset(void *c) { rd(c)->trace += "R "; }
int rd_nap(void *c)
{
    RdFake &k = *rd(c);
    k.trace += "N ";
    return k.n < k.naps.size() ? k.naps[k.n++] : (k.n++, 0);
}
bool trace_is(const RdFake &k, const char *want) // prints the trace that came on a mismatch
{
    if (k.trace == want)
        return true;
    std::fprintf(stderr, "  trace: %s\n", k.trace.c_str());
    return false;
}
int rd_run(RdFake &k, int slots = VTI_RD_SLOTS)
{
    vti_rd_ops o = {&k, slots, rd_submit, rd_finish, rd_cancel, rd_deliver, rd_error, rd_reset, rd_nap};
    return vti_rd_run(&o);
}
} // namespace

TEST(winread_keeps_two_reads_pending_and_resubmits_oldest_first)
{
    RdFake k;
    k.fin = {{0, 4, 0}, {0, 8, 0}, {0, 4, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
    CHECK_EQ(rd_run(k), VTI_RD_STOP);
    // both out at once, each read's bytes go on before it is asked again, never a read left unasked
    CHECK(trace_is(k, "s0 s1 f0 d0:4 s0 f1 d1:8 s1 f0 d0:4 s0 f1 C f1! f0! "));
}

TEST(winread_one_slot_reads_one_at_a_time)
{
    RdFake k; // the Snow's bulk EP 0x85: one read, as before (two would double its idle reads, #42)
    k.fin = {{0, 4, 0}, {0, 4, 0}, {1, 0, 0}, {1, 0, 0}};
    CHECK_EQ(rd_run(k, 1), VTI_RD_STOP);
    CHECK(trace_is(k, "s0 f0 d0:4 s0 f0 d0:4 s0 f0 C f0! "));
}

TEST(winread_hands_on_the_bytes_of_a_timed_out_read)
{
    RdFake k; // ERROR_SEM_TIMEOUT (121) with a packet in it: the packet goes on, the read is simply asked again
    k.fin = {{0, 4, 121}, {0, 4, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
    CHECK_EQ(rd_run(k), VTI_RD_STOP);
    CHECK(trace_is(k, "s0 s1 f0 d0:4 s0 f1 d1:4 s1 f0 C f0! f1! "));
}

TEST(winread_hands_on_a_failed_reads_bytes_then_clears_the_stall)
{
    RdFake k; // a stall (31) after a packet: the packet first, then cancel, drain, log, clear the halt, wait, again
    k.fin = {{0, 4, 31}, {0, 0, 995}, {0, 4, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}};
    CHECK_EQ(rd_run(k), VTI_RD_STOP);
    CHECK(trace_is(k, "s0 s1 f0 d0:4 C f1! E31 R N s0 s1 f0 d0:4 s0 f1 C f1! f0! "));
}

TEST(winread_drains_pending_reads_on_stop_and_keeps_their_bytes)
{
    RdFake k; // the stop comes while slot 0 waits; the cancel ends it with a packet already in it
    k.fin = {{1, 0, 0}, {0, 4, 995}, {0, 0, 995}};
    CHECK_EQ(rd_run(k), VTI_RD_STOP);
    CHECK(trace_is(k, "s0 s1 f0 C f0! d0:4 f1! "));
}

TEST(winread_gives_up_on_a_gone_device)
{
    RdFake k;
    k.fin = {{0, 0, 433}, {0, 0, 433}};
    CHECK_EQ(rd_run(k), VTI_RD_DEAD);
    CHECK(trace_is(k, "s0 s1 f0 C f1! E433 "));
    RdFake g; // a read that cannot even start on a removed device: no wait for it
    g.sub = {22, 22};
    CHECK_EQ(rd_run(g), VTI_RD_DEAD);
    CHECK(trace_is(g, "s0 s1 C E22 "));
}

TEST(winread_gives_up_after_51_stalls_in_a_row_only)
{
    RdFake k; // 51 stalls, each with its drained partner: dead at the 51st, not before
    for (int i = 0; i < 51; i++)
        k.fin.push_back({0, 0, 31}), k.fin.push_back({0, 0, 995});
    CHECK_EQ(rd_run(k), VTI_RD_DEAD);
    RdFake g; // a good read in between starts the count again
    for (int i = 0; i < 50; i++)
        g.fin.push_back({0, 0, 31}), g.fin.push_back({0, 0, 995});
    g.fin.push_back({0, 4, 0});
    for (int i = 0; i < 50; i++)
        g.fin.push_back({0, 0, 31}), g.fin.push_back({0, 0, 995});
    CHECK_EQ(rd_run(g), VTI_RD_STOP); // the script ends in stops
}

TEST(winread_stops_while_it_waits_after_an_error)
{
    RdFake k;
    k.fin = {{0, 0, 995}, {0, 0, 995}};
    k.naps = {1};
    CHECK_EQ(rd_run(k), VTI_RD_STOP);
    CHECK(trace_is(k, "s0 s1 f0 C f1! E995 N "));
}

// R12-7: a client that stops reading (a frozen DAW) used to be dropped when its 256 KB queue filled; the original
// drops the one 32-byte transfer. Now a device packet (RX) is dropped and counted, anything else still ends the client
// (a missing reply would leave it waiting); an observer loses any message, as before.
TEST(full_client_queue_drops_the_packet_not_the_client)
{
    CHECK(!vti_queue_full_drops_client(0, VTIMSG_RX));
    CHECK(vti_queue_full_drops_client(0, VTIMSG_CONTROL_REPLY));
    CHECK(vti_queue_full_drops_client(0, VTIMSG_STATE));
    CHECK(!vti_queue_full_drops_client(1, VTIMSG_CONTROL_REPLY)); // an observer only loses the message
}

TEST(dropped_packets_are_logged_at_powers_of_two)
{
    CHECK(!vti_drop_logged(0));
    CHECK(vti_drop_logged(1));
    CHECK(vti_drop_logged(2));
    CHECK(!vti_drop_logged(3));
    CHECK(vti_drop_logged(1024));
    CHECK(!vti_drop_logged(1025));
}
