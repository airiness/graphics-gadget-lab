#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetIdentity.h"
#include "GGLabRuntime/Graphics/Asset/AssetLifecycleTypes.h"

#include <cstdint>

namespace gglab
{
	enum class ModelDependencyOutcome : uint8_t
	{
		Pending,
		Ready,
		Failed,
		Cancelled,
	};

	struct DependencyStatus
	{
		AssetContentVersion m_ContentVersion{};
		AssetContentState m_ContentState = AssetContentState::Unloaded;
		AssetResidencyState m_ResidencyState = AssetResidencyState::NonResident;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_ContentVersion.IsValid(); }

		friend constexpr bool operator==(
			const DependencyStatus&, const DependencyStatus&) = default;
	};

	[[nodiscard]] constexpr DependencyStatus MakeDependencyStatus(
		AssetContentVersion contentVersion, const AssetLifecycle& lifecycle) noexcept
	{
		return {
			.m_ContentVersion = contentVersion,
			.m_ContentState = lifecycle.m_ContentState,
			.m_ResidencyState = lifecycle.m_ResidencyState,
		};
	}

	[[nodiscard]] constexpr ModelDependencyOutcome ProjectDependencyOutcome(
		const DependencyStatus& status) noexcept
	{
		if (status.m_ContentState == AssetContentState::Failed)
		{
			return ModelDependencyOutcome::Failed;
		}
		if (status.m_ContentState == AssetContentState::Cancelled)
		{
			return ModelDependencyOutcome::Cancelled;
		}
		return status.m_ContentState == AssetContentState::Ready &&
			status.m_ResidencyState == AssetResidencyState::Resident
			? ModelDependencyOutcome::Ready
			: ModelDependencyOutcome::Pending;
	}
}
