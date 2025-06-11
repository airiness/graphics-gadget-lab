#include "Precompiled.h"
#include "Geometry.h"
#include "DX12Device.h"
#include "Mesh.h"

namespace graphicsGadgetLab
{
	// Geometry
	Geometry::Geometry(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}

	Geometry::~Geometry() noexcept
	{
	}

	void Geometry::Initialize() noexcept
	{
		m_Mesh = std::make_unique<GeometryMesh>(CreateVertices(), CreateIndices());
		m_Mesh->UploadMesh(m_DX12Device);
	}

	const GeometryMesh* Geometry::GetMesh() const noexcept
	{
		return m_Mesh.get();
	}

	// Cube
	Cube::Cube(DX12Device* dx12Device) noexcept :
		Geometry(dx12Device)
	{
	}

	Cube::~Cube() noexcept
	{
	}

	std::vector<GeometryVertexType> Cube::CreateVertices() noexcept
	{
		constexpr std::array<XMFLOAT3, 8> positions =
		{ {
			{ -1.0f, -1.0f, -1.0f }, // 0
			{ -1.0f,  1.0f, -1.0f }, // 1
			{  1.0f,  1.0f, -1.0f }, // 2
			{  1.0f, -1.0f, -1.0f }, // 3
			{  1.0f, -1.0f,  1.0f }, // 4
			{  1.0f,  1.0f,  1.0f }, // 5
			{ -1.0f,  1.0f,  1.0f }, // 6
			{ -1.0f, -1.0f,  1.0f }, // 7
		} };

		constexpr std::array<XMFLOAT2, 4> texCoords =
		{ {
			{ 0.0f, 1.0f },
			{ 0.0f, 0.0f },
			{ 1.0f, 0.0f },
			{ 1.0f, 1.0f },
		} };

		constexpr std::array<std::array<int, 4>, 6> faceIndices =
		{ {
			{{0, 1, 2, 3}}, // Front
			{{5, 6, 7, 4}}, // Back
			{{1, 6, 5, 2}}, // Top
			{{7, 0, 3, 4}}, // Bottom
			{{6, 1, 0, 7}}, // Left
			{{3, 2, 5, 4}}, // Right
		} };

		constexpr std::array<XMFLOAT3, 6> normals =
		{ {
			{ 0.0f,  0.0f, -1.0f }, // Front
			{ 0.0f,  0.0f,  1.0f }, // Back
			{ 0.0f,  1.0f,  0.0f }, // Top
			{ 0.0f, -1.0f,  0.0f }, // Bottom
			{-1.0f,  0.0f,  0.0f }, // Left
			{ 1.0f,  0.0f,  0.0f }, // Right
		} };

		std::vector<GeometryVertexType> vertices;
		vertices.reserve(36); // 6 faces * 2 triangles * 3 verts

		for (size_t face = 0; face < 6; ++face)
		{
			const auto& idx = faceIndices[face];
			const auto& n = normals[face];

			vertices.push_back({ positions[idx[0]], n, texCoords[0] });
			vertices.push_back({ positions[idx[1]], n, texCoords[1] });
			vertices.push_back({ positions[idx[2]], n, texCoords[2] });

			vertices.push_back({ positions[idx[0]], n, texCoords[0] });
			vertices.push_back({ positions[idx[2]], n, texCoords[2] });
			vertices.push_back({ positions[idx[3]], n, texCoords[3] });
		}

		return vertices;
	}

	std::vector<GeometryIndexType> Cube::CreateIndices() noexcept
	{
		std::vector<GeometryIndexType> indices;
		indices.reserve(36); // 6 faces * 2 triangles * 3 indices

		for (GeometryIndexType face = 0; face < 6; ++face)
		{
			GeometryIndexType base = face * 4;
			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);

			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
		}

		return indices;
	}
}
