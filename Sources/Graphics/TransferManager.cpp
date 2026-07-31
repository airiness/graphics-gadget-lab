#include "Core/Precompiled.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITransferContext.h"

#include <cstring>

namespace gglab
{
	TransferManager::TransferManager(std::unique_ptr<RHITransferContext> transferContext) noexcept :
		m_TransferContext(std::move(transferContext))
	{
		GGLAB_ASSERT_MSG(m_TransferContext != nullptr,
			"TransferManager failed to create an RHI transfer context.");
	}

	void TransferManager::Reclaim() noexcept
	{
		m_TransferContext->ReclaimCompleted();
	}

	TransferBatch TransferManager::BeginBatch() noexcept
	{
		// Reclaim completed uploads before starting a new batch.
		Reclaim();

		m_TransferContext->Begin();
		return TransferBatch(*m_TransferContext);
	}

	const std::byte* TransferManager::MapTextureReadback(
		RHIDevice& device, const RHITextureReadbackRequest& request) noexcept
	{
		if (!request.IsValid())
		{
			return nullptr;
		}
		const auto* mapped = static_cast<const std::byte*>(
			device.MapBuffer(request.m_Buffer.Get(), { 0, request.m_BufferSizeInBytes }));
		if (!mapped)
		{
			GGLAB_LOG_GRAPHICS_ERROR("TransferManager failed to map a texture readback buffer.");
			return nullptr;
		}
		return mapped;
	}

	TextureAssetData TransferManager::ResolveMappedTextureReadback(
		const RHITextureReadbackRequest& request, const std::byte* mapped) noexcept
	{
		if (!request.IsValid() || !mapped)
		{
			return {};
		}
		const RHITextureDesc& desc = request.m_TextureDesc;
		TextureAssetData result{};
		result.m_ResourceFormat = desc.m_Format;
		result.m_ViewFormat = desc.m_Format;
		result.m_Extent = desc.m_Extent;
		result.m_ArraySize = desc.m_ArraySize;
		result.m_MipLevels = desc.m_MipLevels;
		result.m_ColorSpace = TextureColorSpace::Linear;
		result.m_Subresources.reserve(request.m_Subresources.size());

		for (const auto& source : request.m_Subresources)
		{
			const uint64_t dataOffset = result.m_Pixels.size();
			const uint64_t tightSlicePitch = source.m_RowSizeInBytes * source.m_RowCount;
			const uint64_t dataSize = tightSlicePitch * source.m_Depth;
			result.m_Pixels.resize(static_cast<size_t>(dataOffset + dataSize));

			for (uint32_t depthSlice = 0; depthSlice < source.m_Depth; ++depthSlice)
			{
				for (uint32_t row = 0; row < source.m_RowCount; ++row)
				{
					const std::byte* src = mapped + source.m_BufferOffset +
						static_cast<uint64_t>(depthSlice) * source.m_SlicePitch +
						static_cast<uint64_t>(row) * source.m_RowPitch;
					std::byte* dst = result.m_Pixels.data() + dataOffset +
						static_cast<uint64_t>(depthSlice) * tightSlicePitch +
						static_cast<uint64_t>(row) * source.m_RowSizeInBytes;
					std::memcpy(dst, src, static_cast<size_t>(source.m_RowSizeInBytes));
				}
			}

			result.m_Subresources.push_back(
				{
					.m_DataOffset = dataOffset,
					.m_DataSize = dataSize,
					.m_RowPitch = source.m_RowSizeInBytes,
					.m_SlicePitch = tightSlicePitch,
					.m_Width = source.m_Width,
					.m_Height = source.m_Height,
					.m_Depth = source.m_Depth,
					.m_MipLevel = source.m_MipLevel,
					.m_ArraySlice = source.m_ArraySlice,
				});
		}

		return result;
	}

	void TransferManager::UnmapTextureReadback(
		RHIDevice& device, const RHITextureReadbackRequest& request) noexcept
	{
		if (request.IsValid())
		{
			device.UnmapBuffer(request.m_Buffer.Get(), {});
		}
	}
}
