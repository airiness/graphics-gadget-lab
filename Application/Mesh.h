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

	};

}