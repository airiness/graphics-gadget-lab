#pragma once

#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"

#include <cstdint>

namespace gglab
{
	// GTAOSettings clamp bounds enforced by view-settings sanitization.
	inline constexpr uint32_t GTAOMaxDirectionCount = 8;
	inline constexpr uint32_t GTAOMaxStepCount = 8;
	inline constexpr uint32_t GTAOMaxDenoiseRadius = 8;

	enum class GTAOFrameStatus : uint8_t
	{
		Disabled,
		Active,
		CoreCapabilityUnavailable,
		PipelineUnavailable,
		RenderSceneUnavailable,
		DepthCoverageUnavailable,
		NoOpaqueDraws,
	};

	[[nodiscard]] constexpr GTAOFrameStatus ResolveGTAOFrameStatus(bool enabled,
		bool coreCapabilityAvailable, bool pipelineAvailable, bool renderSceneAvailable,
		bool depthCoverageAvailable, bool hasOpaqueDraws) noexcept
	{
		if (!enabled)
		{
			return GTAOFrameStatus::Disabled;
		}
		if (!coreCapabilityAvailable)
		{
			return GTAOFrameStatus::CoreCapabilityUnavailable;
		}
		if (!pipelineAvailable)
		{
			return GTAOFrameStatus::PipelineUnavailable;
		}
		if (!renderSceneAvailable)
		{
			return GTAOFrameStatus::RenderSceneUnavailable;
		}
		if (!depthCoverageAvailable)
		{
			return GTAOFrameStatus::DepthCoverageUnavailable;
		}
		return hasOpaqueDraws ? GTAOFrameStatus::Active : GTAOFrameStatus::NoOpaqueDraws;
	}

	struct GTAOSurfaceFormatSupport
	{
		RHITextureSupportResult m_ShaderResource{};
		RHITextureSupportResult m_TypedUavStore{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_ShaderResource.IsSupported() && m_TypedUavStore.IsSupported();
		}
	};

	struct GTAOFinalAOFormatResolution
	{
		GTAOSurfaceFormatSupport m_PreferredR8Unorm{};
		GTAOSurfaceFormatSupport m_FallbackR16Float{};
		RHIFormat m_Format = RHIFormat::Unknown;

		[[nodiscard]] constexpr bool IsAvailable() const noexcept
		{
			return m_Format != RHIFormat::Unknown;
		}

		[[nodiscard]] constexpr bool UsesFallback() const noexcept
		{
			return m_Format == RHIFormat::R16Float;
		}
	};

	struct GTAOCapabilityStatus
	{
		GTAOSurfaceFormatSupport m_R16Float{};
		GTAOSurfaceFormatSupport m_R32Float{};
		GTAOSurfaceFormatSupport m_R16G16Float{};
		GTAOSurfaceFormatSupport m_R16G16B16A16Float{};
		GTAOFinalAOFormatResolution m_FinalAO{};

		[[nodiscard]] constexpr bool IsCoreAvailable() const noexcept
		{
			return m_R16Float.IsSupported() && m_R32Float.IsSupported() &&
				m_FinalAO.IsAvailable();
		}

		[[nodiscard]] constexpr bool AreDiagnosticOutputsAvailable() const noexcept
		{
			return m_R16G16Float.IsSupported() && m_R16G16B16A16Float.IsSupported();
		}
	};
}
