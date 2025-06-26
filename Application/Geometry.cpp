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
		m_Mesh->Upload(m_DX12Device);
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
		constexpr std::array<std::array<Vector3, VertexCountPerFace>, FaceCount> facePositions = 
		{ {
			// Front face (-Z)
			{{{-1.0f, -1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}}},
			// Back face (+Z)
			{{{ 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}}},
			// Top face (+Y)
			{{{-1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f, -1.0f}}},
			// Bottom face (-Y)
			{{{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f,  1.0f}}},
			// Left face (-X)
			{{{-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}}},
			// Right face (+X)
			{{{ 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}}},
		} };

		constexpr std::array<Vector2, VertexCountPerFace> texCoords =
		{ {
			{ 0.0f, 1.0f },
			{ 0.0f, 0.0f },
			{ 1.0f, 0.0f },
			{ 1.0f, 1.0f },
		} };

		constexpr std::array<Vector3, FaceCount> faceNormals =
		{ {
			{ 0.0f,  0.0f, -1.0f }, // Front
			{ 0.0f,  0.0f,  1.0f }, // Back
			{ 0.0f,  1.0f,  0.0f }, // Top
			{ 0.0f, -1.0f,  0.0f }, // Bottom
			{-1.0f,  0.0f,  0.0f }, // Left
			{ 1.0f,  0.0f,  0.0f }, // Right
		} };

		std::vector<GeometryVertexType> vertices;
		vertices.reserve(FaceCount * VertexCountPerFace);

		for (size_t face = 0; face < FaceCount; ++face)
		{
			const auto& n = faceNormals[face];
			const auto& p = facePositions[face];

			for (size_t i = 0; i < VertexCountPerFace; ++i)
			{
				vertices.push_back({ p[i], n, texCoords[i] });
			}
		}

		return vertices;
	}

	std::vector<GeometryIndexType> Cube::CreateIndices() noexcept
	{
		std::vector<GeometryIndexType> indices;
		indices.reserve(36); // 6 faces * 2 triangles * 3 indices

		for (GeometryIndexType face = 0; face < FaceCount; ++face)
		{
			GeometryIndexType base = face * VertexCountPerFace;
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
