#include "UnrealVoxelSim/Simulation/FixedStep/Controller.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace UnrealVoxelSim::Simulation::FixedStep
{
namespace
{

class RecordingPipeline final : public Api::ITickPipeline
{
  public:
    void Step(const Api::StepContext context) override
    {
        Contexts.push_back(context);
        State = State * 1664525U + static_cast<std::uint32_t>(context.Tick.Value()) + 1013904223U;
    }

    std::vector<Api::StepContext> Contexts;
    std::uint32_t State{};
};

TEST(ControllerTest, RejectsInvalidDuration)
{
    RecordingPipeline pipeline;

    EXPECT_THROW(static_cast<void>(Controller{pipeline, Api::StepDuration{}}), std::invalid_argument);
}

TEST(ControllerTest, ExplicitStepInvokesCompositionPipelineInTickOrder)
{
    RecordingPipeline pipeline;
    Controller controller{pipeline};

    ASSERT_TRUE(controller.Step(Api::TickCount{3}));

    ASSERT_EQ(pipeline.Contexts.size(), 3U);
    EXPECT_EQ(pipeline.Contexts[0].Tick, Api::TickIndex{0});
    EXPECT_EQ(pipeline.Contexts[1].Tick, Api::TickIndex{1});
    EXPECT_EQ(pipeline.Contexts[2].Tick, Api::TickIndex{2});
    EXPECT_EQ(pipeline.Contexts[0].Duration, Api::StandardStepDuration);
    EXPECT_EQ(controller.CurrentTick(), Api::TickIndex{3});
}

TEST(ControllerTest, AccumulatesFrameSlicesWithoutLosingTime)
{
    RecordingPipeline pipeline;
    Controller controller{pipeline};

    ASSERT_TRUE(controller.Advance(std::chrono::nanoseconds{333'333'333}, Api::TickCount{100}));
    ASSERT_TRUE(controller.Advance(std::chrono::nanoseconds{333'333'333}, Api::TickCount{100}));
    const auto final = controller.Advance(std::chrono::nanoseconds{333'333'334}, Api::TickCount{100});

    ASSERT_TRUE(final);
    EXPECT_EQ(controller.CurrentTick(), Api::TickIndex{50});
    EXPECT_EQ(final->Pending, Api::TickCount{0});
}

TEST(ControllerTest, AppliesRationalDilation)
{
    RecordingPipeline pipeline;
    Controller controller{pipeline};
    controller.SetRate(*Api::Rate::Create(1, 2));

    const auto result = controller.Advance(std::chrono::seconds{1}, Api::TickCount{100});

    ASSERT_TRUE(result);
    EXPECT_EQ(result->Executed, Api::TickCount{25});
}

TEST(ControllerTest, AppliesHundredTimesCompression)
{
    RecordingPipeline pipeline;
    Controller controller{pipeline};
    controller.SetRate(*Api::Rate::Create(100, 1));

    const auto result = controller.Advance(std::chrono::milliseconds{20}, Api::TickCount{200});

    ASSERT_TRUE(result);
    EXPECT_EQ(result->Executed, Api::TickCount{100});
}

TEST(ControllerTest, RetainsTicksBeyondInteractiveBudget)
{
    RecordingPipeline pipeline;
    Controller controller{pipeline};
    controller.SetRate(*Api::Rate::Create(100, 1));

    const auto first = controller.Advance(std::chrono::milliseconds{20}, Api::TickCount{8});
    const auto second = controller.Advance(std::chrono::nanoseconds{0}, Api::TickCount{100});

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->Executed, Api::TickCount{8});
    EXPECT_EQ(first->Pending, Api::TickCount{92});
    EXPECT_EQ(second->Executed, Api::TickCount{92});
    EXPECT_EQ(controller.CurrentTick(), Api::TickIndex{100});
}

TEST(ControllerTest, DifferentRatesProduceIdenticalStateAtSameTick)
{
    RecordingPipeline normalPipeline;
    RecordingPipeline compressedPipeline;
    Controller normal{normalPipeline};
    Controller compressed{compressedPipeline};
    compressed.SetRate(*Api::Rate::Create(100, 1));

    ASSERT_TRUE(normal.Advance(std::chrono::seconds{2}, Api::TickCount{100}));
    ASSERT_TRUE(compressed.Advance(std::chrono::milliseconds{20}, Api::TickCount{100}));

    EXPECT_EQ(normal.CurrentTick(), compressed.CurrentTick());
    EXPECT_EQ(normalPipeline.State, compressedPipeline.State);
    EXPECT_EQ(normalPipeline.Contexts.size(), compressedPipeline.Contexts.size());
}

}
}
