#pragma once
#include "Core/CoreMacros.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/AssetIdentity.h"
#include "Graphics/ModelImporter.h"

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

	using AssetLoadCompletion = std::variant<
		ModelImportSucceeded,
		ModelImportFailed,
		MeshReloadSucceeded,
		MeshReloadFailed>;

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

		[[nodiscard]] TaskHandle GetModelImportTask(
			AssetContentVersion contentVersion) const noexcept;
		[[nodiscard]] bool HasMeshReload(
			AssetContentVersion sourceModelVersion) const noexcept;
		[[nodiscard]] bool CancelModelImport(
			AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool UpdateModelImportPriority(
			AssetContentVersion contentVersion,
			TaskPriority priority) noexcept;
		[[nodiscard]] bool UpdateMeshReloadPriority(
			AssetContentVersion sourceModelVersion,
			TaskPriority priority) noexcept;

		[[nodiscard]] bool IsCurrentModelImport(
			AssetOperationToken operation) const noexcept;
		[[nodiscard]] bool IsCurrentMeshReload(
			AssetOperationToken operation) const noexcept;
		void CompleteModelImport(AssetOperationToken operation) noexcept;
		void CompleteMeshReload(AssetOperationToken operation) noexcept;
		void DiscardModelImport(AssetKey model) noexcept;

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
		std::vector<AssetLoadCompletion> m_PendingCompletions;
	};
}
