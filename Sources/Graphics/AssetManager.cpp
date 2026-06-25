#include "Core/Precompiled.h"
#include "Graphics/AssetManager.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <limits>

namespace gglab
{
	namespace
	{
		constexpr float TangentLengthEpsilon = 1.0e-4f;
		constexpr float TangentLengthSqEpsilon = TangentLengthEpsilon * TangentLengthEpsilon;

		// MaterialTextureSlot to assimp Texture type mapping
		aiTextureType ToAssimpTextureType(MaterialTextureSlot slot) noexcept
		{
			switch (slot)
			{
			case MaterialTextureSlot::BaseColor:
				return aiTextureType_BASE_COLOR;
			case MaterialTextureSlot::MetallicRoughness:
				return aiTextureType_GLTF_METALLIC_ROUGHNESS;
			case MaterialTextureSlot::Normal:
				return aiTextureType_NORMALS;
			case MaterialTextureSlot::Occlusion:
				return aiTextureType_AMBIENT_OCCLUSION;
			case MaterialTextureSlot::Emissive:
				return aiTextureType_EMISSIVE;
			default:
				return aiTextureType_NONE;
			}
		}

		RHITextureAddressMode ToRHITextureAddressMode(aiTextureMapMode mode) noexcept
		{
			switch (mode)
			{
			case aiTextureMapMode_Wrap:
				return RHITextureAddressMode::Wrap;
			case aiTextureMapMode_Clamp:
				return RHITextureAddressMode::Clamp;
			case aiTextureMapMode_Mirror:
				return RHITextureAddressMode::Mirror;
			case aiTextureMapMode_Decal:
				return RHITextureAddressMode::Clamp;
			default:
				return RHITextureAddressMode::Wrap;
			}
		}

		static SamplerKey MakeSamplerKeyFromAssimpTextureParams(const aiTextureMapMode mapMode[3]) noexcept
		{
			SamplerKey key{};

			key.m_Filter = RHISamplerFilter::MinMagMipLinear;

			key.m_AddressU = ToRHITextureAddressMode(mapMode[0]);
			key.m_AddressV = ToRHITextureAddressMode(mapMode[1]);
			key.m_AddressW = ToRHITextureAddressMode(mapMode[2]);

			key.m_MipLODBias = 0.0f;
			key.m_MaxAnisotropy = 1;
			key.m_CompareOp = RHICompareOp::Never;

			key.m_BorderColor[0] = 0.0f;
			key.m_BorderColor[1] = 0.0f;
			key.m_BorderColor[2] = 0.0f;
			key.m_BorderColor[3] = 0.0f;

			key.m_MinLOD = 0.0f;

			return key;
		}

		Vector4 MakeFallbackTangent(const Vector3& normal) noexcept
		{
			Vector3 n = normal;
			if (n.LengthSquared() <= TangentLengthSqEpsilon)
			{
				n = Vector3::UnitY;
			}
			else
			{
				n.Normalize();
			}

			const Vector3 up = (std::abs(n.y) < 0.999f) ? Vector3::UnitY : Vector3::UnitZ;
			Vector3 tangent = up.Cross(n);
			if (tangent.LengthSquared() <= TangentLengthSqEpsilon)
			{
				tangent = Vector3::UnitX;
			}
			else
			{
				tangent.Normalize();
			}

			return Vector4(tangent.x, tangent.y, tangent.z, 1.0f);
		}
	}

