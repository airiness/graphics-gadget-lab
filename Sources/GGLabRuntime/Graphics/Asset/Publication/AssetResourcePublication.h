#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Task/TaskTypes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

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
		// Source bytes are released only when the publication job no longer owns
		// them. Copying data into an upload payload does not release its source.
		uint64_t m_SourceBytesReleased = 0;
		// Independent CPU payload bytes created by this step. Transfer staging
		// allocations are accounted by the upload scheduler separately.
		uint64_t m_SourceBytesCopiedToUpload = 0;
		AssetResourcePublicationStage m_Stage = AssetResourcePublicationStage::Unknown;
	};

	// Tracks the source payload still owned by one publication job. Step usage
	// may retire ownership early; every terminal path retires the remainder.
	struct AssetResourcePublicationPayloadState
	{
		uint64_t m_RemainingSourceBytes = 0;

		[[nodiscard]] uint64_t RetireStep(const AssetResourcePublicationStepUsage& usage) noexcept
		{
			GGLAB_ASSERT_MSG(usage.m_SourceBytesReleased <= m_RemainingSourceBytes,
				"Resource publication step released more source bytes than the job owns.");
			const uint64_t releasedBytes =
				std::min(usage.m_SourceBytesReleased, m_RemainingSourceBytes);
			m_RemainingSourceBytes -= releasedBytes;
			return releasedBytes;
		}

		[[nodiscard]] uint64_t RetireTerminal() noexcept
		{
			return std::exchange(m_RemainingSourceBytes, 0);
		}
	};

	struct AssetResourcePublicationStepResult
	{
		AssetResourcePublicationStepStatus m_Status = AssetResourcePublicationStepStatus::Continue;
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
		virtual void Abort(AssetResourcePublicationContext& context,
			AssetResourcePublicationAbortReason reason) noexcept = 0;
		[[nodiscard]] virtual uint64_t GetProgressToken() const noexcept = 0;
		[[nodiscard]] virtual AssetResourcePublicationStage GetCurrentStage() const noexcept = 0;
	};
}
