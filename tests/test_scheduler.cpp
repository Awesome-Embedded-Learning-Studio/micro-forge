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

} // namespace