	AssetManager::AssetManager(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_TextureRegistry(createInfo.m_TextureRegistry),
		m_SamplerRegistry(createInfo.m_SamplerRegistry)
	{
		GGLAB_ASSERT_MSG(m_DX12Device != nullptr, "DX12Device is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_TextureRegistry != nullptr, "TextureRegistry is null!");
		GGLAB_ASSERT_MSG(m_SamplerRegistry != nullptr, "SamplerRegistry is null!");
	}

	AssetManager::~AssetManager() = default;

	ModelID AssetManager::LoadModel(const std::filesystem::path& path) noexcept
	{
		const auto canonicalPath = utils::Canonical(path);

		// Check if model is already loaded
		if (auto existing = FindModel(canonicalPath); existing.IsValid())
		{
			return existing;
		}

		// Check extension, load glTF file
		const auto getModelFileType = [](const std::string& extension) -> ModelType
			{
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

		m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		meshUploadData.m_MeshId = meshId;

		// Upload Mesh to GPU
		auto batch = m_TransferManager->BeginBatch();
		auto* transferContext = m_TransferManager->GetTransferContext();
		UploadMesh(meshUploadData, *transferContext);
		batch.Submit(true);

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

	void AssetManager::UploadMesh(const MeshUploadData& uploadData, RHITransferContext& transferContext) noexcept
	{
		auto* mesh = GetMesh(uploadData.m_MeshId);
		if (mesh == nullptr)
		{
			GGLAB_ASSERT_MSG(false, "UploadMesh: Invalid MeshID, check it!");
			return;
		}

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
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::UploadMesh received an empty mesh.");
			return;
		}

		RHIBufferDesc vertexBufferDesc{};
		vertexBufferDesc.m_SizeInBytes = vertexBufferSize;
		vertexBufferDesc.m_StrideInBytes = sizeof(Vertex);
		vertexBufferDesc.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest;
		vertexBufferDesc.m_DebugName = "AssetManager.Mesh.VertexBuffer";

		RHIBufferDesc indexBufferDesc{};
		indexBufferDesc.m_SizeInBytes = indexBufferSize;
		indexBufferDesc.m_StrideInBytes = sizeof(uint32_t);
		indexBufferDesc.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest;
		indexBufferDesc.m_DebugName = "AssetManager.Mesh.IndexBuffer";

		const RHIBufferHandle vertexBuffer = m_DX12Device->CreateBuffer(vertexBufferDesc);
		const RHIBufferHandle indexBuffer = m_DX12Device->CreateBuffer(indexBufferDesc);
		if (!vertexBuffer.IsValid() || !indexBuffer.IsValid())
		{
			if (vertexBuffer.IsValid())
			{
				m_DX12Device->DestroyBuffer(vertexBuffer);
			}
			if (indexBuffer.IsValid())
			{
				m_DX12Device->DestroyBuffer(indexBuffer);
			}

			GGLAB_LOG_GRAPHICS_ERROR("AssetManager::UploadMesh failed to create RHI mesh buffers.");
			return;
		}

		mesh->m_VertexBuffer = RHIBufferOwner(m_DX12Device, vertexBuffer);
		mesh->m_IndexBuffer = RHIBufferOwner(m_DX12Device, indexBuffer);

		transferContext.UploadBuffer(verticesData.data(), vertexBufferSize, mesh->m_VertexBuffer.Get());
		transferContext.UploadBuffer(indicesData.data(), indexBufferSize, mesh->m_IndexBuffer.Get());

		if (vertexBufferSize > std::numeric_limits<uint32_t>::max() ||
			indexBufferSize > std::numeric_limits<uint32_t>::max())
		{
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

		mesh->m_IsUploaded = true;
	}

	ModelID AssetManager::LoadModelGltf(const std::filesystem::path& path) noexcept
	{
		const auto canonicalPath = utils::Canonical(path);
		const auto directory = canonicalPath.parent_path();

		// Load model file
		Assimp::Importer importer;
		constexpr uint32_t importFlags =
			aiProcess_ConvertToLeftHanded |
			aiProcess_Triangulate |			// Index count per face must be 3
			aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace |
			aiProcess_JoinIdenticalVertices |
			aiProcess_ImproveCacheLocality |
			aiProcess_RemoveRedundantMaterials |
			aiProcess_SortByPType |
			aiProcess_OptimizeMeshes |
			aiProcess_OptimizeGraph;

		const auto* aiScene = importer.ReadFile(path.string(), importFlags);
		GGLAB_ASSERT_MSG(aiScene != nullptr, "Assimp load file failed.");
		GGLAB_ASSERT_MSG(aiScene->HasMeshes(), "Model file do not has mesh data.");

		// Parse material datas
		const auto aiMaterialCount = aiScene->mNumMaterials;
		std::vector<MaterialID> materialIds(aiMaterialCount);
		std::vector<TextureRegistry::TextureUploadData> texUploadDatas;

		for (uint32_t materialIndex = 0; materialIndex < aiMaterialCount; ++materialIndex)
		{
			const auto materialId = CreateMaterial();
			materialIds[materialIndex] = materialId;

			auto* material = GetMaterial(materialId);
			material->m_Id = materialId;

			const auto& aiMaterial = aiScene->mMaterials[materialIndex];
			for (uint32_t slotIndex = 0; slotIndex < utils::ToIndex(MaterialTextureSlot::Count); ++slotIndex)
			{
				const auto slot = static_cast<MaterialTextureSlot>(slotIndex);
				const auto semantic = GetMaterialTextureSlotSemantic(slot);
				const auto aiTexType = ToAssimpTextureType(slot);
				if (aiTexType == aiTextureType_NONE)
				{
					continue;
				}

				aiString aiTexPath{};
				aiTextureMapping mapping = aiTextureMapping_UV;
				unsigned int uvIndex = 0;
				ai_real blend = 1.0f;
				aiTextureOp op = aiTextureOp_Multiply;
				aiTextureMapMode mapMode[3] =
				{
					aiTextureMapMode_Wrap,
					aiTextureMapMode_Wrap,
					aiTextureMapMode_Wrap
				};

				if (aiMaterial->GetTexture(aiTexType,
					0,
					&aiTexPath,
					&mapping,
					&uvIndex,
					&blend,
					&op,
					mapMode) != aiReturn_SUCCESS)
				{
					continue;
				}

				const auto texPath = directory / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);

				auto texId = m_TextureRegistry->FindTexture(canonicalTexPath);
				if (!texId.IsValid())
				{
					texId = m_TextureRegistry->CreateTexture(canonicalTexPath);
					texUploadDatas.emplace_back(m_TextureRegistry->MakeTextureUploadData(texId, canonicalTexPath, semantic));
				}

				const auto samplerKey = MakeSamplerKeyFromAssimpTextureParams(mapMode);
				const auto samplerId = m_SamplerRegistry->GetOrCreateSampler(samplerKey);

				MaterialTextureBinding binding{};
				binding.m_TextureId = texId;
				binding.m_SamplerId = samplerId;
				if (uvIndex > 1)
				{
					GGLAB_LOG_GRAPHICS_WARN("Texture '{}' requests TEXCOORD{}, but only TEXCOORD0/1 are supported. Falling back to TEXCOORD0.",
						canonicalTexPath.string(),
						uvIndex);
					uvIndex = 0;
				}
				binding.m_TexCoordIndex = uvIndex;

				SetMaterialTexture(*material, slot, binding);
			}

			// Get factors
			aiColor4D baseColor;
			if (aiMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) == aiReturn_SUCCESS)
			{
				material->m_BaseColor = Color(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
			}

			float metallicFactor = 0.0f;
			if (aiMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor) == aiReturn_SUCCESS)
			{
				material->m_MetallicFactor = metallicFactor;
			}

			float roughnessFactor = 1.0f;
			if (aiMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor) == aiReturn_SUCCESS)
			{
				material->m_RoughnessFactor = roughnessFactor;
			}

			aiColor3D emissiveColor;
			if (aiMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == aiReturn_SUCCESS)
			{
				material->m_EmissiveColor = Color(emissiveColor.r, emissiveColor.g, emissiveColor.b, 1.0f);
			}

			// AlphaMode
			aiString alphaModeStr;
			if (aiMaterial->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaModeStr) == aiReturn_SUCCESS)
			{
				std::string_view modeStr = alphaModeStr.C_Str();
				if (modeStr == "MASK")
				{
					material->m_AlphaMode = AlphaMode::Mask;
					material->m_AlphaCutoffMode = AlphaCutoffMode::AlphaCutoff;

					float alphaCutoff = 0.5f;
					aiMaterial->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff);
					material->m_AlphaCutoff = alphaCutoff;
				}
				else if (modeStr == "BLEND")
				{
					material->m_AlphaMode = AlphaMode::Blend;
				}
				else
				{
					material->m_AlphaMode = AlphaMode::Opaque;
				}
			}
			else
			{
				material->m_AlphaMode = AlphaMode::Opaque;
				material->m_AlphaCutoffMode = AlphaCutoffMode::Disabled;
			}

			// Material flags
			int32_t doubleSided = 0;
			if (aiMaterial->Get(AI_MATKEY_TWOSIDED, doubleSided) == aiReturn_SUCCESS)
			{
				if (doubleSided != 0)
				{
					material->m_Flags |= MaterialFlags::DoubleSided;
				}
			}

			material->m_Name = StringID(aiMaterial->GetName().C_Str());
		}

