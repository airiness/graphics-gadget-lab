#pragma once
#include "DX12Texture.h"
#include "DX12Descriptor.h"

#include <DirectXTex.h>

namespace graphicsGadgetLab
{
	class DX12Device;
	class Texture
	{
		//public:
		//	Texture() noexcept = default;
	private:
		void Upload(DX12Device* dx12Device) noexcept;

	private:
		DX12Texture m_DX12Texture;
		DX12Descriptor m_Descriptor;

		DirectX::ScratchImage m_ScratchImage;

		bool m_Uploaded = false;

		friend class TextureManager;
	};
}
