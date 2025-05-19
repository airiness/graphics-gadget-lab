#pragma once

namespace graphicsGadgetLab
{
	class DX12Buffer;
	class Mesh
	{
	public:
		Mesh();
		~Mesh();

	private:

		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;

	};

}