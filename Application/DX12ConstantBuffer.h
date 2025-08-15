#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Buffer;

	template<typename T>
	class DX12ConstantBuffer
	{
	public:
		explicit DX12ConstantBuffer(DX12Device* device) noexcept;
		~DX12ConstantBuffer() noexcept;

		void Update(const T& data) noexcept;
		uint32_t GetBufferSize() const noexcept { return m_BufferSize; }

		DX12Buffer* GetBuffer() const noexcept { return m_ConstantBuffer.get(); }

	private:
		static uint32_t CalcBufferSize();

	private:
		std::unique_ptr<DX12Buffer> m_ConstantBuffer;
		T* m_MappedData = nullptr;
		uint32_t m_BufferSize = 0;
	};

	template<typename T>
	inline DX12ConstantBuffer<T>::DX12ConstantBuffer(DX12Device* device) noexcept :
		m_BufferSize(CalcBufferSize())
	{
		auto rtResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(m_BufferSize);
		m_ConstantBuffer = std::make_unique<DX12Buffer>(device, D3D12_HEAP_TYPE_UPLOAD, rtResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, std::nullopt);
		m_MappedData = reinterpret_cast<T*>(m_ConstantBuffer->Map());
	}

	template<typename T>
	inline DX12ConstantBuffer<T>::~DX12ConstantBuffer() noexcept
	{
	}

	template<typename T>
	inline void DX12ConstantBuffer<T>::Update(const T& data) noexcept
	{
		GGLAB_ASSERT_MSG(m_MappedData != nullptr, "");
		std::memcpy(m_MappedData, &data, m_BufferSize);
	}

	template<typename T>
	inline uint32_t DX12ConstantBuffer<T>::CalcBufferSize()
	{
		return (sizeof(T) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
	}
}