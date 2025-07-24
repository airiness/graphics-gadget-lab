#pragma once
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Descriptor.h"
#include "GraphicsTypes.h"

namespace graphicsGadgetLab
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
			TextureID m_TextureID = 0;

			std::unique_ptr<DX12Texture> m_Texture;
			DX12Descriptor m_Descriptor;

			bool m_IsUploaded = false;
		};
		DEFINE_COMPONENT_TYPE(Texture);

		struct Material
		{
			MaterialID m_MaterialID = 0;
			std::string m_MaterialName;

			TextureID m_TexBaseColor = InvalidTextureID;
			TextureID m_TexMetallicRoughness = InvalidTextureID;
			TextureID m_NormalTex = InvalidTextureID;
			TextureID m_OcclusionTex = InvalidTextureID;
			TextureID m_EmissiveTex = InvalidTextureID;

			Color m_BaseColor = ggLabColor::White;
			float m_MetallicFactor = 0.0f;
			float m_RoughnessFactor = 1.0f;
			Color m_EmissiveColor = ggLabColor::Black;

			bool m_DoubleSided = false;
		};
		DEFINE_COMPONENT_TYPE(Material);

		struct Mesh
		{
			MeshID m_MeshID = 0;
			std::string m_MeshName;

			MaterialID m_Material = InvalidMaterialID;

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
			std::vector<MeshID> m_Meshes;
		};
		DEFINE_COMPONENT_TYPE(Model);
	}
	using namespace components;
}