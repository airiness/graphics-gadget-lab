#pragma once
#include "RendererConstant.h"
#include "DX12Buffer.h"
#include "DX12Device.h"

namespace graphicsGadgetLab
{
	template<typename T> struct DXGIIndexFormat;

	template<> struct DXGIIndexFormat<uint16_t> 
	{
		static constexpr DXGI_FORMAT Value = DXGI_FORMAT_R16_UINT;
	};

	template<> struct DXGIIndexFormat<uint32_t> 
	{
		static constexpr DXGI_FORMAT Value = DXGI_FORMAT_R32_UINT;
	};

	template<typename VertexType = Vertex, typename IndexType = uint16_t>
	class Mesh final
	{
	public:
		explicit Mesh(std::vector<VertexType>&& vertices, std::vector<IndexType>&& indices) noexcept;
		~Mesh() noexcept = default;

		void Upload(DX12Device* dx12Device) noexcept;

		const DX12Buffer* GetVertexBuffer() const noexcept { return m_VertexBuffer.get(); }
		const DX12Buffer* GetIndexBuffer() const noexcept { return m_IndexBuffer.get(); }

		const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const noexcept { return m_VertexBufferView; }
		const D3D12_INDEX_BUFFER_VIEW& GetIndexBufferView() const noexcept { return m_IndexBufferView; }

		uint32_t GetVertexCount() const noexcept { return m_VertexCount; }
		uint32_t GetIndexCount() const noexcept { return m_IndexCount; }

	private:
		std::vector<VertexType> m_VerticesData;
		std::vector<IndexType> m_IndicesData;

		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};
		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		bool m_Uploaded = false;
	};

	template<typename VertexType, typename IndexType>
	inline Mesh<VertexType, IndexType>::Mesh(std::vector<VertexType>&& vertices, std::vector<IndexType>&& indices) noexcept :
		m_VerticesData(std::move(vertices)),
		m_IndicesData(std::move(indices)),
		m_VertexCount(static_cast<uint32_t>(m_VerticesData.size())),
		m_IndexCount(static_cast<uint32_t>(m_IndicesData.size()))
	{
	}

	template<typename VertexType, typename IndexType>
	inline void Mesh<VertexType, IndexType>::Upload(DX12Device* dx12Device) noexcept
	{
		dx12Device->BeginUpload();
		{
			size_t vertexBufferSize = m_VerticesData.size() * sizeof(VertexType);
			size_t indexBufferSize = m_IndicesData.size() * sizeof(IndexType);

			m_VertexBuffer = std::make_unique<DX12Buffer>(dx12Device,
				D3D12_HEAP_TYPE_DEFAULT,
				CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_COMMON);

			m_IndexBuffer = std::make_unique<DX12Buffer>(dx12Device,
				D3D12_HEAP_TYPE_DEFAULT,
				CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
				D3D12_RESOURCE_STATE_COMMON);

			dx12Device->UploadResource(m_VerticesData.data(), vertexBufferSize, m_VertexBuffer.get());
			dx12Device->UploadResource(m_IndicesData.data(), indexBufferSize, m_IndexBuffer.get());

			// Initialize buffer view
			m_VertexBufferView.BufferLocation = m_VertexBuffer->Get()->GetGPUVirtualAddress();
			m_VertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
			m_VertexBufferView.StrideInBytes = sizeof(VertexType);

			m_IndexBufferView.BufferLocation = m_IndexBuffer->Get()->GetGPUVirtualAddress();
			m_IndexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
			m_IndexBufferView.Format = DXGIIndexFormat<IndexType>::Value;

		}
		dx12Device->EndUpload(true);

		m_Uploaded = true;
		m_VerticesData.clear();
		m_IndicesData.clear();
	}
}