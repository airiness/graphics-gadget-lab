#pragma once
#include "Graphics/TransferBatch.h"
#include "Graphics/Asset/TextureAsset.h"

#include <memory>

namespace gglab
{
	class RHIDevice;
	class RHITransferContext;

	class TransferManager
	{
	public:
		explicit TransferManager(std::unique_ptr<RHITransferContext> transferContext) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TransferManager);
		~TransferManager() = default;

		void Reclaim() noexcept;

		TransferBatch BeginBatch() noexcept;
		[[nodiscard]] const std::byte* MapTextureReadback(
			RHIDevice& device, const RHITextureReadbackRequest& request) noexcept;
		[[nodiscard]] static TextureAssetData ResolveMappedTextureReadback(
			const RHITextureReadbackRequest& request, const std::byte* mapped) noexcept;
		static void UnmapTextureReadback(
			RHIDevice& device, const RHITextureReadbackRequest& request) noexcept;

	private:
		std::unique_ptr<RHITransferContext> m_TransferContext;
	};
}
