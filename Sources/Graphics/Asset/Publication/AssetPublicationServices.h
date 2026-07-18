#pragma once
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/Asset/Publication/AssetResourcePublication.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Asset/Loading/ModelImporter.h"

#include <compare>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	enum class ModelPublicationClaimOrigin : uint8_t
	{
		Reused,
		Created,
	};

	struct ModelPublicationRetainToken
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		friend constexpr auto operator<=>(
			const ModelPublicationRetainToken&,
			const ModelPublicationRetainToken&) = default;
	};

	struct ModelPublicationOwnerToken
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		friend constexpr auto operator<=>(
			const ModelPublicationOwnerToken&,
			const ModelPublicationOwnerToken&) = default;
	};

	struct ModelPublicationLeaseToken
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		friend constexpr auto operator<=>(
			const ModelPublicationLeaseToken&,
			const ModelPublicationLeaseToken&) = default;
	};

	struct ModelPublicationClaim
	{
		AssetContentVersion m_ContentVersion{};
		ModelPublicationClaimOrigin m_Origin = ModelPublicationClaimOrigin::Reused;
		ModelPublicationRetainToken m_Retain{};
	};

	struct ModelPublicationTextureResult
	{
		TextureID m_TextureId{};
		ModelPublicationClaim m_Claim{};
		AssetContentVersion m_Dependency{};
		AssetResourcePublicationStepUsage m_Usage{};
		bool m_UploadQueued = false;
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept { return m_Error.empty(); }
	};

	struct ModelPublicationMaterialResult
	{
		MaterialID m_MaterialId{};
		AssetResourcePublicationStepUsage m_Usage{};
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept { return m_Error.empty(); }
	};

	struct ModelPublicationMeshResult
	{
		MeshID m_MeshId{};
		ModelPublicationClaim m_Claim{};
		AssetContentVersion m_Dependency{};
		AssetResourcePublicationStepUsage m_Usage{};
		bool m_UploadQueued = false;
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept { return m_Error.empty(); }
	};

	struct ModelPublicationCommit
	{
		AssetContentVersion m_Model{};
		std::string m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ModelMesh> m_MeshInstances;
		ModelPublicationOwnerToken m_DependencyOwner{};
		std::span<const ModelPublicationLeaseToken> m_DependencyLeases;
		uint32_t m_QueuedTextureUploads = 0;
		uint32_t m_QueuedMeshUploads = 0;
	};

	class AssetPublicationServicesBase
	{
	public:
		virtual ~AssetPublicationServicesBase() = default;

		[[nodiscard]] virtual bool PrepareModelForPublication(
			const AssetContentVersion& model) noexcept = 0;
		[[nodiscard]] virtual ModelPublicationTextureResult PublishTexture(
			ImportedTexture& importedTexture,
			TaskPriority priority) noexcept = 0;
		[[nodiscard]] virtual ModelPublicationMaterialResult PublishMaterial(
			const ImportedMaterial* importedMaterial,
			std::span<const TextureID> textureIds) noexcept = 0;
		[[nodiscard]] virtual ModelPublicationMeshResult PublishMesh(
			const AssetContentVersion& model,
			uint32_t sourceMeshIndex,
			ImportedMesh& importedMesh,
			TaskPriority priority) noexcept = 0;

		[[nodiscard]] virtual ModelPublicationOwnerToken CreateDependencyOwner(
			const AssetContentVersion& model) noexcept = 0;
		[[nodiscard]] virtual ModelPublicationLeaseToken AcquireDependencyLease(
			ModelPublicationOwnerToken owner,
			const AssetContentVersion& dependency,
			TaskPriority priority) noexcept = 0;
		[[nodiscard]] virtual std::string CommitModel(
			ModelPublicationCommit&& commit) noexcept = 0;

		virtual void ReleaseRetain(ModelPublicationRetainToken retain) noexcept = 0;
		virtual void CancelClaimIfUnreferenced(
			const ModelPublicationClaim& claim) noexcept = 0;
		virtual void RollbackClaimIfUnreferenced(
			const ModelPublicationClaim& claim) noexcept = 0;
		virtual void RemoveMaterial(MaterialID materialId) noexcept = 0;
		virtual void ReleaseDependencyLease(
			ModelPublicationLeaseToken lease) noexcept = 0;
		virtual void DestroyDependencyOwner(
			ModelPublicationOwnerToken owner) noexcept = 0;
		virtual void AbortModel(
			const AssetContentVersion& model,
			AssetResourcePublicationAbortReason reason) noexcept = 0;
	};
}
