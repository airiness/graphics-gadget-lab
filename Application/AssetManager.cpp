#include "Precompiled.h"
#include "AssetManager.h"
#include "DX12Device.h"
#include "DX12ResourceUploader.h"
#include "DX12Resource.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12CommandList.h"
#include "Utility.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace graphicsGadgetLab
{
	AssetManager::AssetManager(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
	}
	void AssetManager::Initialize() noexcept
	{
	}

	void AssetManager::Finalize() noexcept
	{
	}

	Model AssetManager::LoadModel(const std::filesystem::path& path) noexcept
	{
		// Check extension, load glTF file
		const auto getModelFileType = [](const std::string& extension) -> ModelType
			{
				if (extension == ".gltf") { return ModelType::ModelType_glTF; }
				return ModelType::ModelType_Invalid;
			};

		const auto extension = path.extension().string();
		const auto modelFileType = getModelFileType(extension);

		switch (modelFileType)
		{
		case ModelType::ModelType_glTF:
			return LoadModelGltf(path);
			break;
		default:
			GGLAB_UNREACHABLE("Unknown model file type.");
			break;
		}
		return Model();
	}

	TextureID AssetManager::GetTextureID(const std::filesystem::path& path) noexcept
	{
		const auto cannonicalPath = std::filesystem::canonical(path);
		auto textureId = HasTexture(cannonicalPath);
		if (textureId == InvalidTextureID)
		{
			textureId = CreateTexture(cannonicalPath);
			TextureUploadData texUploadData{ .m_TextureID = textureId };
			LoadTextureScratchImage(path, texUploadData);

			// Upload texture to GPU
			auto* resourceUploader = m_DX12Device->GetResourceUploader();
			resourceUploader->BeginUpload();
			UploadTexture(resourceUploader, texUploadData);
			resourceUploader->EndUpload(true);
		}
		
		return textureId;
	}

	Texture* AssetManager::GetTexture(TextureID textureId) noexcept
	{
		auto iter = m_TextureContainer.m_TextureIDMap.find(textureId);
		if (iter != m_TextureContainer.m_TextureIDMap.end())
		{
			return iter->second.get();
		}

		//GGLAB_ASSERT_MSG(false, "Invalid TextureID, check it!");
		return nullptr;
	}

	Mesh* AssetManager::GetMesh(MeshID meshId) noexcept
	{
		auto iter = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iter != m_MeshContainer.m_MeshIDMap.end())
		{
			return iter->second.get();
		}

		//GGLAB_ASSERT_MSG(false, "Invalid MeshID, check it!");
		return nullptr;
	}

	Material* AssetManager::GetMaterial(MaterialID materialId) noexcept
	{	
		auto iter = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iter != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return iter->second.get();
		}
		//GGLAB_ASSERT_MSG(false, "Invalid MaterialID, check it!");
		return nullptr;	
	}

	void AssetManager::AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept
	{
		auto meshId = mesh->m_MeshID;
		if (meshId == InvalidMeshID)
		{
			meshId = m_NextMeshID++;
			mesh->m_MeshID = meshId;	
		}
		
		const auto iter = m_MeshContainer.m_MeshIDMap.find(meshUploadData.m_MeshID);
		if (iter == m_MeshContainer.m_MeshIDMap.end())
		{
			m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		}
		else
		{
			return;
		}
		
		meshUploadData.m_MeshID = meshId;

		// Upload Mesh to GPU
		auto* resourceUploader = m_DX12Device->GetResourceUploader();
		resourceUploader->BeginUpload();
		UploadMesh(resourceUploader, meshUploadData);
		resourceUploader->EndUpload(true);
	}

	void AssetManager::AddMaterial(std::unique_ptr<Material>&& material) noexcept
	{
		auto materialId = material->m_MaterialID;
		if (materialId == InvalidMaterialID)
		{
			materialId = m_NextMaterialID++;
			material->m_MaterialID = materialId;
		}

		const auto iter = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iter == m_MaterialContainer.m_MaterialIDMap.end())
		{
			m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::move(material));
		}
	}

	void AssetManager::UploadTexture(DX12ResourceUploader* resourceUploader, const TextureUploadData& uploadData) noexcept
	{
		auto* texture = GetTexture(uploadData.m_TextureID);

		const auto& texMedaData = uploadData.m_ScratchImage.GetMetadata();
		texture->m_Texture = std::make_unique<DX12Texture>(m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Tex2D(texMedaData.format,
				static_cast<UINT64>(texMedaData.width),
				static_cast<UINT>(texMedaData.height),
				static_cast<UINT16>(texMedaData.arraySize),
				static_cast<UINT16>(texMedaData.mipLevels)),
			D3D12_RESOURCE_STATE_COMMON);

		const auto imageCount = uploadData.m_ScratchImage.GetImageCount();
		std::vector<D3D12_SUBRESOURCE_DATA> subResourceDatas(imageCount);

		const auto& images = uploadData.m_ScratchImage.GetImages();
		for (int32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
		{
			subResourceDatas[imageIndex].pData = images[imageIndex].pixels;
			subResourceDatas[imageIndex].RowPitch = images[imageIndex].rowPitch;
			subResourceDatas[imageIndex].SlicePitch = images[imageIndex].slicePitch;
		}

		auto subResourceCount = static_cast<UINT>(subResourceDatas.size());
		auto uploadSize = GetRequiredIntermediateSize(texture->m_Texture.get()->Get(), 0, subResourceCount);

		std::unique_ptr uploadBuffer = std::make_unique<DX12Buffer>(m_DX12Device,
			D3D12_HEAP_TYPE_UPLOAD,
			CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
			D3D12_RESOURCE_STATE_GENERIC_READ);
		uploadBuffer->SetDebugName(L"UploadIntermediateBuffer");

		resourceUploader->UploadResource(subResourceDatas, texture->m_Texture.get());

		// Convert Texture State to Pixel Shader Resource. TODO: have better way to management resource state?
		auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
			texture->m_Texture.get()->Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		resourceUploader->GetUploadCommandList()->Get()->ResourceBarrier(1, &transition);

		// Allocate Descriptor& create srv
		auto* srvHeap = m_DX12Device->GetCbvSrvUavDescriptorHeap();
		texture->m_Descriptor = srvHeap->CreateDescriptor();

		const auto& textureDesc = texture->m_Texture->Get()->GetDesc();
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
		m_DX12Device->Get()->CreateShaderResourceView(texture->m_Texture->Get(),
			&srvDesc,
			texture->m_Descriptor.m_CpuHandle);

		texture->m_IsUploaded = true;
	}

	void AssetManager::UploadMesh(DX12ResourceUploader* resourceUploader, const MeshUploadData& uploadData) noexcept
	{
		auto* mesh = GetMesh(uploadData.m_MeshID);
		if (mesh == nullptr)
		{
			GGLAB_ASSERT_MSG(false, "Invalid MeshID, check it!");
			return;
		}

		const auto& verticesData = uploadData.m_VerticesData;
		const auto& indicesData = uploadData.m_IndicesData;

		const auto vertexCount = verticesData.size();
		const auto indexCount = indicesData.size();

		mesh->m_VertexCount = vertexCount;
		mesh->m_IndexCount = indexCount;

		const auto vertexBufferSize = vertexCount * sizeof(Vertex);
		const auto indexBufferSize = indexCount * sizeof(uint32_t);

		mesh->m_VertexBuffer = std::make_unique<DX12Buffer>(m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_COMMON);

		mesh->m_IndexBuffer = std::make_unique<DX12Buffer>(m_DX12Device,
			D3D12_HEAP_TYPE_DEFAULT,
			CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
			D3D12_RESOURCE_STATE_COMMON);

		resourceUploader->UploadResource(verticesData.data(), vertexBufferSize, mesh->m_VertexBuffer.get());
		resourceUploader->UploadResource(indicesData.data(), indexBufferSize, mesh->m_IndexBuffer.get());

		mesh->m_VertexBufferView.BufferLocation = mesh->m_VertexBuffer->Get()->GetGPUVirtualAddress();
		mesh->m_VertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
		mesh->m_VertexBufferView.StrideInBytes = sizeof(Vertex);

		mesh->m_IndexBufferView.BufferLocation = mesh->m_IndexBuffer->Get()->GetGPUVirtualAddress();
		mesh->m_IndexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
		mesh->m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

		mesh->m_IsUploaded = true;
	}

	Model AssetManager::LoadModelGltf(const std::filesystem::path& path) noexcept
	{
		const auto cannonicalPath = std::filesystem::canonical(path);
		const auto directory = cannonicalPath.parent_path();

		// Load model file
		Assimp::Importer importer;
		const uint32_t importFlags =
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
		std::vector<MaterialID> materialIds(aiMaterialCount, InvalidMaterialID);
		std::vector<TextureUploadData> texUploadDatas;
		for (uint32_t materialIndex = 0; materialIndex < aiMaterialCount; ++materialIndex)
		{
			const auto materialId = CreateMaterial();
			materialIds[materialIndex] = materialId;
			auto* material = GetMaterial(materialId);

			const auto& aiMaterial = aiScene->mMaterials[materialIndex];

			aiString aiTexPath;
			// Base Color Texture
			if (aiMaterial->GetTexture(aiTextureType_BASE_COLOR, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = cannonicalPath.parent_path() / aiTexPath.C_Str();
				auto baseColorTexId = HasTexture(texPath);
				if (baseColorTexId == InvalidTextureID)
				{
					baseColorTexId = CreateTexture(texPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureID = baseColorTexId;
					LoadTextureScratchImage(texPath, texUploadData);	
				}
				material->m_TexBaseColor = baseColorTexId;
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
				const auto texPath = cannonicalPath.parent_path() / aiTexPath.C_Str();
				auto metalicRoughnessTexId = HasTexture(texPath);
				if (metalicRoughnessTexId == InvalidTextureID)
				{
					metalicRoughnessTexId = CreateTexture(texPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureID = metalicRoughnessTexId;
					LoadTextureScratchImage(texPath, texUploadData);
				}
				material->m_TexMetallicRoughness = metalicRoughnessTexId;
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
				const auto texPath = cannonicalPath.parent_path() / aiTexPath.C_Str();
				auto normalTexId = HasTexture(texPath);
				if (normalTexId == InvalidTextureID)
				{
					normalTexId = CreateTexture(texPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureID = normalTexId;
					LoadTextureScratchImage(texPath, texUploadData);
				}
				material->m_NormalTex = normalTexId;
			}

			// Occlusion Texture
			if (aiMaterial->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = cannonicalPath.parent_path() / aiTexPath.C_Str();
				auto occlusionTexId = HasTexture(texPath);
				if (occlusionTexId == InvalidTextureID)
				{
					occlusionTexId = CreateTexture(texPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureID = occlusionTexId;
					LoadTextureScratchImage(texPath, texUploadData);
				}
				material->m_OcclusionTex = occlusionTexId;
			}

			// Emissive Texture
			if (aiMaterial->GetTexture(aiTextureType_EMISSIVE, 0, &aiTexPath) == aiReturn_SUCCESS)
			{
				const auto texPath = cannonicalPath.parent_path() / aiTexPath.C_Str();
				auto emissiveTexId = HasTexture(texPath);
				if (emissiveTexId == InvalidTextureID)
				{
					emissiveTexId = CreateTexture(texPath);
					auto& texUploadData = texUploadDatas.emplace_back();
					texUploadData.m_TextureID = emissiveTexId;
					LoadTextureScratchImage(texPath, texUploadData);
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

			material->m_MaterialName = aiMaterial->GetName().C_Str();
		}
	
		const auto aiMeshCount = aiScene->mNumMeshes;
		// Add path & meshIds pair to MeshContainer
		auto pathIdPair = m_MeshContainer.m_PathIDMap.emplace(cannonicalPath, std::vector<MeshID>());
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path&meshIds pair failed.");
		auto& meshIds = pathIdPair.first->second;
		meshIds.resize(aiMeshCount, InvalidMeshID);

		// Prepare meshes upload data
		std::vector<MeshUploadData> meshUploadDatas;
		meshUploadDatas.resize(aiMeshCount);

		for (uint32_t aiMeshIndex = 0; aiMeshIndex < aiMeshCount; ++aiMeshIndex)
		{
			auto& uploadData = meshUploadDatas[aiMeshIndex];
			const auto* aiMesh = aiScene->mMeshes[aiMeshIndex];

			// Create mesh ID and emplace to meshIds
			{
				const auto meshId = CreateMesh();
				meshIds[aiMeshIndex] = meshId;
				uploadData.m_MeshID = meshId;

				// Link Material Index to mesh
				auto mesh = GetMesh(meshId);
				mesh->m_Material = materialIds[aiMesh->mMaterialIndex];
				mesh->m_MeshName = aiMesh->mName.C_Str();
			}

			// Vertices data
			{
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

					// TODO: targent vertex data?
				}
			}

			// Indices data
			{
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
		}


		// Upload to GPU
		auto* resourceUploader = m_DX12Device->GetResourceUploader();
		resourceUploader->BeginUpload();
		// Upload textures
		for (const auto& texUploadData : texUploadDatas)
		{
			UploadTexture(resourceUploader, texUploadData);
		}
		// Upload meshes
		for (const auto& meshUploadData : meshUploadDatas)
		{
			UploadMesh(resourceUploader, meshUploadData);
		}
		resourceUploader->EndUpload(true);

		Model model;
		model.m_Type = ModelType::ModelType_glTF;
		model.m_ModelName = cannonicalPath.stem().string();
		model.m_Meshes = meshIds;
		return model;
	}

	TextureID AssetManager::CreateTexture(const std::filesystem::path& cannonicalPath) noexcept
	{
		// Create new texture ID and emplace to TextureContainer.
		const auto textureId = m_NextTextureID++;
		auto pathIdPair = m_TextureContainer.m_PathIDMap.emplace(cannonicalPath, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path&textureID pair failed.");

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second == true, "Emplace textureId&TexturePtr pair failed.");

		return textureId;
	}

	MeshID AssetManager::CreateMesh() noexcept
	{
		const auto meshId = m_NextMeshID++;
		auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
		GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace meshId&meshPtr pair failed.");

		return meshId;
	}

	MaterialID AssetManager::CreateMaterial() noexcept
	{
		const auto materialId = m_NextMaterialID++;
		auto idMatPair = m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::make_unique<Material>());
		GGLAB_ASSERT_MSG(idMatPair.second == true, "Emplace materialId&materialPtr pair failed.");

		return materialId;
	}

	TextureID AssetManager::HasTexture(const std::filesystem::path& path) const noexcept
	{		
		auto& texNameIdsMap = m_TextureContainer.m_PathIDMap;
		auto searchIter = texNameIdsMap.find(path);
		if (searchIter != texNameIdsMap.end())
		{
			return searchIter->second;
		}
		return InvalidTextureID;
	}

	AssetManager::TextureUploadData& AssetManager::LoadTextureScratchImage(
		const std::filesystem::path& texPath, 
		TextureUploadData& uploadData) noexcept
	{
		GGLAB_ASSERT_MSG(uploadData.m_TextureID != InvalidTextureID, "Invalid TextureUploadData");
		GGLAB_ASSERT_MSG(std::filesystem::exists(texPath), "Invalid texture file path.");

		const auto extension = texPath.extension().string();

		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;

		if (extension == ".dds")
		{
			utility::ThrowIfFailed(LoadFromDDSFile(texPath.c_str(),
				DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}
		else
		{
			utility::ThrowIfFailed(LoadFromWICFile(texPath.c_str(),
				DirectX::WIC_FLAGS::WIC_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}

		uploadData.m_ScratchImage = std::move(scratchImage);
		return uploadData;
	}
}