#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12Buffer;
	class Mesh
	{
	public:
		Mesh();
		~Mesh();

		// TODO: move this function into AssetManager, temporary write here as a static function
		static void UploadMesh(Mesh* mesh, DX12Device* device);
	private:
		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	};

}