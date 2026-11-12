#include <gtest/gtest.h>

#include <micro_forge/kernel/scheduler.hpp>

#include <vector>

namespace {

using micro_forge::kernel::Scheduler;
using micro_forge::kernel::Tick;

TEST(SchedulerTest, FiresInDeadlineOrder) {
    Scheduler scheduler;
    std::vector<int> fired;

    scheduler.schedule(30, [&] { fired.push_back(30); });
    scheduler.schedule(10, [&] { fired.push_back(10); });
    scheduler.schedule(20, [&] { fired.push_back(20); });

    EXPECT_FALSE(scheduler.empty());
    ASSERT_EQ(std::optional<Tick>(10), scheduler.next_deadline());
    scheduler.dispatch(30);
    EXPECT_EQ((std::vector<int>{10, 20, 30}), fired);
    EXPECT_TRUE(scheduler.empty());
}

TEST(SchedulerTest, SameDeadlineFiresInScheduleOrder) {
    Scheduler scheduler;
    std::vector<char> fired;

    scheduler.schedule(5, [&] { fired.push_back('a'); });
    scheduler.schedule(5, [&] { fired.push_back('b'); });
    scheduler.schedule(5, [&] { fired.push_back('c'); });

    scheduler.dispatch(5);
    EXPECT_EQ((std::vector<char>{'a', 'b', 'c'}), fired);
}

TEST(SchedulerTest, DispatchFiresOnlyDueEvents) {
    Scheduler scheduler;
    std::vector<int> fired;

    scheduler.schedule(10, [&] { fired.push_back(10); });
    scheduler.schedule(11, [&] { fired.push_back(11); });

    scheduler.dispatch(10);
    EXPECT_EQ((std::vector<int>{10}), fired);
    EXPECT_FALSE(scheduler.empty());
    ASSERT_EQ(std::optional<Tick>(11), scheduler.next_deadline());

    scheduler.dispatch(11);
    EXPECT_EQ((std::vector<int>{10, 11}), fired);
}

TEST(SchedulerTest, RescheduledEventJoinsSamePass) {
    Scheduler scheduler;
    std::vector<int> fired;

    scheduler.schedule(1, [&] {
        fired.push_back(1);
        // Deadline equal to "now" of the running pass joins it.
        scheduler.schedule(1, [&] { fired.push_back(2); });
    });

    scheduler.dispatch(1);
    EXPECT_EQ((std::vector<int>{1, 2}), fired);
}

TEST(SchedulerTest, IdenticalRunsProduceIdenticalTraces) {
    // Batch-1 acceptance shape: the same schedule/dispatch sequence must
    // yield a bit-identical event trace on every run.
    const auto run = [] {
        Scheduler scheduler;
        std::vector<int> trace;
        for (int i = 0; i < 100; ++i) {
            const Tick at = (static_cast<Tick>(i) * 37) % 91;
            scheduler.schedule(at, [&trace, at] { trace.push_back(static_cast<int>(at)); });
        }
        for (Tick now = 0; now <= 90; ++now) {
            scheduler.dispatch(now);
        }
        return trace;
    };

    const auto first = run();
    const auto second = run();
    ASSERT_EQ(first.size(), 100U);
    EXPECT_EQ(first, second);
}

TEST(SchedulerTest, EmptySchedulerHasNoDeadline) {
    Scheduler scheduler;
    EXPECT_TRUE(scheduler.empty());
    EXPECT_EQ(std::nullopt, scheduler.next_deadline());
}

TEST(SchedulerTest, CancelledEventNeverFires) {
    Scheduler scheduler;
    std::vector<int> fired;
    const auto doomed = scheduler.schedule(10, [&] { fired.push_back(10); });
    scheduler.schedule(10, [&] { fired.push_back(11); }); // same deadline, later seq
    scheduler.schedule(20, [&] { fired.push_back(20); });
    scheduler.cancel(doomed);
    EXPECT_FALSE(scheduler.empty()); // two live events remain
    ASSERT_EQ(std::optional<Tick>(10), scheduler.next_deadline());
    scheduler.dispatch(30);
    EXPECT_EQ((std::vector<int>{11, 20}), fired);
    EXPECT_TRUE(scheduler.empty());
}

TEST(SchedulerTest, CancelIsNoOpOnFiredOrDoubleCancel) {
    Scheduler scheduler;
    bool fired = false;
    const auto once = scheduler.schedule(5, [&] { fired = true; });
    scheduler.dispatch(10);
    ASSERT_TRUE(fired);
    scheduler.cancel(once);
    scheduler.cancel(once);
    scheduler.dispatch(20);
    EXPECT_TRUE(scheduler.empty());
}

TEST(SchedulerTest, PeriodicEventRearmsUntilCancelled) {
    Scheduler scheduler;
    unsigned fired = 0;
    const auto chain = scheduler.schedule_periodic(10, 5, [&] { ++fired; });
    scheduler.dispatch(22); // deadlines 10, 15, 20 all pass
    EXPECT_EQ(3U, fired);
    const auto live = scheduler.live_events();
    ASSERT_EQ(1U, live.size()); // rearmed, pending at 25
    EXPECT_EQ(25U, live[0].at);
    EXPECT_EQ(5U, live[0].period);

    scheduler.cancel(chain);
    scheduler.dispatch(100);
    EXPECT_EQ(3U, fired); // the chain is stopped
    EXPECT_TRUE(scheduler.empty());
}

TEST(SchedulerTest, ZeroPeriodDegradesToOneNotSpin) {
    Scheduler scheduler;
    unsigned fired = 0;
    (void)scheduler.schedule_periodic(10, 0, [&] { ++fired; });
    scheduler.dispatch(11); // fires at 10, rearms at 11, both due
    EXPECT_EQ(2U, fired);
    EXPECT_FALSE(scheduler.empty());
}

TEST(SchedulerTest, RestoreEventKeepsIdentityAndOrder) {
    Scheduler original;
    (void)original.schedule(30, [] {});
    (void)original.schedule_periodic(10, 20, [] {});
    const auto saved = original.live_events();
    const auto saved_next = original.next_seq();
    ASSERT_EQ(2U, saved.size());
    EXPECT_EQ(10U, saved[0].at);
    EXPECT_EQ(20U, saved[0].period);
    EXPECT_EQ(30U, saved[1].at);
    EXPECT_EQ(0U, saved[1].period);

    Scheduler restored;
    for (const auto& info : saved) {
        restored.restore_event(info, [] {});
    }
    const auto after = restored.live_events();
    ASSERT_EQ(saved.size(), after.size());
    for (size_t i = 0; i < saved.size(); ++i) {
        EXPECT_EQ(saved[i].at, after[i].at);
        EXPECT_EQ(saved[i].seq, after[i].seq);
        EXPECT_EQ(saved[i].period, after[i].period);
    }
    EXPECT_EQ(saved_next, restored.next_seq()); // determinism: identical seq stream
}

} // namespace
