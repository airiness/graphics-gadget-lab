#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Texture
	{
	public:
		explicit DX12Texture(DX12Device* dx12Device) noexcept;
		~DX12Texture() noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
	};
}