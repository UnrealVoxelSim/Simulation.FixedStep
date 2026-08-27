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
    m_Impl = std::make_unique<Impl>(pipeline, duration);
}

Controller::~Controller() = default;

Api::TickIndex Controller::CurrentTick() const noexcept
{
    m_Impl->AssertOwnerThread();
    return m_Impl->Tick;
}

std::expected<void, Api::StepError> Controller::Step(const Api::TickCount count)
{
    m_Impl->AssertOwnerThread();
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (count.Value() > maximum - m_Impl->Tick.Value())
    {
        return std::unexpected{Api::StepError::TickOverflow};
    }

    for (std::uint64_t index = 0; index < count.Value(); ++index)
    {
        m_Impl->Pipeline.Step(Api::StepContext{m_Impl->Tick, m_Impl->Duration});
        m_Impl->Tick.Advance();
    }
    return {};
}

Api::Rate Controller::CurrentRate() const noexcept
{
    m_Impl->AssertOwnerThread();
    return m_Impl->Rate;
}

void Controller::SetRate(const Api::Rate rate) noexcept
{
    m_Impl->AssertOwnerThread();
    const auto oldDenominator = static_cast<std::uint64_t>(m_Impl->Rate.Denominator());
    const auto newDenominator = static_cast<std::uint64_t>(rate.Denominator());
    m_Impl->RateRemainder = m_Impl->RateRemainder * newDenominator / oldDenominator;
    m_Impl->Rate = rate;
}

std::expected<Api::AdvanceResult, Api::AdvanceError> Controller::Advance(const std::chrono::nanoseconds elapsed,
                                                                         const Api::TickCount maximumTicks)
{
    m_Impl->AssertOwnerThread();
    if (elapsed.count() < 0)
    {
        return std::unexpected{Api::AdvanceError::NegativeElapsedTime};
    }

    const auto denominator = static_cast<std::uint64_t>(m_Impl->Rate.Denominator());
    const auto numerator = static_cast<std::uint64_t>(m_Impl->Rate.Numerator());
    const auto elapsedNanoseconds = static_cast<std::uint64_t>(elapsed.count());
    const auto quotient = elapsedNanoseconds / denominator;
    const auto remainder = elapsedNanoseconds % denominator;
    constexpr auto Maximum = std::numeric_limits<std::uint64_t>::max();

    if (numerator != 0 && quotient > Maximum / numerator)
    {
        return std::unexpected{Api::AdvanceError::AccumulatorOverflow};
    }
    const auto fractionalProduct = remainder * numerator + m_Impl->RateRemainder;
    const auto scaledWhole = quotient * numerator + fractionalProduct / denominator;
    const auto newRemainder = fractionalProduct % denominator;
    if (scaledWhole > Maximum - m_Impl->PendingNanoseconds)
    {
        return std::unexpected{Api::AdvanceError::AccumulatorOverflow};
    }

    m_Impl->PendingNanoseconds += scaledWhole;
    m_Impl->RateRemainder = newRemainder;

    const auto stepNanoseconds = static_cast<std::uint64_t>(m_Impl->Duration.Value().count());
    const auto pendingTicks = m_Impl->PendingNanoseconds / stepNanoseconds;
    if (m_Impl->Rate.IsPaused())
    {
        return Api::AdvanceResult{Api::TickCount{}, Api::TickCount{pendingTicks}};
    }

    const auto ticksToExecute = std::min(pendingTicks, maximumTicks.Value());
    const auto result = Step(Api::TickCount{ticksToExecute});
    if (!result)
    {
        return std::unexpected{Api::AdvanceError::TickOverflow};
    }
    m_Impl->PendingNanoseconds -= ticksToExecute * stepNanoseconds;
    return Api::AdvanceResult{Api::TickCount{ticksToExecute},
                              Api::TickCount{m_Impl->PendingNanoseconds / stepNanoseconds}};
}

}
