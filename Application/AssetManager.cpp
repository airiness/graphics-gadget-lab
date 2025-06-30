#include "Precompiled.h"
#include "AssetManager.h"
#include "DX12Device.h"
#include "DX12ResourceUploader.h"
#include "DX12Resource.h"
#include "DX12Texture.h"
#include "DX12Buffer.h"
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

	TextureID AssetManager::LoadTexture(const std::filesystem::path& path) noexcept
	{
		// Use canonical path as unordered map key.
		const auto cannonicalPath = std::filesystem::canonical(path);

		auto& texNameIdsMap = m_TextureContainer.m_PathIDMap;
		auto searchIter = texNameIdsMap.find(cannonicalPath);
		if (searchIter != texNameIdsMap.end())
		{
			return searchIter->second;
		}

		const auto extension = path.extension().string();

		DirectX::TexMetadata metaData;
		DirectX::ScratchImage scratchImage;

		if (extension == ".dds")
		{
			utility::ThrowIfFailed(LoadFromDDSFile(path.c_str(),
				DirectX::DDS_FLAGS::DDS_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}
		else
		{
			utility::ThrowIfFailed(LoadFromWICFile(path.c_str(),
				DirectX::WIC_FLAGS::WIC_FLAGS_FORCE_RGB,
				&metaData,
				scratchImage));
		}

		const auto textureId = m_NextTextureID++;
		auto pathIdPair = m_TextureContainer.m_PathIDMap.emplace(cannonicalPath, textureId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path&textureID pair failed.");

		auto idTexPair = m_TextureContainer.m_TextureIDMap.emplace(textureId, std::make_unique<Texture>());
		GGLAB_ASSERT_MSG(idTexPair.second == true, "Emplace textureId&TexturePtr pair failed.");

		auto& texture = idTexPair.first->second;
		TextureUploadData uploadData{ .m_ScratchImage = std::move(scratchImage) };

		// Upload to GPU
		auto* resourceUploader = m_DX12Device->GetResourceUploader();
		resourceUploader->BeginUpload();
		UploadTexture(resourceUploader, texture.get(), uploadData);
		resourceUploader->EndUpload(true);

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

		return textureId;
	}

	std::vector<MeshID> AssetManager::LoadMeshes(const std::filesystem::path& path) noexcept
	{
		const auto cannonicalPath = std::filesystem::canonical(path);

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

		const auto aiMeshCount = aiScene->mNumMeshes;
		std::vector<MeshUploadData> uploadDatas;
		uploadDatas.resize(aiMeshCount);

		for (uint32_t aiMeshIndex = 0; aiMeshIndex < aiMeshCount; ++aiMeshIndex)
		{
			auto& uploadData = uploadDatas[aiMeshIndex];

			// Vertices data
			const auto* aiMesh = aiScene->mMeshes[aiMeshIndex];
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
					vertex.m_TexCoord = Vector2(aiTexCoord.x, aiTexCoord.y);
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
		}

		auto pathIdPair = m_MeshContainer.m_PathIDMap.emplace(cannonicalPath, std::vector<MeshID>());
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path&meshIds pair failed.");
		auto& meshIds = pathIdPair.first->second;
		meshIds.reserve(aiMeshCount);

		// Upload to GPU
		auto* resourceUploader = m_DX12Device->GetResourceUploader();
		resourceUploader->BeginUpload();
		for (size_t meshIdIndex = 0; meshIdIndex < aiMeshCount; ++meshIdIndex)
		{
			const auto meshId = m_NextMeshID++;
			auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
			GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace meshId&meshPtr pair failed.");

			auto& mesh = idMeshPair.first->second;
			UploadMesh(resourceUploader, mesh.get(), uploadDatas[meshIdIndex]);
		}
		resourceUploader->EndUpload(true);

		return meshIds;
	}

	Texture* AssetManager::GetTexture(TextureID textureId) noexcept
	{
		auto iter = m_TextureContainer.m_TextureIDMap.find(textureId);
		if (iter != m_TextureContainer.m_TextureIDMap.end())
		{
			return iter->second.get();
		}

		GGLAB_ASSERT_MSG(false, "Invalid TextureID, check it!");
		return nullptr;
	}

	Mesh* AssetManager::GetMesh(MeshID meshId) noexcept
	{
		auto iter = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iter != m_MeshContainer.m_MeshIDMap.end())
		{
			return iter->second.get();
		}

		GGLAB_ASSERT_MSG(false, "Invalid MeshID, check it!");
		return nullptr;
	}

	void AssetManager::UploadMeshes(const std::vector<std::tuple<Mesh*, const MeshUploadData&>>& meshes) noexcept
	{
		auto* resourceUploader = m_DX12Device->GetResourceUploader();
		resourceUploader->BeginUpload();
		{
			for (auto& [mesh, uploadData] : meshes)
			{
				UploadMesh(resourceUploader, mesh, uploadData);
			}
		}
		resourceUploader->EndUpload(true);
	}

	void AssetManager::UploadTexture(DX12ResourceUploader* resourceUploader,
		Texture* texture, const TextureUploadData& uploadData) noexcept
	{
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

		resourceUploader->UploadResource(subResourceDatas, texture->m_Texture.get());
	}

	void AssetManager::UploadMesh(DX12ResourceUploader* resourceUploader,
		Mesh* mesh, const MeshUploadData& uploadData) noexcept
	{
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
	}
}