#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

namespace gglab
{
	class AssetManager;
	class AssetUploadControl;
	class AssetUploadScheduling;
	class EnvironmentSourceControl;
	class RenderSamplerAccess;
	class TransferManager;

	// Explicit composition-time service contract for the application runtime.
	// The concrete service owners stay Runtime-internal; this contract names
	// only the services required to compose and shut down the asset and
	// environment verticals.
	class RenderCompositionAccess
	{
	public:
		virtual ~RenderCompositionAccess() = default;

		[[nodiscard]] virtual TransferManager* GetTransferManager() const noexcept = 0;
		[[nodiscard]] virtual AssetUploadScheduling* GetAssetUploadScheduler() const noexcept = 0;
		[[nodiscard]] virtual AssetUploadControl* GetAssetUploadControl() const noexcept = 0;
		[[nodiscard]] virtual RenderSamplerAccess* GetSamplerRegistry() const noexcept = 0;
		[[nodiscard]] virtual EnvironmentSourceControl* GetEnvironmentSourceControl()
			const noexcept = 0;

		virtual void AttachAssetManager(AssetManager& assetManager) noexcept = 0;
		virtual void DetachAssetManager() noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint GetLastSubmittedFencePoint() const noexcept = 0;
	};
}
