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
			DirectX::ScratchImage m_ScratchImage;
		};

		struct MeshUploadData
		{
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
		};

	private:
		struct TextureContainer
		{
			std::unordered_map<std::filesystem::path, TextureID> m_PathIDMap;
			std::unordered_map<TextureID, std::unique_ptr<Texture>> m_TextureIDMap;
		};

		struct MeshContainer
		{
			std::unordered_map<std::filesystem::path, std::vector<MeshID>> m_PathIDMap;
			std::unordered_map<MeshID, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

	public:
		explicit AssetManager(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager() noexcept = default;

		void Initialize() noexcept;
		void Finalize() noexcept;

		TextureID LoadTexture(const std::filesystem::path& path) noexcept;
		std::vector<MeshID> LoadMeshes(const std::filesystem::path& path) noexcept;

		Texture* GetTexture(TextureID textureId) noexcept;
		Mesh* GetMesh(MeshID meshId) noexcept;

		// Upload meshes management by caller self.
		void UploadMeshes(const std::vector<std::tuple<Mesh*, const MeshUploadData&>>& meshes) noexcept;

	private:
		void UploadTexture(DX12ResourceUploader* resourceUploader,
			Texture* texture, const TextureUploadData& uploadData) noexcept;

		void UploadMesh(DX12ResourceUploader* resourceUploader,
			Mesh* mesh, const MeshUploadData& uploadData) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		TextureID m_NextTextureID = ReservedTextureID + 1;
		MeshID m_NextMeshID = InvalidMeshID + 1;

		TextureContainer m_TextureContainer;
		MeshContainer m_MeshContainer;


		ComPtr<ID3D12Resource> m_TestTextureResource;
		ComPtr<ID3D12Resource> m_TestUploadInterResource;
	};
}