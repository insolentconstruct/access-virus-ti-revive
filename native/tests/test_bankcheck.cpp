// BankCheck (bankcheck.h): when the plug-in checks the Virus' RAM banks again, like the original at every connection
// (research R6-1, R10-2, R2-3). The sequences below follow the plug-in's threads: the readiness probe asks, the sync
// thread's poll starts the check once the Virus is ready and no RAM job runs, the check takes the requests before it.
#include "check.h"
#include "device/answerwatch.h"
#include "device/bankcheck.h"

using namespace vti;

TEST(bankcheck_nothing_without_a_request)
{
    BankCheck c;
    CHECK(!c.due(true, false, false));
}

TEST(bankcheck_runs_again_after_a_reconnect)
{
    BankCheck c;
    c.request(); // the probe at the first start
    c.begin();   // the first sync covers it
    CHECK(!c.due(true, false, false));
    // switched off and on: the reconnect thread probes (ready false, notes held), then waits for the sync marker
    CHECK(!c.due(false, true, false));
    c.request(); // the probe answered
    CHECK(!c.due(true, true, false)); // the marker wait: not yet, like the first start
    CHECK(c.due(true, false, false));
    c.begin();
    CHECK(!c.due(true, false, false)); // once
}

TEST(bankcheck_runs_when_the_virus_appears_after_the_start)
{
    BankCheck c;
    c.begin(); // the first sync found no Virus and returned
    CHECK(!c.due(true, false, false));
    c.request(); // switched on later: its first probe answered (absent_at_start_ -> after_reconnect)
    CHECK(c.due(true, false, false));
}

TEST(bankcheck_a_request_during_a_check_runs_it_once_more)
{
    BankCheck c;
    c.request();
    c.begin(); // the check runs ...
    c.request(); // ... the Virus went away (the check stopped) and came back: its probe answered
    CHECK(c.due(true, false, false));
}

TEST(bankcheck_waits_for_a_ram_job)
{
    BankCheck c;
    c.request();
    CHECK(!c.due(true, false, true)); // a download or a RAM write reads the same slots and writes the cache back
    CHECK(c.due(true, false, false)); // the request stays until it has ended
}

// R2-3: a sync that ended because the Virus did not answer sends the cables again and probes; an answer resumes the
// check. Bounded by AnswerWatch: one re-send per connection, so a second miss warns and asks for no further check.
TEST(bankcheck_a_missed_sync_resumes_once_after_the_answer)
{
    BankCheck c;
    AnswerWatch w;
    c.begin();
    CHECK(w.missed() == AnswerWatch::RESEND); // the sync's bank info went unanswered
    c.request();                                // the probe after the re-send answered
    w.answered();
    CHECK(c.due(true, false, false));
    c.begin();
    CHECK(w.missed() == AnswerWatch::WARN); // the check missed again: a warning, no probe, so no request
    CHECK(!c.due(true, false, false));
}
