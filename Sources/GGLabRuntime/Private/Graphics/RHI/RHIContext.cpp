#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

namespace gglab
{
	RHIContextFactoryBase::~RHIContextFactoryBase() = default;

	RHITeardownState QuiesceRHIForTeardown(
		RHIContext& context, const RHITeardownWaitPolicy& policy) noexcept
	{
		for (uint32_t attempt = 1; attempt <= policy.m_AttemptCount; ++attempt)
		{
			if (context.IsDeviceLost())
			{
				return RHITeardownState::DeviceLost;
			}
			const RHIGpuWaitResult result =
				context.WaitIdleFor(policy.m_AttemptTimeoutMilliseconds);
			if (result == RHIGpuWaitResult::Completed)
			{
				return RHITeardownState::Quiesced;
			}
			if (result == RHIGpuWaitResult::DeviceLost || context.IsDeviceLost())
			{
				return RHITeardownState::DeviceLost;
			}
			GGLAB_LOG_GRAPHICS_WARN("RHI teardown idle wait {} of {} returned {}.", attempt,
				policy.m_AttemptCount, GetRHIGpuWaitResultName(result));
		}
		GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
			"RHI teardown could not verify idle on a live device; GPU-visible memory is leaked.");
		return RHITeardownState::Unverified;
	}
}