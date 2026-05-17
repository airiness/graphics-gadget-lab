#pragma once
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/CopyContext.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureRegistry.h"

namespace gglab
{
	class DX12Device;
	class TransferManager;

	class AssetManager
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			TransferManager* m_TransferManager = nullptr;
			TextureRegistry* m_TextureRegistry = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
		};

		struct MeshUploadData
		{
			MeshID m_MeshId{};
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
		};

	private:
		struct MeshContainer
		{
			std::unordered_map<MeshID, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

		struct MaterialContainer
		{
			std::unordered_map<MaterialID, std::unique_ptr<Material>> m_MaterialIDMap;
		};

		struct ModelContainer
		{
			std::unordered_map<std::filesystem::path, ModelID> m_PathIDMap;
			std::unordered_map<ModelID, std::unique_ptr<Model>> m_ModelIDMap;
		};

	public:
		explicit AssetManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager();

		ModelID LoadModel(const std::filesystem::path& path) noexcept;

		Mesh* GetMesh(MeshID meshId) noexcept;
		const Mesh* GetMesh(MeshID meshId) const noexcept;

		Material* GetMaterial(MaterialID materialId) noexcept;
		const Material* GetMaterial(MaterialID materialId) const noexcept;

		Model* GetModel(ModelID modelId) noexcept;
		const Model* GetModel(ModelID modelId) const noexcept;

		MeshID AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		MaterialID AddMaterial(std::unique_ptr<Material>&& material) noexcept;
		ModelID AddModel(std::unique_ptr<Model>&& model) noexcept;

		uint32_t ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;
		TextureBindingGPU ResolveTextureBinding(const MaterialTextureBinding& binding,
			ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler) const noexcept;

	private:
		void UploadMesh(const MeshUploadData& uploadData, CopyContext& copyContext) noexcept;

		ModelID LoadModelGltf(const std::filesystem::path& path) noexcept;

		MeshID CreateMesh() noexcept;
		MaterialID CreateMaterial() noexcept;
		ModelID CreateModel(const std::filesystem::path& canonicalPath) noexcept;

		ModelID FindModel(const std::filesystem::path& canonicalPath) const noexcept;

	public:
		static void ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept;

	private:
		static void SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		TextureRegistry* m_TextureRegistry = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;

		MeshIDCounter m_MeshIdCounter{ ReservedMeshCount };
		MaterialIDCounter m_MaterialIdCounter{ ReservedMaterialCount };
		ModelIDCounter m_ModelIdCounter{ ReservedModelCount };

		MeshContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
		ModelContainer m_ModelContainer;
	};
}
