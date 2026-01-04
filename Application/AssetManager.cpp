#include "Precompiled.h"
#include "AssetManager.h"
#include "TransferManager.h"
#include "DX12Device.h"
#include "DX12Resource.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12CommandList.h"
#include "DX12DescriptorFreeListAllocator.h"
#include "HResult.h"
#include "PathUtils.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace gglab
{
	AssetManager::AssetManager(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_DescriptorManager(createInfo.m_DescriptorManager)
	{
		GGLAB_ASSERT_MSG(m_DX12Device != nullptr, "DX12Device is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_DescriptorManager != nullptr, "DX12DescriptorManager is null!");
	}

	AssetManager::~AssetManager()
	{
		// Release Descriptor for texture
		for (auto& texture : m_TextureContainer.m_TextureIDMap)
		{
			texture.second->m_Descriptor.Free();
		}
	}

	ModelId AssetManager::LoadModel(const std::filesystem::path& path) noexcept
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
		const auto modelFileType = getModelFileType(extension);

		switch (modelFileType)
		{
		case ModelType::GlTF:
			return LoadModelGltf(canonicalPath);
		default:
			GGLAB_UNREACHABLE("Unknown model file type.");
		}
	}

	TextureId AssetManager::LoadTexture(const std::filesystem::path& path) noexcept
	{
		const auto canonicalPath = utils::Canonical(path);

		auto textureId = FindTexture(canonicalPath);
		if (!textureId.IsValid())
		{
			textureId = CreateTexture(canonicalPath);

			TextureUploadData texUploadData{};
			texUploadData.m_TextureId = textureId;
			LoadTextureScratchImage(canonicalPath, texUploadData);

			// Upload texture to GPU
			auto batch = m_TransferManager->BeginBatch();
			auto* copyContext = m_TransferManager->GetCopyContext();
			UploadTexture(texUploadData, *copyContext);
			batch.Submit(true);
		}

		return textureId;
	}

	Texture* AssetManager::GetTexture(TextureId textureId) noexcept
	{
		return const_cast<Texture*>(std::as_const(*this).GetTexture(textureId));
	}

	const Texture* AssetManager::GetTexture(TextureId textureId) const noexcept
	{
		auto iter = m_TextureContainer.m_TextureIDMap.find(textureId);
		if (iter != m_TextureContainer.m_TextureIDMap.end())
		{
			return iter->second.get();
		}

		//GGLAB_ASSERT_MSG(false, "Invalid TextureID, check it!");
		return nullptr;
	}

	Mesh* AssetManager::GetMesh(MeshId meshId) noexcept
	{
		return const_cast<Mesh*>(std::as_const(*this).GetMesh(meshId));
	}

	const Mesh* AssetManager::GetMesh(MeshId meshId) const noexcept
	{
		auto iter = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iter != m_MeshContainer.m_MeshIDMap.end())
		{
			return iter->second.get();
		}

		//GGLAB_ASSERT_MSG(false, "Invalid MeshID, check it!");
		return nullptr;
	}

	Material* AssetManager::GetMaterial(MaterialId materialId) noexcept
	{
		return const_cast<Material*>(std::as_const(*this).GetMaterial(materialId));
	}

	const Material* AssetManager::GetMaterial(MaterialId materialId) const noexcept
	{
		auto iter = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iter != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return iter->second.get();
		}
		//GGLAB_ASSERT_MSG(false, "Invalid MaterialID, check it!");
		return nullptr;
	}

	Model* AssetManager::GetModel(ModelId modelId) noexcept
	{
		return const_cast<Model*>(std::as_const(*this).GetModel(modelId));
	}

	const Model* AssetManager::GetModel(ModelId modelId) const noexcept
	{
		auto iter = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iter != m_ModelContainer.m_ModelIDMap.end())
		{
			return iter->second.get();
		}

		//GGLAB_ASSERT_MSG(false, "Invalid ModelID, check it!");
		return nullptr;
	}

	uint32_t AssetManager::GetTextureDescriptorIndex(TextureId textureId) const noexcept
	{
		const auto* texture = GetTexture(textureId);
		if (texture == nullptr || !texture->m_IsUploaded)
		{
			return 0u;
		}
		return texture->m_DescriptorIndex;
	}

	MeshId AssetManager::AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept
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
		const auto iter = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iter != m_MeshContainer.m_MeshIDMap.end())
		{
			return meshId;
		}

		m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		meshUploadData.m_MeshId = meshId;

		// Upload Mesh to GPU
		auto batch = m_TransferManager->BeginBatch();
		auto* copyContext = m_TransferManager->GetCopyContext();
		UploadMesh(meshUploadData, *copyContext);
		batch.Submit(true);

		return meshId;
	}

	MaterialId AssetManager::AddMaterial(std::unique_ptr<Material>&& material) noexcept
	{
		GGLAB_ASSERT(material);

		auto materialId = material->m_Id;
		if (!materialId.IsValid())
		{
			materialId = m_MaterialIdCounter.Acquire();
			material->m_Id = materialId;
		}

		const auto iter = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iter != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return materialId;
		}

		m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::move(material));

		return materialId;
	}

	ModelId AssetManager::AddModel(std::unique_ptr<Model>&& model) noexcept
	{
		GGLAB_ASSERT(model);

		auto modelId = model->m_Id;
		if (!modelId.IsValid())
		{
			modelId = m_ModelIdCounter.Acquire();
			model->m_Id = modelId;
		}

		const auto iter = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iter != m_ModelContainer.m_ModelIDMap.end())
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

	void AssetManager::UploadTexture(const TextureUploadData& uploadData, CopyContext& copyContext) noexcept
	{
		auto* texture = GetTexture(uploadData.m_TextureId);
		GGLAB_ASSERT_MSG(texture != nullptr, "UploadTexture: Invalid TextureID, check it!");

		const auto& texMedaData = uploadData.m_ScratchImage.GetMetadata();

		texture->m_Texture = std::make_unique<DX12Texture>();
		texture->m_Texture->Create(DX12Texture::AssetTextureCreateInfo(m_DX12Device->GetMemAllocator(),
			CD3DX12_RESOURCE_DESC::Tex2D(texMedaData.format,
				static_cast<UINT64>(texMedaData.width),
				static_cast<UINT>(texMedaData.height),
				static_cast<UINT16>(texMedaData.arraySize),
				static_cast<UINT16>(texMedaData.mipLevels))));

		const auto imageCount = uploadData.m_ScratchImage.GetImageCount();
		std::vector<D3D12_SUBRESOURCE_DATA> subResourceDatas(imageCount);

		const auto& images = uploadData.m_ScratchImage.GetImages();
		for (int32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
		{
			subResourceDatas[imageIndex].pData = images[imageIndex].pixels;
			subResourceDatas[imageIndex].RowPitch = images[imageIndex].rowPitch;
			subResourceDatas[imageIndex].SlicePitch = images[imageIndex].slicePitch;
		}

		copyContext.UploadResource(subResourceDatas, texture->m_Texture.get());

		// Convert Texture State to Pixel Shader Resource. TODO: have better way to management resource state?
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
			texture->m_Texture.get()->Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		copyContext.GetCommandList()->Get()->ResourceBarrier(1, &transition);

		// Allocate Descriptor& create srv
		auto& srvDescriptorAllocator = m_DescriptorManager->GetCbvSrvUavDescriptorAllocator();
		texture->m_Descriptor = std::move(srvDescriptorAllocator.Allocate());

		const auto& textureDesc = texture->m_Texture->Get()->GetDesc();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
		m_DX12Device->Get()->CreateShaderResourceView(
			texture->m_Texture->Get(),
			&srvDesc,
			texture->m_Descriptor.CpuHandle());

		texture->m_DescriptorIndex = 0;	// TODO: get global descriptor index

		texture->m_IsUploaded = true;
	}

	void AssetManager::UploadMesh(const MeshUploadData& uploadData, CopyContext& copyContext) noexcept
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

		const auto vertexBufferSize = vertexCount * sizeof(Vertex);
		const auto indexBufferSize = indexCount * sizeof(uint32_t);

		mesh->m_VertexBuffer = std::make_unique<DX12Buffer>();
		mesh->m_VertexBuffer->Create(
			DX12Buffer::VertexOrIndexBufferCreateInfo(
				m_DX12Device->GetMemAllocator(), vertexBufferSize));

		mesh->m_IndexBuffer = std::make_unique<DX12Buffer>();
		mesh->m_IndexBuffer->Create(
			DX12Buffer::VertexOrIndexBufferCreateInfo(
				m_DX12Device->GetMemAllocator(), indexBufferSize));

		copyContext.UploadResource(verticesData.data(), vertexBufferSize, mesh->m_VertexBuffer.get());
		copyContext.UploadResource(indicesData.data(), indexBufferSize, mesh->m_IndexBuffer.get());

		mesh->m_VertexBufferView.BufferLocation = mesh->m_VertexBuffer->Get()->GetGPUVirtualAddress();
		mesh->m_VertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
		mesh->m_VertexBufferView.StrideInBytes = sizeof(Vertex);

		mesh->m_IndexBufferView.BufferLocation = mesh->m_IndexBuffer->Get()->GetGPUVirtualAddress();
		mesh->m_IndexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
		mesh->m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

		mesh->m_IsUploaded = true;
	}

	ModelId AssetManager::LoadModelGltf(const std::filesystem::path& path) noexcept
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
		std::vector<MaterialId> materialIds(aiMaterialCount);
		std::vector<TextureUploadData> texUploadDatas;
		for (uint32_t materialIndex = 0; materialIndex < aiMaterialCount; ++materialIndex)
		{
			const auto materialId = CreateMaterial();
			materialIds[materialIndex] = materialId;
			auto* material = GetMaterial(materialId);
			material->m_Id = materialId;

			const auto& aiMaterial = aiScene->mMaterials[materialIndex];

			aiString aiTexPath;
			// Base Color Texture
			if (aiMaterial->GetTexture(aiTextureType_BASE_COLOR, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = canonicalPath.parent_path() / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);
				auto baseColorTexId = FindTexture(canonicalTexPath);
				if (!baseColorTexId.IsValid())
				{
					baseColorTexId = CreateTexture(canonicalTexPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureId = baseColorTexId;
					LoadTextureScratchImage(canonicalTexPath, texUploadData);
				}
				material->m_BaseColorTex = baseColorTexId;
			}
			else
			{
				aiColor4D baseColor;
				if (aiMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) == aiReturn_SUCCESS)
				{
					material->m_BaseColor = Color(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
				}
			}

			// Metallic Roughness Texture
			if (aiMaterial->GetTexture(aiTextureType_METALNESS, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = canonicalPath.parent_path() / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);
				auto metalicRoughnessTexId = FindTexture(canonicalTexPath);
				if (!metalicRoughnessTexId.IsValid())
				{
					metalicRoughnessTexId = CreateTexture(canonicalTexPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureId = metalicRoughnessTexId;
					LoadTextureScratchImage(canonicalTexPath, texUploadData);
				}
				material->m_MetallicRoughnessTex = metalicRoughnessTexId;
			}
			else
			{
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
			}

			// Normal Texture
			if (aiMaterial->GetTexture(aiTextureType_NORMALS, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = canonicalPath.parent_path() / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);
				auto normalTexId = FindTexture(canonicalTexPath);
				if (!normalTexId.IsValid())
				{
					normalTexId = CreateTexture(canonicalTexPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureId = normalTexId;
					LoadTextureScratchImage(canonicalTexPath, texUploadData);
				}
				material->m_NormalTex = normalTexId;
			}

			// Occlusion Texture
			if (aiMaterial->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = canonicalPath.parent_path() / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);
				auto occlusionTexId = FindTexture(canonicalTexPath);
				if (!occlusionTexId.IsValid())
				{
					occlusionTexId = CreateTexture(canonicalTexPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureId = occlusionTexId;
					LoadTextureScratchImage(canonicalTexPath, texUploadData);
				}
				material->m_OcclusionTex = occlusionTexId;
			}

			// Emissive Texture
			if (aiMaterial->GetTexture(aiTextureType_EMISSIVE, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = canonicalPath.parent_path() / aiTexPath.C_Str();
				const auto canonicalTexPath = utils::Canonical(texPath);
				auto emissiveTexId = FindTexture(canonicalTexPath);
				if (!emissiveTexId.IsValid())
				{
					emissiveTexId = CreateTexture(canonicalTexPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureId = emissiveTexId;
					LoadTextureScratchImage(canonicalTexPath, texUploadData);
				}
				material->m_EmissiveTex = emissiveTexId;
			}
			else
			{
				aiColor3D emissiveColor;
				if (aiMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == aiReturn_SUCCESS)
				{
					material->m_EmissiveColor = Color(emissiveColor.r, emissiveColor.g, emissiveColor.b, 1.0f);
				}
			}

			material->m_Name = StringId(aiMaterial->GetName().C_Str());
		}

		// Create Model and Meshes.
		const auto aiMeshCount = aiScene->mNumMeshes;

		const ModelId modelId = CreateModel(canonicalPath);
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

			const MeshId meshId = CreateMesh();
			uploadData.m_MeshId = meshId;

			auto* mesh = GetMesh(meshId);
			GGLAB_ASSERT(mesh);
			mesh->m_Id = meshId;
			mesh->m_Name = StringId(aiMesh->mName.C_Str());

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

				if (aiMesh->HasTextureCoords(0)) // TODO: support more texture coordinates
				{
					const auto& aiTexCoord = aiMesh->mTextureCoords[0][aiVertexIndex];
					vertex.m_TexCoord = Vector2(aiTexCoord.x, aiTexCoord.y);
				}

				//// TODO: targent vertex data?
				//if (aiMesh->HasTangentsAndBitangents())
				//{
				//	// Tangent
				//	const auto& aiTangent = aiMesh->mTangents[aiVertexIndex];
				//	vertex.m_Tangent = Vector3(aiTangent.x, aiTangent.y, aiTangent.z);
				//	// Bitangent
				//	const auto& aiBitangent = aiMesh->mBitangents[aiVertexIndex];
				//	vertex.m_Bitangent = Vector3(aiBitangent.x, aiBitangent.y, aiBitangent.z);
				//}			
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
		}

		// Upload to GPU
		auto batch = m_TransferManager->BeginBatch();
		auto* copyContext = m_TransferManager->GetCopyContext();
		// Upload textures
		for (const auto& texUploadData : texUploadDatas)
		{
			UploadTexture(texUploadData, *copyContext);
		}
		// Upload meshes
		for (const auto& meshUploadData : meshUploadDatas)
		{
			UploadMesh(meshUploadData, *copyContext);
		}
		batch.Submit(true);

		return modelId;
	}

	TextureId AssetManager::CreateTexture(const std::filesystem::path& canonicalPath) noexcept
	{
		// Create new texture ID and emplace to TextureContainer.
		const auto textureId = m_TextureIdCounter.Acquire();
		auto pathIdPair = m_TextureContainer.m_PathIDMap.emplace(canonicalPath, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path & TextureID pair failed.");

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second == true, "Emplace textureId & TexturePtr pair failed.");

		return textureId;
	}

	MeshId AssetManager::CreateMesh() noexcept
	{
		const auto meshId = m_MeshIdCounter.Acquire();
		auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
		GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace MeshId & meshPtr pair failed.");

		return meshId;
	}

	MaterialId AssetManager::CreateMaterial() noexcept
	{
		const auto materialId = m_MaterialIdCounter.Acquire();
		auto idMatPair = m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::make_unique<Material>());
		GGLAB_ASSERT_MSG(idMatPair.second == true, "Emplace MaterialId & materialPtr pair failed.");

		return materialId;
	}

	ModelId AssetManager::CreateModel(const std::filesystem::path& canonicalPath) noexcept
	{
		const auto modelId = m_ModelIdCounter.Acquire();
		auto pathIdPair = m_ModelContainer.m_PathIDMap.emplace(canonicalPath, modelId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path & ModelId pair failed.");

		auto idModelPair = m_ModelContainer.m_ModelIDMap.emplace(modelId, std::make_unique<Model>());
		GGLAB_ASSERT_MSG(idModelPair.second == true, "Emplace ModelId & ModelPtr pair failed.");
		return modelId;
	}

	TextureId AssetManager::FindTexture(const std::filesystem::path& canonicalPath) const noexcept
	{
		auto& texNameIdsMap = m_TextureContainer.m_PathIDMap;
		auto searchIter = texNameIdsMap.find(canonicalPath);
		if (searchIter != texNameIdsMap.end())
		{
			return searchIter->second;
		}
		return InvalidTextureId;
	}

	ModelId AssetManager::FindModel(const std::filesystem::path& canonicalPath) const noexcept
	{
		auto& modelPathMap = m_ModelContainer.m_PathIDMap;
		auto searchIter = modelPathMap.find(canonicalPath);
		if (searchIter != modelPathMap.end())
		{
			return searchIter->second;
		}
		return InvalidModelId;
	}

	AssetManager::TextureUploadData& AssetManager::LoadTextureScratchImage(
		const std::filesystem::path& texPath,
		TextureUploadData& uploadData) noexcept
	{
		GGLAB_ASSERT_MSG(uploadData.m_TextureId != InvalidTextureId, "Invalid TextureUploadData");
		GGLAB_ASSERT_MSG(std::filesystem::exists(texPath), "Invalid texture file path.");

		const auto extension = texPath.extension().string();

		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;

		if (extension == ".dds")
		{
			GGLAB_HR(LoadFromDDSFile(texPath.c_str(),
				DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}
		else
		{
			GGLAB_HR(LoadFromWICFile(texPath.c_str(),
				DirectX::WIC_FLAGS::WIC_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}

		uploadData.m_ScratchImage = std::move(scratchImage);
		return uploadData;
	}
}