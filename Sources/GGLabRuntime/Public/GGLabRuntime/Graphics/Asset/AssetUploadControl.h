#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetUploadControlTypes.h"

#include <memory>

namespace gglab
{
	class IResourcePublicationJob;

	// Explicit developer/acceptance control contract for the asset upload
	// scheduler. Published so acceptance Labs and host tooling can drive work
	// scheduling, budgets and fault injection without the concrete scheduler.
	class AssetUploadControl
	{
	public:
		virtual ~AssetUploadControl() = default;

		[[nodiscard]] virtual AssetUploadStatistics GetStatistics() const = 0;
		[[nodiscard]] virtual const AssetStreamingFrameBudget& GetFrameBudget() const noexcept = 0;
		virtual void SetFrameBudget(const AssetStreamingFrameBudget& budget) noexcept = 0;
		virtual void ArmResourcePublicationFault(
			const AssetResourcePublicationFaultInjection& fault) noexcept = 0;
		virtual void ClearResourcePublicationFault() noexcept = 0;
		virtual void ArmGpuCompletionHold(const AssetStreamingIdentity& identity) noexcept = 0;
		virtual void ClearGpuCompletionHold() noexcept = 0;
		virtual void EnqueueResourcePublication(AssetStreamingWorkDesc desc,
			std::unique_ptr<IResourcePublicationJob>&& job) noexcept = 0;
		virtual void EnqueueUploadRecording(AssetStreamingWorkDesc desc,
			AssetStreamingWork work) noexcept = 0;
		[[nodiscard]] virtual AssetUploadHandle RecordUpload(AssetUploadDesc desc,
			AssetUploadRecord record, AssetUploadCompletion completion = {}) noexcept = 0;
		virtual uint32_t CancelReadyWork(const AssetStreamingIdentity& identity) noexcept = 0;
		virtual void DrainReadyWork() noexcept = 0;
	};
}
