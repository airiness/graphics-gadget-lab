#pragma once
#include "RendererConstant.h"
#include "DX12Buffer.h"
#include "DX12Device.h"

namespace graphicsGadgetLab
{
	template<typename VertexType = Vertex, typename IndexType = uint16_t>
	class Mesh final
	{
	public:
		explicit Mesh(std::vector<VertexType>&& vertices, std::vector<IndexType>&& indices) noexcept;
		~Mesh() = default;

		bool UploadMesh(DX12Device* device);

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
	inline bool Mesh<VertexType, IndexType>::UploadMesh(DX12Device* device)
	{
		device->BeginUpload();
		{
			size_t vertexBufferSize = m_VerticesData.size() * sizeof(VertexType);
			size_t indexBufferSize = m_IndicesData.size() * sizeof(IndexType);

			m_VertexBuffer = std::make_unique<DX12Buffer>(device,
				D3D12_HEAP_TYPE_DEFAULT,
				CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
				D3D12_RESOURCE_STATE_COMMON);

			m_IndexBuffer = std::make_unique<DX12Buffer>(device,
				D3D12_HEAP_TYPE_DEFAULT,
				CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
				D3D12_RESOURCE_STATE_COMMON);

			device->UploadResource(m_VerticesData.data(), vertexBufferSize, m_VertexBuffer.get());
			device->UploadResource(m_IndicesData.data(), indexBufferSize, m_IndexBuffer.get());
		}
		device->EndUpload(true);

		m_Uploaded = true;
		m_VerticesData.clear();
		m_IndicesData.clear();

		return true;
	}
}