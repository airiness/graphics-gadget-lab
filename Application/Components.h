#pragma once
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Descriptor.h"
#include "GraphicsTypes.h"
#include "Color.h"

namespace gglab
{
	namespace components
	{
		template<typename T>
		struct Component : std::false_type {};

#define DEFINE_COMPONENT_TYPE(type) \
	template<> struct Component<type> : std::true_type {};

		struct Transform
		{
			Vector3 m_Position = Vector3::Zero;
			Quaternion m_Rotation = Quaternion::Identity;
			Vector3 m_Scale = Vector3::One;
		};
		DEFINE_COMPONENT_TYPE(Transform);

		struct Texture
		{
			TextureId m_TextureId{};

			std::unique_ptr<DX12Texture> m_Texture;
			DX12Descriptor m_Descriptor;

			bool m_IsUploaded = false;
		};
		DEFINE_COMPONENT_TYPE(Texture);

		struct Material
		{
			MaterialId m_MaterialId{};
			std::string m_MaterialName;

			TextureId m_BaseColorTex{};
			TextureId m_MetallicRoughnessTex{};
			TextureId m_NormalTex{};
			TextureId m_OcclusionTex{};
			TextureId m_EmissiveTex{};

			Color m_BaseColor = color::White;
			float m_MetallicFactor = 0.0f;
			float m_RoughnessFactor = 1.0f;
			float m_NormalScale = 1.0f;
			float m_OcclusionStrength = 1.0f;
			Color m_EmissiveColor = color::Black;

			uint32_t m_Flags; // TODO: double sided and so on
		};
		DEFINE_COMPONENT_TYPE(Material);

		struct Mesh
		{
			MeshId m_MeshId{};
			std::string m_MeshName;

			MaterialId m_MaterialId{};

			std::unique_ptr<DX12Buffer> m_VertexBuffer;
			std::unique_ptr<DX12Buffer> m_IndexBuffer;

			size_t m_VertexCount = 0;
			size_t m_IndexCount = 0;

			D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};
			D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};

			bool m_IsUploaded = false;
		};
		DEFINE_COMPONENT_TYPE(Mesh);

		struct Model
		{
			ModelType m_Type = ModelType::ModelType_Invalid;
			std::string m_ModelName;

			//Matrix m_WorldTransform = Matrix::Identity;
			std::vector<MeshId> m_Meshes;
		};
		DEFINE_COMPONENT_TYPE(Model);

		struct Light
		{
			LightType m_LightType = LightType::LightType_Directional;
		};
	}
	using namespace components;
}