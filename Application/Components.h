#pragma once
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Descriptor.h"

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
		};
		DEFINE_COMPONENT_TYPE(Texture);

		struct Material
		{
			MaterialID m_MaterialID = 0;

			TextureID m_TexBaseColor = 0;
		};
		DEFINE_COMPONENT_TYPE(Material);

		struct Mesh
		{
			MeshID m_MeshID = 0;

			std::unique_ptr<DX12Buffer> m_VertexBuffer;
			std::unique_ptr<DX12Buffer> m_IndexBuffer;

			size_t m_VertexCount = 0;
			size_t m_IndexCount = 0;

			D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};
			D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
		};
		DEFINE_COMPONENT_TYPE(Mesh);

		enum class ModelFileType : uint8_t
		{
			glTF
		};

		struct Model
		{
			ModelFileType m_Type;

			std::string m_ModelName;
			std::vector<Material> m_Materials;
			std::vector<Mesh> m_Meshes;
		};
		DEFINE_COMPONENT_TYPE(Model);
	}
	using namespace components;
}