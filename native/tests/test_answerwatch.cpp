// AnswerWatch (answerwatch.h): what the plug-in does when the Virus does not answer a request, like the original
// (E591): re-send the reply-cable settings once per connection, then warn; an answer clears the warning.
#include "check.h"
#include "device/answerwatch.h"

using namespace vti;
using B = std::vector<uint8_t>;

TEST(answerwatch_resends_once_then_warns)
{
    AnswerWatch w;
    CHECK(w.missed() == AnswerWatch::RESEND);
    CHECK(!w.warned());
    CHECK(w.missed() == AnswerWatch::WARN);
    CHECK(w.warned());
    CHECK(w.missed() == AnswerWatch::NONE); // the warning stays, nothing more is sent
}

TEST(answerwatch_resends_once_per_connection)
{
    AnswerWatch w;
    CHECK(w.missed() == AnswerWatch::RESEND);
    w.answered(); // the re-send helped
    CHECK(w.missed() == AnswerWatch::WARN); // a later miss on the same connection: no second re-send
}

TEST(answerwatch_answer_clears_the_warning)
{
    AnswerWatch w;
    w.answered();
    CHECK(!w.warned());
    w.missed();
    w.missed();
    CHECK(w.warned());
    w.answered();
    CHECK(!w.warned());
    CHECK(w.missed() == AnswerWatch::WARN); // a new miss warns again
}

TEST(answerwatch_reset_on_a_new_connection)
{
    AnswerWatch w;
    w.missed();
    w.missed();
    w.reset();
    CHECK(!w.warned());
    CHECK(w.missed() == AnswerWatch::RESEND);
}

TEST(answerwatch_routing_messages_are_the_cable_globals)
{
    // the start-up list (GlobalInit from the user's files, then the display and the parts): only the link and the three
    // reply-cable settings, unchanged (E591, E395, E601)
    const std::vector<B> startup = {
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x7A, 0x03, 0xF7}, // Mode REMOTE
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3B, 0x01, 0xF7}, // MIDI Data cable
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x52, 0x01, 0xF7}, // USB MidiPort active: the link
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3C, 0x02, 0xF7}, // Panel Data cable
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3A, 0x03, 0xF7}, // Sounddump cable
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, 0x3A, 0x03, 0xF7}, // a part parameter with the same index
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3A, 0xF7},       // too short
    };
    const auto r = AnswerWatch::routing_messages(startup);
    CHECK_EQ(r.size(), 4u);
    // the link first (with 0x52 = 0 the firmware forces the cables back to 1, R9-1), then the cables in start-up order
    CHECK(r.size() == 4 && r[0] == startup[2] && r[1] == startup[1] && r[2] == startup[3] && r[3] == startup[4]);
    CHECK(AnswerWatch::routing_messages({}).empty());
}

TEST(answerwatch_routing_messages_only_the_users_link_on)
{
    // 0x52 goes out again only as the user's own start-up message with the value 1: never a link off, never one
    // built here; without it in the start-up list only the cables go out (rule 2)
    const std::vector<B> startup = {
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3A, 0x03, 0xF7},
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x52, 0x00, 0xF7}, // link off: not re-sent
        {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, 0x52, 0x01, 0xF7}, // a part parameter 0x52: not the global
    };
    const auto r = AnswerWatch::routing_messages(startup);
    CHECK_EQ(r.size(), 1u);
    CHECK(r.size() == 1 && r[0] == startup[0]);
}
