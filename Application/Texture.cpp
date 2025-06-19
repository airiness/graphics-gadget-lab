#include "Precompiled.h"
#include "Texture.h"
#include "DX12Device.h"

namespace graphicsGadgetLab
{
	void Texture::Upload(DX12Device* dx12Device) noexcept
	{
		dx12Device->BeginUpload();
		{
			const auto& texMedaData = m_ScratchImage.GetMetadata();
			m_DX12Texture = DX12Texture(dx12Device,
				D3D12_HEAP_TYPE_DEFAULT,
				CD3DX12_RESOURCE_DESC::Tex2D(texMedaData.format,
					static_cast<UINT64>(texMedaData.width),
					static_cast<UINT>(texMedaData.height),
					static_cast<UINT16>(texMedaData.arraySize),
					static_cast<UINT16>(texMedaData.mipLevels)),
				D3D12_RESOURCE_STATE_COMMON);

			const auto imageCount = m_ScratchImage.GetImageCount();
			std::vector<D3D12_SUBRESOURCE_DATA> subResourceDatas(imageCount);
			const auto& images = m_ScratchImage.GetImages();
			for (int32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
			{
				subResourceDatas[imageIndex].pData = images[imageIndex].pixels;
				subResourceDatas[imageIndex].RowPitch = images[imageIndex].rowPitch;
				subResourceDatas[imageIndex].SlicePitch = images[imageIndex].slicePitch;
			}

			dx12Device->UploadResource(subResourceDatas, &m_DX12Texture);
		}
		dx12Device->EndUpload(true);

		m_ScratchImage.Release();
		m_Uploaded = true;
	}
}