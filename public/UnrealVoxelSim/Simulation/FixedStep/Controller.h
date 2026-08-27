#pragma once

#include "UnrealVoxelSim/Simulation/Api/IPacer.h"
#include "UnrealVoxelSim/Simulation/Api/IStepper.h"
#include "UnrealVoxelSim/Simulation/Api/ITickPipeline.h"
#include "UnrealVoxelSim/Simulation/Api/StepDuration.h"

#include <memory>

namespace UnrealVoxelSim::Simulation::FixedStep
{

class Controller final : public Api::IStepper, public Api::IPacer
{
  public:
    explicit Controller(Api::ITickPipeline &pipeline, Api::StepDuration duration = Api::StandardStepDuration);
    ~Controller() override;

    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    Controller(Controller &&) = delete;
    Controller &operator=(Controller &&) = delete;

    [[nodiscard]] Api::TickIndex CurrentTick() const noexcept override;
    [[nodiscard]] std::expected<void, Api::StepError> Step(Api::TickCount count) override;

    [[nodiscard]] Api::Rate CurrentRate() const noexcept override;
    void SetRate(Api::Rate rate) noexcept override;
    [[nodiscard]] std::expected<Api::AdvanceResult, Api::AdvanceError> Advance(
        std::chrono::nanoseconds elapsed, Api::TickCount maximumTicks) override;

  private:
    class Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace UnrealVoxelSim::Simulation::FixedStep
