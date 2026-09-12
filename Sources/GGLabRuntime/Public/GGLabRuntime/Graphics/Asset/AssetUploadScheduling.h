#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetIdentity.h"
#include "GGLabRuntime/Graphics/Asset/AssetStreamingTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadControl.h"

#include <cstdint>
#include <memory>

namespace gglab
{
	class RHIDevice;
	class TransferManager;

	[[nodiscard]] constexpr AssetKind ToAssetKind(AssetStreamingWorkKind kind) noexcept
	{
		switch (kind)
		{
		case AssetStreamingWorkKind::Model:
			return AssetKind::Model;
		case AssetStreamingWorkKind::Texture:
			return AssetKind::Texture;
		case AssetStreamingWorkKind::Mesh:
			return AssetKind::Mesh;
		case AssetStreamingWorkKind::RuntimeMesh:
		case AssetStreamingWorkKind::Unknown:
			return AssetKind::Unknown;
		}
		return AssetKind::Unknown;
	}

	[[nodiscard]] constexpr AssetStreamingWorkKind ToAssetStreamingWorkKind(AssetKind kind) noexcept
	{
		switch (kind)
		{
		case AssetKind::Model:
			return AssetStreamingWorkKind::Model;
		case AssetKind::Texture:
			return AssetStreamingWorkKind::Texture;
		case AssetKind::Mesh:
			return AssetStreamingWorkKind::Mesh;
		case AssetKind::Unknown:
		case AssetKind::Material:
			return AssetStreamingWorkKind::Unknown;
		}
		return AssetStreamingWorkKind::Unknown;
	}

	[[nodiscard]] constexpr AssetContentVersion ToAssetContentVersion(
		const AssetStreamingIdentity& identity) noexcept
	{
		return MakeAssetContentVersion(
			ToAssetKind(identity.m_Kind), identity.m_StableId, identity.m_Generation);
	}

	[[nodiscard]] constexpr AssetStreamingIdentity ToAssetStreamingIdentity(
		const AssetContentVersion& contentVersion) noexcept
	{
		return {
			.m_Kind = ToAssetStreamingWorkKind(contentVersion.m_Key.m_Kind),
			.m_StableId = contentVersion.m_Key.m_StableId,
			.m_Generation = contentVersion.m_ContentGeneration,
		};
	}

	// Production scheduling capability for the asset upload owner. The concrete
	// scheduler stays Runtime-internal; developer and acceptance tooling consume
	// the AssetUploadControl vocabulary instead of this scheduling surface.
	class AssetUploadScheduling : public AssetUploadControl
	{
	public:
		virtual void EnqueueCpuPayload(
			AssetStreamingWorkDesc desc, AssetStreamingWork work) noexcept = 0;
		virtual uint32_t CancelReadyWork(const AssetContentVersion& contentVersion) noexcept = 0;
		virtual uint32_t UpdateWorkPriority(
			const AssetContentVersion& contentVersion, TaskPriority priority) noexcept = 0;
		virtual uint32_t UpdateWorkPriority(
			const AssetStreamingIdentity& identity, TaskPriority priority) noexcept = 0;
		virtual uint32_t Tick() noexcept = 0;
		virtual void Finalize() noexcept = 0;
		[[nodiscard]] virtual bool IsOwnerThread() const noexcept = 0;
	};

	struct AssetUploadSchedulerCreateInfo
	{
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		uint32_t m_RecentUploadCapacity = 64;
		AssetStreamingFrameBudget m_FrameBudget{};
	};

	[[nodiscard]] std::unique_ptr<AssetUploadScheduling> CreateAssetUploadScheduler(
		const AssetUploadSchedulerCreateInfo& createInfo) noexcept;
}
