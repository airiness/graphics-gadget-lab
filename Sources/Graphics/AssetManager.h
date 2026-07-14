#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ModelImporter.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureRegistry.h"

namespace gglab
{
	class RHIDevice;
	class AssetUploadScheduler;
	class TaskSystem;
	class TransferBatch;
	class TransferManager;
	struct AssetSnapshot;

	class AssetManager;
	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;

	class AssetManager
	{
	public:
		using MaterialTextureSamplingSettings = ModelImportSettings;

		struct ModelLoadRequest
		{
			ModelID m_ModelId{};
			TaskHandle m_Task{};

			[[nodiscard]] bool IsValid() const noexcept { return m_ModelId.IsValid(); }
		};

		using TextureLoadRequest = TextureRegistry::TextureLoadRequest;

		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			TransferManager* m_TransferManager = nullptr;
			AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
			TextureRegistry* m_TextureRegistry = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
			MaterialTextureSamplingSettings m_MaterialTextureSampling{};
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

		[[nodiscard]] ModelLoadRequest LoadModelAsync(
			const std::filesystem::path& path,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] TextureLoadRequest LoadTextureAsync(
			const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		void Tick() noexcept;

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
		MaterialTextureBindingGPU ResolveTextureBinding(const MaterialTextureBinding& binding,
			ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler) const noexcept;

	private:
		[[nodiscard]] bool UploadMesh(
			const MeshUploadData& uploadData,
			TransferBatch& transferBatch) noexcept;
		void CompleteMeshUpload(MeshID meshId, bool succeeded) noexcept;
		bool PublishImportedModel(ModelID modelId, ImportedModel&& importedModel) noexcept;
		void CompleteModelLoad(
			ModelID modelId,
			const TaskCompletionInfo& completion,
			ImportedModel&& importedModel) noexcept;

		MeshID CreateMesh() noexcept;
		MaterialID CreateMaterial() noexcept;
		ModelID CreateModel(
			const std::filesystem::path& canonicalPath,
			AssetState initialState = AssetState::LoadingCpu) noexcept;

		ModelID FindModel(const std::filesystem::path& canonicalPath) const noexcept;
		bool DetachTerminalModelPath(
			const std::filesystem::path& canonicalPath,
			ModelID modelId) noexcept;
		bool RefreshModelState(ModelID modelId) noexcept;

	public:
		static void ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;

		static void SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
		TextureRegistry* m_TextureRegistry = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;
		MaterialTextureSamplingSettings m_MaterialTextureSampling{};

		MeshIDCounter m_MeshIdCounter{ ReservedMeshCount };
		MaterialIDCounter m_MaterialIdCounter{ ReservedMaterialCount };
		ModelIDCounter m_ModelIdCounter{ ReservedModelCount };

		MeshContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
		ModelContainer m_ModelContainer;
		std::unordered_map<ModelID, TaskHandle> m_ModelLoadTasks;
		std::unordered_set<ModelID> m_PendingModels;
	};
}