		// Create Model and Meshes.
		const auto aiMeshCount = aiScene->mNumMeshes;

		const ModelID modelId = CreateModel(canonicalPath);
		auto* model = GetModel(modelId);
		GGLAB_ASSERT(model);
		model->m_Id = modelId;
		model->m_Type = ModelType::GlTF;
		model->m_MeshInstance.resize(aiMeshCount);

		// Prepare meshes upload data
		std::vector<MeshUploadData> meshUploadDatas;
		meshUploadDatas.resize(aiMeshCount);

		for (uint32_t aiMeshIndex = 0; aiMeshIndex < aiMeshCount; ++aiMeshIndex)
		{
			auto& uploadData = meshUploadDatas[aiMeshIndex];
			const auto* aiMesh = aiScene->mMeshes[aiMeshIndex];

			const MeshID meshId = CreateMesh();
			uploadData.m_MeshId = meshId;

			auto* mesh = GetMesh(meshId);
			GGLAB_ASSERT(mesh);
			mesh->m_Id = meshId;
			mesh->m_Name = StringID(aiMesh->mName.C_Str());

			ModelMesh modelMesh{};
			modelMesh.m_MeshId = meshId;
			modelMesh.m_MaterialId = materialIds[aiMesh->mMaterialIndex];
			model->m_MeshInstance[aiMeshIndex] = modelMesh;

			// Vertex data
			const auto aiVertexCount = aiMesh->mNumVertices;
			auto& verticesData = uploadData.m_VerticesData;
			verticesData.resize(aiVertexCount);

			for (uint32_t aiVertexIndex = 0; aiVertexIndex < aiVertexCount; ++aiVertexIndex)
			{
				auto& vertex = verticesData[aiVertexIndex];

				const auto& aiPosition = aiMesh->mVertices[aiVertexIndex];
				vertex.m_Position = Vector3(aiPosition.x, aiPosition.y, aiPosition.z);

				if (aiMesh->HasNormals())
				{
					const auto& aiNormal = aiMesh->mNormals[aiVertexIndex];
					vertex.m_Normal = Vector3(aiNormal.x, aiNormal.y, aiNormal.z);
				}

				if (aiMesh->HasTextureCoords(0))
				{
					const auto& aiTexCoord = aiMesh->mTextureCoords[0][aiVertexIndex];
					vertex.m_TexCoord0 = Vector2(aiTexCoord.x, aiTexCoord.y);
					vertex.m_TexCoord1 = vertex.m_TexCoord0;
				}

				if (aiMesh->HasTextureCoords(1))
				{
					const auto& aiTexCoord = aiMesh->mTextureCoords[1][aiVertexIndex];
					vertex.m_TexCoord1 = Vector2(aiTexCoord.x, aiTexCoord.y);
				}

				if (aiMesh->HasTangentsAndBitangents())
				{
					Vector3 normal = vertex.m_Normal;
					if (normal.LengthSquared() <= TangentLengthSqEpsilon)
					{
						normal = Vector3::UnitY;
					}
					else
					{
						normal.Normalize();
					}

					const auto& aiTangent = aiMesh->mTangents[aiVertexIndex];
					Vector3 tangent(aiTangent.x, aiTangent.y, aiTangent.z);
					if (tangent.LengthSquared() <= TangentLengthSqEpsilon)
					{
						vertex.m_Tangent = MakeFallbackTangent(normal);
					}
					else
					{
						tangent.Normalize();

						const auto& aiBitangent = aiMesh->mBitangents[aiVertexIndex];
						Vector3 bitangent(aiBitangent.x, aiBitangent.y, aiBitangent.z);
						float handedness = 1.0f;
						if (bitangent.LengthSquared() > TangentLengthSqEpsilon)
						{
							bitangent.Normalize();
							handedness = (tangent.Cross(bitangent).Dot(normal) < 0.0f) ? -1.0f : 1.0f;
						}

						vertex.m_Tangent = Vector4(tangent.x, tangent.y, tangent.z, handedness);
					}
				}
				else
				{
					vertex.m_Tangent = MakeFallbackTangent(vertex.m_Normal);
				}
			}

			// Indices data
			const auto aiFaceCount = aiMesh->mNumFaces;
			auto& indicesData = uploadData.m_IndicesData;

			constexpr auto IndicesCountPerFace = 3;
			indicesData.reserve(static_cast<size_t>(aiFaceCount * IndicesCountPerFace));

			for (uint32_t aiFaceIndex = 0; aiFaceIndex < aiFaceCount; ++aiFaceIndex)
			{
				const auto& aiFace = aiMesh->mFaces[aiFaceIndex];
				for (uint32_t aiIndIndex = 0; aiIndIndex < aiFace.mNumIndices; ++aiIndIndex)
				{
					indicesData.push_back(static_cast<uint32_t>(aiFace.mIndices[aiIndIndex]));
				}
			}

			// Compute Bounding infos
			ComputeMeshBounds(*mesh, verticesData);
		}

