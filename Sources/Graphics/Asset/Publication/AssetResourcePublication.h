#pragma once
#include "Core/Task/TaskTypes.h"

#include <cstdint>
#include <string>

namespace gglab
{
	enum class AssetResourcePublicationStage : uint8_t
	{
		Unknown,
		Textures,
		Materials,
		Meshes,
		MeshInstances,
		Dependencies,
		Commit,
		ReleaseRetains,
		Count,
	};

	enum class AssetResourcePublicationStepStatus : uint8_t
	{
		Continue,
		Completed,
		Failed,
		Cancelled,
	};

	enum class AssetResourcePublicationAbortReason : uint8_t
	{
		Cancelled,
		Failed,
		Shutdown,
	};

	struct AssetResourcePublicationStepUsage
	{
		uint32_t m_ResourceCreations = 0;
		uint64_t m_PayloadBytesMovedToUpload = 0;
		uint64_t m_PayloadBytesDestroyed = 0;
		AssetResourcePublicationStage m_Stage = AssetResourcePublicationStage::Unknown;
	};

	struct AssetResourcePublicationStepResult
	{
		AssetResourcePublicationStepStatus m_Status =
			AssetResourcePublicationStepStatus::Continue;
		AssetResourcePublicationStepUsage m_Usage{};
		std::string m_Error;
	};

	class AssetUploadScheduler;

	struct AssetResourcePublicationContext
	{
		AssetUploadScheduler* m_Scheduler = nullptr;
		TaskPriority m_Priority = TaskPriority::Normal;
	};

	class IResourcePublicationJob
	{
	public:
		virtual ~IResourcePublicationJob() = default;

		[[nodiscard]] virtual AssetResourcePublicationStepResult Step(
			AssetResourcePublicationContext& context) noexcept = 0;
		virtual void Abort(
			AssetResourcePublicationContext& context,
			AssetResourcePublicationAbortReason reason) noexcept = 0;
		[[nodiscard]] virtual uint64_t GetProgressToken() const noexcept = 0;
		[[nodiscard]] virtual AssetResourcePublicationStage GetCurrentStage() const noexcept = 0;
	};
}
