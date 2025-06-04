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

	private:
		std::vector<VertexType> m_VerticesData;
		std::vector<IndexType> m_IndicesData;

		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};

		bool m_Uploaded = false;
	};

	template<typename VertexType, typename IndexType>
	inline Mesh<VertexType, IndexType>::Mesh(std::vector<VertexType>&& vertices, std::vector<IndexType>&& indices) noexcept :
		m_VerticesData(std::move(vertices)),
		m_IndicesData(std::move(indices))
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

		return false;
	}
}