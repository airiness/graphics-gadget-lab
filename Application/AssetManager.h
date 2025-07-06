#pragma once
#include "Components.h"
#include "RendererConstant.h"
#include <DirectXTex.h>

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12ResourceUploader;
	class AssetManager
	{
	public:
		struct TextureUploadData
		{
			TextureID m_TextureID = InvalidTextureID;
			DirectX::ScratchImage m_ScratchImage;
		};

		struct MeshUploadData
		{
			MeshID m_MeshID = InvalidMeshID;
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
		};

	private:
		struct TextureContainer
		{
			std::unordered_map<std::filesystem::path, TextureID> m_PathIDMap;
			std::unordered_map<TextureID, std::unique_ptr<Texture>> m_TextureIDMap;
		};

		struct MeshesContainer
		{
			std::unordered_map<std::filesystem::path, std::vector<MeshID>> m_PathIDMap;
			std::unordered_map<MeshID, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

		struct MaterialContainer
		{
			std::unordered_map<MaterialID, std::unique_ptr<Material>> m_MaterialIDMap;
		};

	public:
		explicit AssetManager(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager() noexcept = default;

		void Initialize() noexcept;
		void Finalize() noexcept;

		Model LoadModel(const std::filesystem::path& path) noexcept;
		TextureID GetTextureID(const std::filesystem::path& path) noexcept;

		Texture* GetTexture(TextureID textureId) noexcept;
		Mesh* GetMesh(MeshID meshId) noexcept;
		Material* GetMaterial(MaterialID materialId) noexcept;

		void AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		void AddMaterial(std::unique_ptr<Material>&& material) noexcept;

	private:
		void UploadTexture(DX12ResourceUploader* resourceUploader, const TextureUploadData& uploadData) noexcept;
		void UploadMesh(DX12ResourceUploader* resourceUploader, const MeshUploadData& uploadData) noexcept;

		Model LoadModelGltf(const std::filesystem::path& path) noexcept;

		TextureID CreateTexture(const std::filesystem::path& cannonicalPath) noexcept;
		MeshID CreateMesh() noexcept;
		MaterialID CreateMaterial() noexcept;

		TextureID HasTexture(const std::filesystem::path& path) const noexcept;

		TextureUploadData& LoadTextureScratchImage(const std::filesystem::path& texPath,
			TextureUploadData& uploadData) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		TextureID m_NextTextureID = ReservedTextureID + 1;
		MeshID m_NextMeshID = ReservedMeshID + 1;
		MaterialID m_NextMaterialID = ReservedMaterialID + 1;

		TextureContainer m_TextureContainer;
		MeshesContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
	};
}