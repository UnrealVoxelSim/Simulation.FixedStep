#include "UnrealVoxelSim/Simulation/FixedStep/Controller.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>

namespace UnrealVoxelSim::Simulation::FixedStep
{

class Controller::Impl final
{
  public:
    Impl(Api::ITickPipeline &pipeline, const Api::StepDuration duration) : Pipeline(pipeline), Duration(duration)
    {
    }

    void AssertOwnerThread() const noexcept
    {
        assert(std::this_thread::get_id() == OwnerThread);
    }

    Api::ITickPipeline &Pipeline;
    Api::StepDuration Duration;
    Api::TickIndex Tick;
    Api::Rate Rate{Api::NormalRate};
    std::uint64_t PendingNanoseconds{};
    std::uint64_t RateRemainder{};
    std::thread::id OwnerThread{std::this_thread::get_id()};
};

Controller::Controller(Api::ITickPipeline &pipeline, const Api::StepDuration duration)
{
    if (!duration.IsValid())
    {
        throw std::invalid_argument{"A fixed simulation step must have a positive duration."};
    }
    Impl_ = std::make_unique<Impl>(pipeline, duration);
}

Controller::~Controller() = default;

Api::TickIndex Controller::CurrentTick() const noexcept
{
    Impl_->AssertOwnerThread();
    return Impl_->Tick;
}

std::expected<void, Api::StepError> Controller::Step(const Api::TickCount count)
{
    Impl_->AssertOwnerThread();
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (count.Value() > maximum - Impl_->Tick.Value())
    {
        return std::unexpected{Api::StepError::TickOverflow};
    }

    for (std::uint64_t index = 0; index < count.Value(); ++index)
    {
        Impl_->Pipeline.Step(Api::StepContext{Impl_->Tick, Impl_->Duration});
        Impl_->Tick.Advance();
    }
    return {};
}

Api::Rate Controller::CurrentRate() const noexcept
{
    Impl_->AssertOwnerThread();
    return Impl_->Rate;
}

void Controller::SetRate(const Api::Rate rate) noexcept
{
    Impl_->AssertOwnerThread();
    const auto oldDenominator = static_cast<std::uint64_t>(Impl_->Rate.Denominator());
    const auto newDenominator = static_cast<std::uint64_t>(rate.Denominator());
    Impl_->RateRemainder = Impl_->RateRemainder * newDenominator / oldDenominator;
    Impl_->Rate = rate;
}

std::expected<Api::AdvanceResult, Api::AdvanceError> Controller::Advance(const std::chrono::nanoseconds elapsed,
                                                                         const Api::TickCount maximumTicks)
{
    Impl_->AssertOwnerThread();
    if (elapsed.count() < 0)
    {
        return std::unexpected{Api::AdvanceError::NegativeElapsedTime};
    }

    const auto denominator = static_cast<std::uint64_t>(Impl_->Rate.Denominator());
    const auto numerator = static_cast<std::uint64_t>(Impl_->Rate.Numerator());
    const auto elapsedNanoseconds = static_cast<std::uint64_t>(elapsed.count());
    const auto quotient = elapsedNanoseconds / denominator;
    const auto remainder = elapsedNanoseconds % denominator;
    constexpr auto Maximum = std::numeric_limits<std::uint64_t>::max();

    if (numerator != 0 && quotient > Maximum / numerator)
    {
        return std::unexpected{Api::AdvanceError::AccumulatorOverflow};
    }
    const auto fractionalProduct = remainder * numerator + Impl_->RateRemainder;
    const auto scaledWhole = quotient * numerator + fractionalProduct / denominator;
    const auto newRemainder = fractionalProduct % denominator;
    if (scaledWhole > Maximum - Impl_->PendingNanoseconds)
    {
        return std::unexpected{Api::AdvanceError::AccumulatorOverflow};
    }

    Impl_->PendingNanoseconds += scaledWhole;
    Impl_->RateRemainder = newRemainder;

    const auto stepNanoseconds = static_cast<std::uint64_t>(Impl_->Duration.Value().count());
    const auto pendingTicks = Impl_->PendingNanoseconds / stepNanoseconds;
    if (Impl_->Rate.IsPaused())
    {
        return Api::AdvanceResult{Api::TickCount{}, Api::TickCount{pendingTicks}};
    }

    const auto ticksToExecute = std::min(pendingTicks, maximumTicks.Value());
    const auto result = Step(Api::TickCount{ticksToExecute});
    if (!result)
    {
        return std::unexpected{Api::AdvanceError::TickOverflow};
    }
    Impl_->PendingNanoseconds -= ticksToExecute * stepNanoseconds;
    return Api::AdvanceResult{Api::TickCount{ticksToExecute},
                              Api::TickCount{Impl_->PendingNanoseconds / stepNanoseconds}};
}

} // namespace UnrealVoxelSim::Simulation::FixedStep