		// Upload to GPU
		auto batch = m_TransferManager->BeginBatch();
		auto* transferContext = m_TransferManager->GetTransferContext();
		// Upload textures
		for (const auto& texUploadData : texUploadDatas)
		{
			m_TextureRegistry->UploadTexture(texUploadData, *transferContext);
		}
		// Upload meshes
		for (const auto& meshUploadData : meshUploadDatas)
		{
			UploadMesh(meshUploadData, *transferContext);
		}
		batch.Submit(true);

		return modelId;
	}

	MeshID AssetManager::CreateMesh() noexcept
	{
		const auto meshId = m_MeshIdCounter.Acquire();
		auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
		GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace MeshID & meshPtr pair failed.");

		return meshId;
	}

	MaterialID AssetManager::CreateMaterial() noexcept
	{
		const auto materialId = m_MaterialIdCounter.Acquire();
		auto idMatPair = m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::make_unique<Material>());
		GGLAB_ASSERT_MSG(idMatPair.second == true, "Emplace MaterialID & materialPtr pair failed.");

		return materialId;
	}

	ModelID AssetManager::CreateModel(const std::filesystem::path& canonicalPath) noexcept
	{
		const auto modelId = m_ModelIdCounter.Acquire();
		auto pathIdPair = m_ModelContainer.m_PathIDMap.emplace(canonicalPath, modelId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path & ModelID pair failed.");

		auto idModelPair = m_ModelContainer.m_ModelIDMap.emplace(modelId, std::make_unique<Model>());
		GGLAB_ASSERT_MSG(idModelPair.second == true, "Emplace ModelID & ModelPtr pair failed.");
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
		static_assert(std::is_base_of_v<DirectX::XMFLOAT3, decltype(Vertex::m_Position)>,
			"Vertex position must be XMFLOAT3 or derived type.");

		if (vertices.empty())
		{
			mesh.m_BoundingBox = DirectX::BoundingBox{};
			mesh.m_BoundingSphere = DirectX::BoundingSphere{};
			mesh.m_HasBounds = false;
			return;
		}

		const auto* firstPos = std::addressof(vertices[0].m_Position);
		const auto* firstPoint = static_cast<const DirectX::XMFLOAT3*>(firstPos);

		constexpr size_t stride = sizeof(Vertex);

		DirectX::BoundingBox::CreateFromPoints(mesh.m_BoundingBox, vertices.size(), firstPoint, stride);
		DirectX::BoundingSphere::CreateFromBoundingBox(mesh.m_BoundingSphere, mesh.m_BoundingBox);
		//DirectX::BoundingSphere::CreateFromPoints(mesh.m_BoundingSphere, vertices.size(), firstPoint, stride);

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
