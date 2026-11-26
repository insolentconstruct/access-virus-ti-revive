// One parameter message per value during a mouse drag (dragsend.h, review 2026-10-19): the editor sent one on every
// mouse move, also inside one value step; for parameters that are not automatable they go straight into the queue
// to the Virus, which the byte budget does not cover - hundreds per second from a fast mouse.
#include "check.h"
#include "editor/dragsend.h"

using namespace vti;

TEST(dragsend_same_value_is_sent_once)
{
    DragSend d;
    int sent = 0;
    for (int move = 0; move < 500; move++) // 500 mouse moves within one value step
        sent += d.changed("Filter/Cutoff", 0, 64);
    CHECK(sent == 1);
}

TEST(dragsend_every_new_value_is_sent)
{
    DragSend d;
    int sent = 0;
    for (int v = 0; v <= 127; v++)
        for (int move = 0; move < 3; move++)
            sent += d.changed("Filter/Cutoff", 0, v);
    CHECK(sent == 128);
}

TEST(dragsend_other_parameter_or_part_is_new)
{
    DragSend d;
    CHECK(d.changed("Arp/Step 1 Velocity", 0, 100));
    CHECK(d.changed("Arp/Step 2 Velocity", 0, 100)); // the drag moved to the next step
    CHECK(d.changed("Arp/Step 2 Velocity", 1, 100)); // another part
    CHECK(!d.changed("Arp/Step 2 Velocity", 1, 100));
}

TEST(dragsend_reset_at_a_new_drag)
{
    DragSend d;
    CHECK(d.changed("Filter/Cutoff", 0, 64));
    d.reset(); // mouse down: the first value of a new drag always goes out (a click sets it like before)
    CHECK(d.changed("Filter/Cutoff", 0, 64));
}

TEST(dragsend_two_parameters_in_one_drag) // review 2026-11-25 E2: an x+y graph handle or the arp step corner
{
    DragSend d;
    int sent = 0;
    for (int move = 0; move < 500; move++) { // both values stay inside one step
        sent += d.changed("Filter/Env Attack", 0, 40);
        sent += d.changed("Filter/Env Decay", 0, 70);
    }
    CHECK(sent == 2);
}
