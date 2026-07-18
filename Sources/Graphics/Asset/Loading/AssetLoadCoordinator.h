#pragma once
#include "Core/CoreMacros.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"
#include "Graphics/Asset/Loading/ModelImporter.h"
#include "Graphics/Asset/TextureAsset.h"

#include <filesystem>
#include <unordered_map>
#include <variant>
#include <vector>

namespace gglab
{
	class TaskSystem;

	struct AssetLoadSubmission
	{
		AssetOperationToken m_Operation{};
		TaskHandle m_Task{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Operation.IsValid() && m_Task.IsValid();
		}
	};

	struct ModelImportSucceeded
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
		ImportedModel m_Model;
	};

	struct ModelImportFailed
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
	};

	struct MeshReloadSucceeded
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
		ImportedModel m_Model;
	};

	struct MeshReloadFailed
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
	};

	struct TextureDecodeSucceeded
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TextureAssetData m_TextureData;
		AssetContentFingerprint m_ContentFingerprint{};
		bool m_ResidencyReload = false;
		AssetResidencyOperation m_ResidencyOperation{};
	};

	struct TextureDecodeFailed
	{
		AssetOperationToken m_Operation{};
		TaskCompletionInfo m_Completion{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		bool m_ResidencyReload = false;
		AssetResidencyOperation m_ResidencyOperation{};
	};

	using AssetLoadCompletion = std::variant<
		ModelImportSucceeded,
		ModelImportFailed,
		MeshReloadSucceeded,
		MeshReloadFailed,
		TextureDecodeSucceeded,
		TextureDecodeFailed>;

	struct ModelImportRequest
	{
		AssetContentVersion m_ContentVersion{};
		std::filesystem::path m_SourcePath;
		ModelImportSettings m_ImportSettings{};
		TaskPriority m_Priority = TaskPriority::Normal;
		ProgressChannelPtr m_Progress;
	};

	struct MeshReloadRequest
	{
		AssetContentVersion m_SourceModelVersion{};
		std::filesystem::path m_SourcePath;
		ModelImportSettings m_ImportSettings{};
		TaskPriority m_Priority = TaskPriority::Normal;
	};

	struct TextureDecodeRequest
	{
		AssetContentVersion m_ContentVersion{};
		std::filesystem::path m_SourcePath;
		TextureImportSettings m_ImportSettings{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		TaskPriority m_Priority = TaskPriority::Normal;
		ProgressChannelPtr m_Progress;
		bool m_ResidencyReload = false;
		AssetResidencyOperation m_ResidencyOperation{};
	};

	class AssetLoadCoordinator final
	{
	public:
		struct CreateInfo
		{
			TaskSystem* m_TaskSystem = nullptr;
		};

		explicit AssetLoadCoordinator(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetLoadCoordinator);
		~AssetLoadCoordinator();

		[[nodiscard]] AssetLoadSubmission SubmitModelImport(
			ModelImportRequest request) noexcept;
		[[nodiscard]] AssetLoadSubmission SubmitMeshReload(
			MeshReloadRequest request) noexcept;
		[[nodiscard]] AssetLoadSubmission SubmitTextureDecode(
			TextureDecodeRequest request) noexcept;

		[[nodiscard]] TaskHandle GetModelImportTask(
			AssetContentVersion contentVersion) const noexcept;
		[[nodiscard]] bool HasMeshReload(
			AssetContentVersion sourceModelVersion) const noexcept;
		[[nodiscard]] TaskHandle GetTextureDecodeTask(
			AssetContentVersion contentVersion) const noexcept;
		[[nodiscard]] bool CancelModelImport(
			AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool UpdateModelImportPriority(
			AssetContentVersion contentVersion,
			TaskPriority priority) noexcept;
		[[nodiscard]] bool UpdateMeshReloadPriority(
			AssetContentVersion sourceModelVersion,
			TaskPriority priority) noexcept;
		[[nodiscard]] bool CancelTextureDecode(
			AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool UpdateTextureDecodePriority(
			AssetContentVersion contentVersion,
			TaskPriority priority) noexcept;

		[[nodiscard]] bool IsCurrentModelImport(
			AssetOperationToken operation) const noexcept;
		[[nodiscard]] bool IsCurrentMeshReload(
			AssetOperationToken operation) const noexcept;
		[[nodiscard]] bool IsCurrentTextureDecode(
			AssetOperationToken operation) const noexcept;
		void CompleteModelImport(AssetOperationToken operation) noexcept;
		void CompleteMeshReload(AssetOperationToken operation) noexcept;
		void CompleteTextureDecode(AssetOperationToken operation) noexcept;
		void DiscardModelImport(AssetKey model) noexcept;
		void DiscardTextureDecode(AssetKey texture) noexcept;

		void DrainCompletions(std::vector<AssetLoadCompletion>& output) noexcept;
		[[nodiscard]] bool HasActiveOperations() const noexcept;
		[[nodiscard]] bool HasPendingCompletions() const noexcept
		{
			return !m_PendingCompletions.empty();
		}

	private:
		struct OperationRecord
		{
			AssetOperationToken m_Operation{};
			TaskHandle m_Task{};
		};

		using OperationMap =
			std::unordered_map<AssetKey, OperationRecord, AssetKeyHash>;

		[[nodiscard]] AssetOperationToken AllocateOperation(
			AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] static bool Matches(
			const OperationMap& operations,
			AssetOperationToken operation) noexcept;
		static void Complete(
			OperationMap& operations,
			AssetOperationToken operation) noexcept;

		TaskSystem* m_TaskSystem = nullptr;
		uint64_t m_NextOperationSerial = 1;
		OperationMap m_ModelImports;
		OperationMap m_MeshReloads;
		OperationMap m_TextureDecodes;
		std::vector<AssetLoadCompletion> m_PendingCompletions;
	};
}
