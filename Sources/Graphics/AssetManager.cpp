#include "Core/Precompiled.h"
#include "Graphics/AssetManager.h"
#include "Core/Task/TaskSystem.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace gglab
{
	namespace
	{
		struct ModelLoadJob
		{
			ImportedModel m_Model;
		};

		struct TextureLoadJob
		{
			TextureAssetData m_TextureData;
		};
	}
	AssetManager::AssetManager(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TaskSystem(createInfo.m_TaskSystem),
		m_TransferManager(createInfo.m_TransferManager),
		m_TextureRegistry(createInfo.m_TextureRegistry),
		m_SamplerRegistry(createInfo.m_SamplerRegistry),
		m_MaterialTextureSampling(createInfo.m_MaterialTextureSampling)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TaskSystem != nullptr, "TaskSystem is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_TextureRegistry != nullptr, "TextureRegistry is null!");
		GGLAB_ASSERT_MSG(m_SamplerRegistry != nullptr, "SamplerRegistry is null!");
	}

	AssetManager::~AssetManager() = default;

	ModelID AssetManager::LoadModel(const std::filesystem::path& path) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::LoadModel received an empty path.");
			return InvalidModelID;
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::LoadModel received a missing model file: '{}'.",
				canonicalPath.string());
			return InvalidModelID;
		}

		// Check if model is already loaded
		if (auto existing = FindModel(canonicalPath); existing.IsValid())
		{
			return existing;
		}

		// Check extension, load glTF file
		const auto getModelFileType = [](std::string extension) -> ModelType
			{
				std::ranges::transform(extension, extension.begin(),
					[](unsigned char c)
					{
						return static_cast<char>(std::tolower(c));
					});
				if (extension == ".gltf") { return ModelType::GlTF; }
				return ModelType::Invalid;
			};

		const auto extension = canonicalPath.extension().string();
		switch (getModelFileType(extension))
		{
		case ModelType::GlTF:
			return LoadModelGltf(canonicalPath);
		case ModelType::Procedural:
		case ModelType::Invalid:
			return ModelID::Invalid();
		default:
			GGLAB_UNREACHABLE("Unknown model file type.");
		}
	}

	TextureID AssetManager::LoadTexture(const std::filesystem::path& path, TextureSemantic semantic) noexcept
	{
		return m_TextureRegistry->LoadTexture(path, semantic);
	}

	AssetManager::ModelLoadRequest AssetManager::LoadModelAsync(
		const std::filesystem::path& path,
		TaskPriority priority) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::LoadModelAsync received an empty path.");
			return {};
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"AssetManager::LoadModelAsync received a missing model file: '{}'.",
				canonicalPath.string());
			return {};
		}

		if (const ModelID existing = FindModel(canonicalPath); existing.IsValid())
		{
			const auto task = m_ModelLoadTasks.find(existing);
			return {
				.m_ModelId = existing,
				.m_Task = task != m_ModelLoadTasks.end() ? task->second : TaskHandle{},
			};
		}

		const ModelID modelId = CreateModel(canonicalPath, AssetState::Queued);
		Model* model = GetModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		model->m_Name = StringID(canonicalPath.filename().generic_string());
		model->m_Type = ModelType::GlTF;

		auto job = std::make_shared<ModelLoadJob>();
		const ModelImportSettings importSettings = m_MaterialTextureSampling;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format("Asset.ModelImport: {}", canonicalPath.filename().generic_string()),
				.m_Priority = priority,
			},
			[canonicalPath, importSettings, job](std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(
					canonicalPath,
					importSettings,
					stopToken);
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				job->m_Model = std::move(result.m_Model);
				return TaskResult::Success();
			},
			[this, modelId, job](const TaskCompletionInfo& completion) noexcept
			{
				CompleteModelLoad(modelId, completion, std::move(job->m_Model));
			});
		if (!task.IsValid())
		{
			model->m_State = AssetState::Failed;
			return { .m_ModelId = modelId };
		}

		m_ModelLoadTasks.emplace(modelId, task);
		return {
			.m_ModelId = modelId,
			.m_Task = task,
		};
	}

	AssetManager::TextureLoadRequest AssetManager::LoadTextureAsync(
		const std::filesystem::path& path,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::LoadTextureAsync received an empty path.");
			return {};
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"AssetManager::LoadTextureAsync received a missing texture file: '{}'.",
				canonicalPath.string());
			return {};
		}

		const TextureImportSettings importSettings = MakeTextureImportSettings(semantic);
		if (const TextureID existing = m_TextureRegistry->FindTexture(
			canonicalPath, importSettings); existing.IsValid())
		{
			const auto task = m_TextureLoadTasks.find(existing);
			return {
				.m_TextureId = existing,
				.m_Task = task != m_TextureLoadTasks.end() ? task->second : TaskHandle{},
			};
		}

		const TextureID textureId = m_TextureRegistry->CreateTexture(
			canonicalPath,
			importSettings);
		if (!textureId.IsValid())
		{
			return {};
		}
		Texture* texture = m_TextureRegistry->GetTexture(textureId);
		GGLAB_ASSERT_NOT_NULL(texture);
		texture->m_State = AssetState::Queued;
		texture->m_Semantic = semantic;

		auto job = std::make_shared<TextureLoadJob>();
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format("Asset.TextureDecode: {}", canonicalPath.filename().generic_string()),
				.m_Priority = priority,
			},
			[canonicalPath, importSettings, job](std::stop_token stopToken) noexcept
			{
				if (stopToken.stop_requested())
				{
					return TaskResult::Success();
				}
				job->m_TextureData = TextureLoader::LoadTextureData(
					canonicalPath,
					importSettings);
				if (!job->m_TextureData.IsValid())
				{
					return TaskResult::Failure(std::format(
						"Failed to decode texture '{}'.",
						canonicalPath.string()));
				}
				return TaskResult::Success();
			},
			[this, textureId, semantic, job](const TaskCompletionInfo& completion) noexcept
			{
				CompleteTextureLoad(
					textureId,
					semantic,
					completion,
					std::move(job->m_TextureData));
			});
		if (!task.IsValid())
		{
			texture->m_State = AssetState::Failed;
			return { .m_TextureId = textureId };
		}

		m_TextureLoadTasks.emplace(textureId, task);
		return {
			.m_TextureId = textureId,
			.m_Task = task,
		};
	}

	Mesh* AssetManager::GetMesh(MeshID meshId) noexcept
	{
		return const_cast<Mesh*>(std::as_const(*this).GetMesh(meshId));
	}

	const Mesh* AssetManager::GetMesh(MeshID meshId) const noexcept
	{
		auto iterator = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iterator != m_MeshContainer.m_MeshIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	Material* AssetManager::GetMaterial(MaterialID materialId) noexcept
	{
		return const_cast<Material*>(std::as_const(*this).GetMaterial(materialId));
	}

	const Material* AssetManager::GetMaterial(MaterialID materialId) const noexcept
	{
		auto iterator = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iterator != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	Model* AssetManager::GetModel(ModelID modelId) noexcept
	{
		return const_cast<Model*>(std::as_const(*this).GetModel(modelId));
	}

	const Model* AssetManager::GetModel(ModelID modelId) const noexcept
	{
		auto iterator = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iterator != m_ModelContainer.m_ModelIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	MeshID AssetManager::AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept
	{
		GGLAB_ASSERT(mesh);

		// Assign Mesh ID
		auto meshId = mesh->m_Id;
		if (!meshId.IsValid())
		{
			meshId = m_MeshIdCounter.Acquire();
			mesh->m_Id = meshId;
		}

		// Check if mesh already exists
		const auto iterator = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iterator != m_MeshContainer.m_MeshIDMap.end())
		{
			return meshId;
		}

		if (!mesh->m_HasBounds)
		{
			ComputeMeshBounds(*mesh, meshUploadData.m_VerticesData);
		}

		m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		meshUploadData.m_MeshId = meshId;
		GetMesh(meshId)->m_State = AssetState::CpuReady;

		// Upload Mesh to GPU
		auto batch = m_TransferManager->BeginBatch();
		UploadMesh(meshUploadData, batch);
		const RHIFencePoint uploadFence = batch.Submit(true);
		CompleteMeshUpload(
			meshId,
			uploadFence.IsValid() && GetMesh(meshId)->m_State == AssetState::GpuProcessing);

		return meshId;
	}

	MaterialID AssetManager::AddMaterial(std::unique_ptr<Material>&& material) noexcept
	{
		GGLAB_ASSERT(material);

		auto materialId = material->m_Id;
		if (!materialId.IsValid())
		{
			materialId = m_MaterialIdCounter.Acquire();
			material->m_Id = materialId;
		}

		const auto iterator = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iterator != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return materialId;
		}

		m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::move(material));

		return materialId;
	}

	ModelID AssetManager::AddModel(std::unique_ptr<Model>&& model) noexcept
	{
		GGLAB_ASSERT(model);

		auto modelId = model->m_Id;
		if (!modelId.IsValid())
		{
			modelId = m_ModelIdCounter.Acquire();
			model->m_Id = modelId;
		}

		const auto iterator = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iterator != m_ModelContainer.m_ModelIDMap.end())
		{
			// This id is already have.
			return modelId;
		}

		if (model->m_Type == ModelType::Invalid)
		{
			model->m_Type = ModelType::Procedural;
		}
		model->m_State = AssetState::Ready;

		m_ModelContainer.m_ModelIDMap.emplace(modelId, std::move(model));

		return modelId;
	}

	uint32_t AssetManager::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		return m_TextureRegistry->ResolveSrvIndex(textureId, fallback);
	}

	MaterialTextureBindingGPU AssetManager::ResolveTextureBinding(const MaterialTextureBinding& binding, ReservedTextureIDIndex fallback, SamplerPreset fallbackSampler) const noexcept
	{
		MaterialTextureBindingGPU bindingGpu{};

		bindingGpu.TextureSamplerBinding.TextureIndex = ResolveSrvIndex(binding.m_TextureId, fallback);
		bindingGpu.TextureSamplerBinding.SamplerIndex = m_SamplerRegistry->ResolveSamplerIndex(binding.m_SamplerId, fallbackSampler);

		bindingGpu.TexCoordIndex = binding.m_TexCoordIndex;
		bindingGpu.Padding = 0;

		return bindingGpu;
	}

	void AssetManager::UploadMesh(const MeshUploadData& uploadData, TransferBatch& transferBatch) noexcept
	{
		auto* mesh = GetMesh(uploadData.m_MeshId);
		if (mesh == nullptr)
		{
			GGLAB_ASSERT_MSG(false, "UploadMesh: Invalid MeshID, check it!");
			return;
		}
		mesh->m_State = AssetState::UploadQueued;

		const auto& verticesData = uploadData.m_VerticesData;
		const auto& indicesData = uploadData.m_IndicesData;

		const auto vertexCount = static_cast<uint32_t>(verticesData.size());
		const auto indexCount = static_cast<uint32_t>(indicesData.size());

		mesh->m_VertexCount = vertexCount;
		mesh->m_IndexCount = indexCount;

		const auto vertexBufferSize = static_cast<uint64_t>(vertexCount) * sizeof(Vertex);
		const auto indexBufferSize = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);

		if (vertexBufferSize == 0 || indexBufferSize == 0)
		{
			mesh->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::UploadMesh received an empty mesh.");
			return;
		}

		RHIBufferDesc vertexBufferDesc{};
		vertexBufferDesc.m_SizeInBytes = vertexBufferSize;
		vertexBufferDesc.m_StrideInBytes = sizeof(Vertex);
		vertexBufferDesc.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest;

		RHIBufferDesc indexBufferDesc{};
		indexBufferDesc.m_SizeInBytes = indexBufferSize;
		indexBufferDesc.m_StrideInBytes = sizeof(uint32_t);
		indexBufferDesc.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest;

		const std::string_view meshName = mesh->m_Name.Name().empty() ?
			std::string_view("UnnamedMesh") : mesh->m_Name.Name();
		const RHIResourceDebugIdentityDesc vertexDebugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = "Mesh.VertexBuffer",
			.m_Label = meshName,
			.m_StableId = uploadData.m_MeshId.Value(),
		};
		const RHIResourceDebugIdentityDesc indexDebugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = "Mesh.IndexBuffer",
			.m_Label = meshName,
			.m_StableId = uploadData.m_MeshId.Value(),
		};
		const RHIBufferHandle vertexBuffer =
			m_Device->CreateBuffer(vertexBufferDesc, vertexDebugIdentity);
		const RHIBufferHandle indexBuffer =
			m_Device->CreateBuffer(indexBufferDesc, indexDebugIdentity);
		if (!vertexBuffer.IsValid() || !indexBuffer.IsValid())
		{
			mesh->m_State = AssetState::Failed;
			if (vertexBuffer.IsValid())
			{
				m_Device->DestroyBuffer(vertexBuffer);
			}
			if (indexBuffer.IsValid())
			{
				m_Device->DestroyBuffer(indexBuffer);
			}

			GGLAB_LOG_GRAPHICS_ERROR("AssetManager::UploadMesh failed to create RHI mesh buffers.");
			return;
		}

		mesh->m_VertexBuffer = RHIBufferOwner(m_Device, vertexBuffer);
		mesh->m_IndexBuffer = RHIBufferOwner(m_Device, indexBuffer);

		const bool vertexUploadSucceeded = transferBatch.UploadBuffer(
			mesh->m_VertexBuffer.Get(), 0, verticesData.data(), vertexBufferSize);
		const bool indexUploadSucceeded = transferBatch.UploadBuffer(
			mesh->m_IndexBuffer.Get(), 0, indicesData.data(), indexBufferSize);
		GGLAB_ASSERT_MSG(vertexUploadSucceeded && indexUploadSucceeded,
			"AssetManager failed to record mesh buffer uploads.");
		if (!vertexUploadSucceeded || !indexUploadSucceeded)
		{
			mesh->m_State = AssetState::Failed;
			mesh->m_VertexBuffer.Reset();
			mesh->m_IndexBuffer.Reset();
			return;
		}

		if (vertexBufferSize > std::numeric_limits<uint32_t>::max() ||
			indexBufferSize > std::numeric_limits<uint32_t>::max())
		{
			mesh->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR("AssetManager::UploadMesh mesh buffers exceed RHI binding size limits.");
			mesh->m_VertexBuffer.Reset();
			mesh->m_IndexBuffer.Reset();
			return;
		}

		mesh->m_VertexBufferBinding.m_Buffer = mesh->m_VertexBuffer.Get();
		mesh->m_VertexBufferBinding.m_Offset = 0;
		mesh->m_VertexBufferBinding.m_Stride = sizeof(Vertex);
		mesh->m_VertexBufferBinding.m_SizeInBytes = static_cast<uint32_t>(vertexBufferSize);

		mesh->m_IndexBufferBinding.m_Buffer = mesh->m_IndexBuffer.Get();
		mesh->m_IndexBufferBinding.m_Offset = 0;
		mesh->m_IndexBufferBinding.m_SizeInBytes = static_cast<uint32_t>(indexBufferSize);
		mesh->m_IndexBufferBinding.m_Format = RHIFormat::R32Uint;

		mesh->m_State = AssetState::GpuProcessing;
	}

	void AssetManager::CompleteMeshUpload(MeshID meshId, bool succeeded) noexcept
	{
		auto* mesh = GetMesh(meshId);
		if (!mesh)
		{
			return;
		}
		mesh->m_State = succeeded ? AssetState::Ready : AssetState::Failed;
		mesh->m_IsUploaded = succeeded;
	}

	bool AssetManager::PublishImportedTexture(
		TextureID textureId,
		TextureSemantic semantic,
		TextureAssetData&& textureData) noexcept
	{
		Texture* texture = m_TextureRegistry->GetTexture(textureId);
		if (!texture)
		{
			return false;
		}
		texture->m_State = AssetState::CpuReady;

		auto uploadData = m_TextureRegistry->MakeTextureUploadData(
			textureId,
			std::move(textureData),
			semantic);
		auto batch = m_TransferManager->BeginBatch();
		const bool recorded = m_TextureRegistry->UploadTexture(uploadData, batch);
		const RHIFencePoint uploadFence = batch.Submit(true);
		const bool succeeded = recorded && uploadFence.IsValid();
		m_TextureRegistry->CompleteTextureUpload(textureId, succeeded);
		return succeeded;
	}

	bool AssetManager::PublishImportedModel(
		ModelID modelId,
		ImportedModel&& importedModel) noexcept
	{
		Model* model = GetModel(modelId);
		if (!model || importedModel.m_Meshes.empty())
		{
			return false;
		}

		model->m_Name = StringID(importedModel.m_Name);
		model->m_Type = importedModel.m_Type;
		model->m_State = AssetState::CpuReady;

		std::vector<TextureID> textureIds(importedModel.m_Textures.size());
		std::vector<TextureRegistry::TextureUploadData> textureUploads;
		for (size_t textureIndex = 0; textureIndex < importedModel.m_Textures.size(); ++textureIndex)
		{
			ImportedTexture& importedTexture = importedModel.m_Textures[textureIndex];
			TextureID textureId = m_TextureRegistry->FindTexture(
				importedTexture.m_CanonicalPath,
				importedTexture.m_ImportSettings);
			if (!textureId.IsValid() && importedTexture.m_Data.IsValid())
			{
				textureId = m_TextureRegistry->CreateTexture(
					importedTexture.m_CanonicalPath,
					importedTexture.m_ImportSettings);
				if (textureId.IsValid())
				{
					textureUploads.emplace_back(m_TextureRegistry->MakeTextureUploadData(
						textureId,
						std::move(importedTexture.m_Data),
						importedTexture.m_Semantic));
				}
			}
			textureIds[textureIndex] = textureId;
		}

		std::vector<MaterialID> materialIds;
		materialIds.reserve(std::max<size_t>(importedModel.m_Materials.size(), 1));
		for (ImportedMaterial& importedMaterial : importedModel.m_Materials)
		{
			auto material = std::make_unique<Material>();
			static_cast<MaterialProperties&>(*material) = importedMaterial.m_Properties;
			material->m_Name = StringID(importedMaterial.m_Name);
			for (uint32_t slotIndex = 0;
				slotIndex < utils::ToIndex(MaterialTextureSlot::Count);
				++slotIndex)
			{
				const ImportedMaterialTextureBinding& importedBinding =
					importedMaterial.m_TextureBindings[slotIndex];
				if (importedBinding.m_TextureIndex ==
					ImportedMaterialTextureBinding::InvalidTextureIndex)
				{
					continue;
				}

				MaterialTextureBinding binding{};
				if (importedBinding.m_TextureIndex < textureIds.size())
				{
					binding.m_TextureId = textureIds[importedBinding.m_TextureIndex];
				}
				binding.m_SamplerId = m_SamplerRegistry->GetOrCreateSampler(
					importedBinding.m_SamplerKey);
				binding.m_TexCoordIndex = importedBinding.m_TexCoordIndex;
				SetMaterialTexture(
					*material,
					static_cast<MaterialTextureSlot>(slotIndex),
					binding);
			}
			materialIds.push_back(AddMaterial(std::move(material)));
		}
		if (materialIds.empty())
		{
			materialIds.push_back(AddMaterial(std::make_unique<Material>()));
		}

		std::vector<MeshID> meshIds(importedModel.m_Meshes.size());
		std::vector<MeshUploadData> meshUploads(importedModel.m_Meshes.size());
		for (size_t meshIndex = 0; meshIndex < importedModel.m_Meshes.size(); ++meshIndex)
		{
			ImportedMesh& importedMesh = importedModel.m_Meshes[meshIndex];
			const MeshID meshId = CreateMesh();
			meshIds[meshIndex] = meshId;
			Mesh* mesh = GetMesh(meshId);
			GGLAB_ASSERT_NOT_NULL(mesh);
			mesh->m_Name = StringID(importedMesh.m_Name);
			mesh->m_Sphere = importedMesh.m_Sphere;
			mesh->m_Aabb = importedMesh.m_Aabb;
			mesh->m_HasBounds = importedMesh.m_HasBounds;
			mesh->m_State = AssetState::CpuReady;

			MeshUploadData& upload = meshUploads[meshIndex];
			upload.m_MeshId = meshId;
			upload.m_VerticesData = std::move(importedMesh.m_Vertices);
			upload.m_IndicesData = std::move(importedMesh.m_Indices);
		}

		model->m_MeshInstance.clear();
		model->m_MeshInstance.reserve(importedModel.m_MeshInstances.size());
		for (const ImportedModelMesh& importedInstance : importedModel.m_MeshInstances)
		{
			if (importedInstance.m_MeshIndex >= meshIds.size())
			{
				continue;
			}
			const uint32_t materialIndex =
				importedInstance.m_MaterialIndex < materialIds.size() ?
				importedInstance.m_MaterialIndex : 0;
			model->m_MeshInstance.push_back({
				.m_MeshId = meshIds[importedInstance.m_MeshIndex],
				.m_MaterialId = materialIds[materialIndex],
				.m_LocalTransform = importedInstance.m_LocalTransform,
			});
		}
		if (model->m_MeshInstance.empty())
		{
			for (size_t meshIndex = 0; meshIndex < meshIds.size(); ++meshIndex)
			{
				const uint32_t materialIndex =
					importedModel.m_Meshes[meshIndex].m_MaterialIndex < materialIds.size() ?
					importedModel.m_Meshes[meshIndex].m_MaterialIndex : 0;
				model->m_MeshInstance.push_back({
					.m_MeshId = meshIds[meshIndex],
					.m_MaterialId = materialIds[materialIndex],
				});
			}
		}

		model->m_State = AssetState::UploadQueued;
		auto batch = m_TransferManager->BeginBatch();
		std::vector<TextureID> uploadedTextureIds;
		for (const auto& textureUpload : textureUploads)
		{
			if (m_TextureRegistry->UploadTexture(textureUpload, batch))
			{
				uploadedTextureIds.push_back(textureUpload.m_TextureId);
			}
		}
		for (const MeshUploadData& meshUpload : meshUploads)
		{
			UploadMesh(meshUpload, batch);
		}

		const RHIFencePoint uploadFence = batch.Submit(true);
		for (TextureID textureId : uploadedTextureIds)
		{
			m_TextureRegistry->CompleteTextureUpload(textureId, uploadFence.IsValid());
		}

		bool meshesReady = uploadFence.IsValid();
		for (const MeshUploadData& meshUpload : meshUploads)
		{
			const Mesh* mesh = GetMesh(meshUpload.m_MeshId);
			const bool succeeded = uploadFence.IsValid() &&
				mesh && mesh->m_State == AssetState::GpuProcessing;
			CompleteMeshUpload(meshUpload.m_MeshId, succeeded);
			meshesReady &= succeeded;
		}
		model->m_State = meshesReady ? AssetState::Ready : AssetState::Failed;
		return meshesReady;
	}

	void AssetManager::CompleteModelLoad(
		ModelID modelId,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		m_ModelLoadTasks.erase(modelId);
		Model* model = GetModel(modelId);
		if (!model)
		{
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			model->m_State = AssetState::Cancelled;
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			model->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async model import '{}' failed: {}",
				completion.m_Name,
				completion.m_Error);
			return;
		}

		const uint32_t meshInstanceCount = static_cast<uint32_t>(
			importedModel.m_MeshInstances.size());
		if (!PublishImportedModel(modelId, std::move(importedModel)))
		{
			model->m_State = AssetState::Failed;
			return;
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"Async model {} published (instances={}, queueMs={:.2f}, cpuMs={:.2f}).",
			modelId.Value(),
			meshInstanceCount,
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds);
	}

	void AssetManager::CompleteTextureLoad(
		TextureID textureId,
		TextureSemantic semantic,
		const TaskCompletionInfo& completion,
		TextureAssetData&& textureData) noexcept
	{
		m_TextureLoadTasks.erase(textureId);
		Texture* texture = m_TextureRegistry->GetTexture(textureId);
		if (!texture)
		{
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			texture->m_State = AssetState::Cancelled;
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			texture->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async texture decode '{}' failed: {}",
				completion.m_Name,
				completion.m_Error);
			return;
		}

		if (!PublishImportedTexture(textureId, semantic, std::move(textureData)))
		{
			texture->m_State = AssetState::Failed;
			return;
		}
		GGLAB_LOG_GRAPHICS_INFO(
			"Async texture {} published (queueMs={:.2f}, cpuMs={:.2f}).",
			textureId.Value(),
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds);
	}

	ModelID AssetManager::LoadModelGltf(const std::filesystem::path& path) noexcept
	{
		ModelImportResult result = ModelImporter::Import(path, m_MaterialTextureSampling);
		if (!result.Succeeded())
		{
			GGLAB_LOG_GRAPHICS_ERROR("{}", result.m_Error);
			return InvalidModelID;
		}

		const ModelID modelId = CreateModel(result.m_Model.m_CanonicalPath);
		PublishImportedModel(modelId, std::move(result.m_Model));
		return modelId;
	}
	MeshID AssetManager::CreateMesh() noexcept
	{
		const auto meshId = m_MeshIdCounter.Acquire();
		auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
		GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace MeshID & meshPtr pair failed.");
		idMeshPair.first->second->m_State = AssetState::LoadingCpu;

		return meshId;
	}

	MaterialID AssetManager::CreateMaterial() noexcept
	{
		const auto materialId = m_MaterialIdCounter.Acquire();
		auto idMatPair = m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::make_unique<Material>());
		GGLAB_ASSERT_MSG(idMatPair.second == true, "Emplace MaterialID & materialPtr pair failed.");

		return materialId;
	}

	ModelID AssetManager::CreateModel(
		const std::filesystem::path& canonicalPath,
		AssetState initialState) noexcept
	{
		const auto modelId = m_ModelIdCounter.Acquire();
		auto pathIdPair = m_ModelContainer.m_PathIDMap.emplace(canonicalPath, modelId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path & ModelID pair failed.");

		auto idModelPair = m_ModelContainer.m_ModelIDMap.emplace(modelId, std::make_unique<Model>());
		GGLAB_ASSERT_MSG(idModelPair.second == true, "Emplace ModelID & ModelPtr pair failed.");
		idModelPair.first->second->m_State = initialState;
		return modelId;
	}

	ModelID AssetManager::FindModel(const std::filesystem::path& canonicalPath) const noexcept
	{
		auto& modelPathMap = m_ModelContainer.m_PathIDMap;
		auto iterator = modelPathMap.find(canonicalPath);
		if (iterator != modelPathMap.end())
		{
			return iterator->second;
		}
		return InvalidModelID;
	}

	void AssetManager::ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept
	{
		if (vertices.empty())
		{
			mesh.m_Aabb = math::Aabb{};
			mesh.m_Sphere = math::Sphere{};
			mesh.m_HasBounds = false;
			return;
		}

		const auto* firstPos = std::addressof(vertices[0].m_Position);
		constexpr size_t stride = sizeof(Vertex);

		mesh.m_Aabb = math::CreateAabbFromPoints(vertices.size(), firstPos, stride);
		mesh.m_Sphere = math::CreateSphere(mesh.m_Aabb);
		//mesh.m_Sphere = math::CreateSphereFromPoints(vertices.size(), firstPos, stride);

		mesh.m_HasBounds = true;
	}

	void AssetManager::SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept
	{
		switch (slot)
		{
		case MaterialTextureSlot::BaseColor:
			material.m_BaseColorBinding = binding;
			break;
		case MaterialTextureSlot::MetallicRoughness:
			material.m_MetallicRoughnessBinding = binding;
			break;
		case MaterialTextureSlot::Normal:
			material.m_NormalBinding = binding;
			break;
		case MaterialTextureSlot::Occlusion:
			material.m_OcclusionBinding = binding;
			break;
		case MaterialTextureSlot::Emissive:
			material.m_EmissiveBinding = binding;
			break;
		default:
			GGLAB_UNREACHABLE("Unknown MaterialTextureSlot.");
		}
	}
}
