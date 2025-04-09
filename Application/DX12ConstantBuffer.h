#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;

	template<typename T>
	class DX12ConstantBuffer
	{
	public:
		explicit DX12ConstantBuffer(DX12Device* dx12Device) noexcept;
		~DX12ConstantBuffer() noexcept;
		DX12ConstantBuffer(const DX12ConstantBuffer&) = delete;
		DX12ConstantBuffer& operator=(const DX12ConstantBuffer&) = delete;
		DX12ConstantBuffer(DX12ConstantBuffer&&) = delete;
		DX12ConstantBuffer& operator=(DX12ConstantBuffer&&) = delete;
	private:
		DX12Device* m_DX12Device = nullptr;
	};
}