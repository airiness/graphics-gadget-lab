#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstdint>

namespace gglab
{
	// Asset entries use the same lifecycle for synchronous and asynchronous
	// requests. Consumers must only dereference GPU resources in Ready state.
	enum class AssetState : uint8_t
	{
		Unloaded,
		Queued,
		LoadingCpu,
		CpuReady,
		Publishing,
		UploadQueued,
		GpuProcessing,
		Ready,
		Evicting,
		Failed,
		Cancelled,
	};

	enum class AssetContentState : uint8_t
	{
		Unloaded,
		Loading,
		Ready,
		Failed,
		Cancelled,
	};

	enum class AssetResidencyState : uint8_t
	{
		NonResident,
		Queued,
		Uploading,
		Resident,
		Evicting,
	};

	enum class AssetResidencyPolicy : uint8_t
	{
		Cacheable,
		Pinned,
	};

	struct AssetLifecycle
	{
		uint64_t m_ContentGeneration = 0;
		uint64_t m_ResidencyEpoch = 0;
		uint64_t m_ResidencyOperationSerial = 0;
		uint64_t m_LastUsedFrame = 0;
		uint64_t m_UseCount = 0;
		AssetState m_State = AssetState::Unloaded;
		AssetContentState m_ContentState = AssetContentState::Unloaded;
		AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;
		AssetResidencyPolicy m_ResidencyPolicy = AssetResidencyPolicy::Cacheable;
	};

	[[nodiscard]] constexpr AssetContentState ProjectAssetContentState(AssetState state) noexcept
	{
		switch (state)
		{
		case AssetState::Unloaded:
			return AssetContentState::Unloaded;
		case AssetState::Queued:
		case AssetState::LoadingCpu:
			return AssetContentState::Loading;
		case AssetState::CpuReady:
		case AssetState::Publishing:
		case AssetState::UploadQueued:
		case AssetState::GpuProcessing:
		case AssetState::Ready:
		case AssetState::Evicting:
			return AssetContentState::Ready;
		case AssetState::Failed:
			return AssetContentState::Failed;
		case AssetState::Cancelled:
			return AssetContentState::Cancelled;
		}
		return AssetContentState::Unloaded;
	}

	[[nodiscard]] constexpr AssetResidencyState ProjectAssetResidencyState(
		AssetState state) noexcept
	{
		switch (state)
		{
		case AssetState::UploadQueued:
			return AssetResidencyState::Queued;
		case AssetState::GpuProcessing:
			return AssetResidencyState::Uploading;
		case AssetState::Ready:
			return AssetResidencyState::Resident;
		case AssetState::Evicting:
			return AssetResidencyState::Evicting;
		case AssetState::Unloaded:
		case AssetState::Queued:
		case AssetState::LoadingCpu:
		case AssetState::CpuReady:
		case AssetState::Publishing:
		case AssetState::Failed:
		case AssetState::Cancelled:
			return AssetResidencyState::NonResident;
		}
		return AssetResidencyState::NonResident;
	}

	inline void SetAssetState(AssetLifecycle& lifecycle, AssetState state) noexcept
	{
		const AssetResidencyState residencyState = ProjectAssetResidencyState(state);
		if (lifecycle.m_ResidencyState == AssetResidencyState::NonResident &&
			residencyState != AssetResidencyState::NonResident)
		{
			++lifecycle.m_ResidencyEpoch;
		}
		lifecycle.m_State = state;
		lifecycle.m_ContentState = ProjectAssetContentState(state);
		lifecycle.m_ResidencyState = residencyState;
	}

	inline void BeginAssetContentGeneration(AssetLifecycle& lifecycle, uint64_t generation,
		AssetState initialState,
		AssetResidencyPolicy policy = AssetResidencyPolicy::Cacheable) noexcept
	{
		GGLAB_ASSERT(generation > 0);
		lifecycle = {
			.m_ContentGeneration = generation,
			.m_ResidencyPolicy = policy,
		};
		SetAssetState(lifecycle, initialState);
	}

	[[nodiscard]] constexpr bool IsAssetLifecycleSynchronized(
		const AssetLifecycle& lifecycle) noexcept
	{
		return lifecycle.m_ContentState == ProjectAssetContentState(lifecycle.m_State) &&
			lifecycle.m_ResidencyState == ProjectAssetResidencyState(lifecycle.m_State);
	}
}
