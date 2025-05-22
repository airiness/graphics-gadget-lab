#pragma once
#include "RendererConstant.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	class DX12Device;
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


		return false;
	}
}