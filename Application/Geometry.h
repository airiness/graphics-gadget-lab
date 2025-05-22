#pragma once

namespace graphicsGadgetLab
{
	struct Vertex;

	using GeometryVertexType = Vertex;
	using GeometryIndexType = uint16_t;

	template<typename VertexType, typename IndexType>
	class Mesh;

	using GeometryMesh = Mesh<GeometryVertexType, GeometryIndexType>;

	class DX12Device;
	class Geometry
	{
	public:
		explicit Geometry(DX12Device* dx12Device) noexcept;
		virtual ~Geometry() noexcept;

		void Initialize() noexcept;

		const GeometryMesh* GetMesh() const noexcept;

	protected:
		virtual std::vector<GeometryVertexType> CreateVertices() noexcept = 0;
		virtual std::vector<GeometryIndexType> CreateIndices() noexcept = 0;

	protected:
		DX12Device* m_DX12Device = nullptr;
		std::unique_ptr<GeometryMesh> m_Mesh;
	};

	class Cube : public Geometry
	{
	public:
		explicit Cube(DX12Device* dx12Device) noexcept;
		virtual ~Cube() noexcept;

	protected:
		virtual std::vector<GeometryVertexType> CreateVertices() noexcept override;
		virtual std::vector<GeometryIndexType> CreateIndices() noexcept override;

	};
}