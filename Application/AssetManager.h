#pragma once
#include "Components.h"
#include "VertexData.h"
#include "GraphicsTypes.h"
#include <DirectXTex.h>

namespace gglab
{
	class DX12Device;
	class DX12ResourceUploader;
	class AssetManager
	{
	public:
		struct TextureUploadData
		{
			TextureId m_TextureId{};
			DirectX::ScratchImage m_ScratchImage;
		};

		struct MeshUploadData
		{
			MeshId m_MeshId{};
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
		};

	private:
		struct TextureContainer
		{
			std::unordered_map<std::filesystem::path, TextureId> m_PathIDMap;
			std::unordered_map<TextureId, std::unique_ptr<Texture>> m_TextureIDMap;
		};

		struct MeshesContainer
		{
			std::unordered_map<std::filesystem::path, std::vector<MeshId>> m_PathIDMap;
			std::unordered_map<MeshId, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

		struct MaterialContainer
		{
			std::unordered_map<MaterialId, std::unique_ptr<Material>> m_MaterialIDMap;
		};

	public:
		explicit AssetManager(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager() noexcept = default;

		Model LoadModel(const std::filesystem::path& path) noexcept;
		TextureId GetTextureID(const std::filesystem::path& path) noexcept;

		Texture* GetTexture(TextureId textureId) noexcept;
		Mesh* GetMesh(MeshId meshId) noexcept;
		Material* GetMaterial(MaterialId materialId) noexcept;

		void AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		void AddMaterial(std::unique_ptr<Material>&& material) noexcept;

	private:
		void UploadTexture(DX12ResourceUploader* resourceUploader, const TextureUploadData& uploadData) noexcept;
		void UploadMesh(DX12ResourceUploader* resourceUploader, const MeshUploadData& uploadData) noexcept;

		Model LoadModelGltf(const std::filesystem::path& path) noexcept;

		TextureId CreateTexture(const std::filesystem::path& cannonicalPath) noexcept;
		MeshId CreateMesh() noexcept;
		MaterialId CreateMaterial() noexcept;

		TextureId HasTexture(const std::filesystem::path& path) const noexcept;

		TextureUploadData& LoadTextureScratchImage(const std::filesystem::path& texPath,
			TextureUploadData& uploadData) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		TextureIdCounter m_TextureIdCounter{ ReservedTextureID.Value() + 1u };
		MeshIdCounter m_MeshIdCounter{ ReservedMeshID.Value() + 1u };
		MaterialIdCounter m_MaterialIdCounter{ ReservedMaterialID.Value() + 1u };

		TextureContainer m_TextureContainer;
		MeshesContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
	};
}