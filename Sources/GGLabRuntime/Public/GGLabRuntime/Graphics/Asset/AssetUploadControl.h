#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetUploadControlTypes.h"

namespace gglab
{
	// Explicit developer/acceptance control contract for the asset upload
	// scheduler. It is a sibling of the production AssetUploadScheduling
	// contract: production code receives only the scheduling and submission
	// view, while acceptance Labs and host tooling receive this budget, fault
	// injection and GPU completion hold view.
	class AssetUploadControl
	{
	public:
		virtual ~AssetUploadControl() = default;

		[[nodiscard]] virtual const AssetStreamingFrameBudget& GetFrameBudget() const noexcept = 0;
		virtual void SetFrameBudget(const AssetStreamingFrameBudget& budget) noexcept = 0;
		virtual void ArmResourcePublicationFault(
			const AssetResourcePublicationFaultInjection& fault) noexcept = 0;
		virtual void ClearResourcePublicationFault() noexcept = 0;
		virtual void ArmGpuCompletionHold(const AssetStreamingIdentity& identity) noexcept = 0;
		virtual void ClearGpuCompletionHold() noexcept = 0;
	};
}
