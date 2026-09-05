#pragma once
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"

#include <cstdint>
#include <array>
#include <string_view>

namespace gglab
{
	enum class IBLBakeStage : uint8_t
	{
		Idle,
		LoadingCache,
		Environment,
		EnvironmentMipChain,
		Irradiance,
		PrefilteredSpecular,
		BrdfLut,
		WaitingForGpu,
		SavingCache,
		Ready,
		Failed,
	};

	enum class IBLArtifactStage : uint8_t
	{
		Environment,
		Irradiance,
		PrefilteredSpecular,
		BrdfLut,

		Count,
	};

	enum class IBLArtifactResolution : uint8_t
	{
		Miss,
		CpuCache,
		LocalDdc,
		GpuBuild,
	};

	struct IBLStageArtifactStatus
	{
		DerivedDataKey m_DerivedDataKey{};
		ArtifactContentDigest m_ContentDigest{};
		IBLArtifactResolution m_Resolution = IBLArtifactResolution::Miss;
	};

	struct IBLBakeStatus
	{
		uint64_t m_RequestedGeneration = 0;
		uint64_t m_BakingGeneration = 0;
		uint64_t m_ActiveGeneration = 0;
		IBLBakeStage m_Stage = IBLBakeStage::Idle;
		float m_Progress = 0.0f;
		double m_GpuMilliseconds = 0.0;
		bool m_GpuTimingAvailable = false;
		std::array<IBLStageArtifactStatus, static_cast<size_t>(IBLArtifactStage::Count)>
			m_Artifacts{};
		bool m_CacheHit = false;
		bool m_PartialCacheHit = false;
		bool m_CpuCacheHit = false;
		bool m_DerivedDataCacheHit = false;
		uint32_t m_CacheHitStageCount = 0;
		uint32_t m_GpuBuildStageCount = 0;
		bool m_CacheWritePending = false;
		bool m_HasActiveBake = false;
	};

	[[nodiscard]] constexpr std::string_view GetIBLBakeStageName(IBLBakeStage stage) noexcept
	{
		switch (stage)
		{
		case IBLBakeStage::Idle:
			return "Idle";
		case IBLBakeStage::LoadingCache:
			return "Loading cache";
		case IBLBakeStage::Environment:
			return "Environment";
		case IBLBakeStage::EnvironmentMipChain:
			return "Environment mip chain";
		case IBLBakeStage::Irradiance:
			return "Irradiance";
		case IBLBakeStage::PrefilteredSpecular:
			return "Prefiltered specular";
		case IBLBakeStage::BrdfLut:
			return "BRDF LUT";
		case IBLBakeStage::WaitingForGpu:
			return "Waiting for GPU";
		case IBLBakeStage::SavingCache:
			return "Saving cache";
		case IBLBakeStage::Ready:
			return "Ready";
		case IBLBakeStage::Failed:
			return "Failed";
		}
		return "Unknown";
	}

	[[nodiscard]] constexpr std::string_view GetIBLArtifactStageName(
		IBLArtifactStage stage) noexcept
	{
		switch (stage)
		{
		case IBLArtifactStage::Environment:
			return "Environment";
		case IBLArtifactStage::Irradiance:
			return "Irradiance";
		case IBLArtifactStage::PrefilteredSpecular:
			return "Prefiltered specular";
		case IBLArtifactStage::BrdfLut:
			return "BRDF LUT";
		case IBLArtifactStage::Count:
			break;
		}
		return "Unknown";
	}

	[[nodiscard]] constexpr std::string_view GetIBLArtifactResolutionName(
		IBLArtifactResolution resolution) noexcept
	{
		switch (resolution)
		{
		case IBLArtifactResolution::Miss:
			return "Miss";
		case IBLArtifactResolution::CpuCache:
			return "CPU cache";
		case IBLArtifactResolution::LocalDdc:
			return "Local DDC";
		case IBLArtifactResolution::GpuBuild:
			return "GPU build";
		}
		return "Unknown";
	}
}
