#pragma once
#include "VertexData.h"
#include "GraphicsTypes.h"
#include "CopyContext.h"
#include <DirectXTex.h>

namespace gglab
{
	class DX12Device;
	class TransferManager;

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

		struct MeshContainer
		{
			std::unordered_map<MeshId, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

		struct MaterialContainer
		{
			std::unordered_map<MaterialId, std::unique_ptr<Material>> m_MaterialIDMap;
		};

		struct ModelContainer
		{
			std::unordered_map<std::filesystem::path, ModelId> m_PathIDMap;
			std::unordered_map<ModelId, std::unique_ptr<Model>> m_ModelIDMap;
		};

	public:
		explicit AssetManager(DX12Device* dx12Device, TransferManager* transferManager) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager() noexcept = default;

		ModelId LoadModel(const std::filesystem::path& path) noexcept;
		TextureId LoadTexture(const std::filesystem::path& path) noexcept;

		Texture* GetTexture(TextureId textureId) noexcept;
		const Texture* GetTexture(TextureId textureId) const noexcept;

		Mesh* GetMesh(MeshId meshId) noexcept;
		const Mesh* GetMesh(MeshId meshId) const noexcept;

		Material* GetMaterial(MaterialId materialId) noexcept;
		const Material* GetMaterial(MaterialId materialId) const noexcept;

		Model* GetModel(ModelId modelId) noexcept;
		const Model* GetModel(ModelId modelId) const noexcept;

		uint32_t GetTextureDescriptorIndex(TextureId textureId) const noexcept;

		MeshId AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		MaterialId AddMaterial(std::unique_ptr<Material>&& material) noexcept;
		ModelId AddModel(std::unique_ptr<Model>&& model) noexcept;

	private:
		void UploadTexture(const TextureUploadData& uploadData, CopyContext& copyContext) noexcept;
		void UploadMesh(const MeshUploadData& uploadData, CopyContext& copyContext) noexcept;

		ModelId LoadModelGltf(const std::filesystem::path& path) noexcept;

		TextureId CreateTexture(const std::filesystem::path& canonicalPath) noexcept;
		MeshId CreateMesh() noexcept;
		MaterialId CreateMaterial() noexcept;
		ModelId CreateModel(const std::filesystem::path& canonicalPath) noexcept;

		TextureId FindTexture(const std::filesystem::path& canonicalPath) const noexcept;
		ModelId FindModel(const std::filesystem::path& canonicalPath) const noexcept;

		TextureUploadData& LoadTextureScratchImage(const std::filesystem::path& texPath,
			TextureUploadData& uploadData) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		TransferManager* m_TransferManager = nullptr;

		TextureIdCounter m_TextureIdCounter{ ReservedTextureID.Value() + 1u };
		MeshIdCounter m_MeshIdCounter{ ReservedMeshID.Value() + 1u };
		MaterialIdCounter m_MaterialIdCounter{ ReservedMaterialID.Value() + 1u };
		ModelIdCounter m_ModelIdCounter{ ReservedModelID.Value() + 1u };

		TextureContainer m_TextureContainer;
		MeshContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
		ModelContainer m_ModelContainer;
	};
}