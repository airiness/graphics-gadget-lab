#pragma once

#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Edit/VoxelOperationDetail.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublicationDetail.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/VoxelRestore.h"

#include <cstddef>
#include <cstdio>
#include <string_view>

namespace napa::voxel::testing
{
	// Fault-injection seam types. The canonical probe lives in
	// napa::voxel::detail and is consulted by production allocation paths.
	using VoxelOperationAllocationProbe = detail::VoxelOperationAllocationProbe;
	using VoxelMutationAllocationProbe = VoxelOperationAllocationProbe;
	using VoxelRestoreAllocationProbe = VoxelOperationAllocationProbe;

	// Fault-injection drivers. Owned by the test target; they configure the
	// published detail seams and call the public API.
	[[nodiscard]] ValidationResult ApplySphereEditWithAllocationProbe(
		VoxelWorld& world, const SphereEditRequest& request,
		VoxelMutationResult& result, VoxelMutationAllocationProbe& probe);
	[[nodiscard]] ValidationResult ApplySphereEditWithExhaustedRevision(
		VoxelWorld& world, const SphereEditRequest& request, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult RestoreAllWithAllocationProbe(
		VoxelWorld& world, VoxelMutationResult& result, VoxelRestoreAllocationProbe& probe);
	[[nodiscard]] ValidationResult RestoreAllWithExhaustedRevision(
		VoxelWorld& world, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult PrepareWithAuthoritativeRevision(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible, std::uint64_t authoritativeRevision,
		std::unique_ptr<PendingDataOnlyPublication>& pending);
	[[nodiscard]] ValidationResult PrepareWithAllocationFailure(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible,
		std::unique_ptr<PendingDataOnlyPublication>& pending);

	struct TestSummary
	{
		size_t m_CheckCount = 0;
		size_t m_FailureCount = 0;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_CheckCount > 0 && m_FailureCount == 0;
		}
	};

	class TestReporterBase
	{
	public:
		virtual ~TestReporterBase() = default;

		virtual void OnSuiteStarted(std::string_view suiteName) noexcept = 0;
		virtual void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept = 0;
		virtual void OnSuiteFinished(
			std::string_view suiteName, const TestSummary& summary) noexcept = 0;
	};

	class TestContext final
	{
	public:
		explicit TestContext(TestReporterBase& reporter) noexcept;

		void Check(bool condition, std::string_view name) noexcept;
		[[nodiscard]] const TestSummary& GetSummary() const noexcept;

	private:
		TestReporterBase& m_Reporter;
		TestSummary m_Summary{};
	};

	using TestSuiteFunction = void (*)(TestContext& context) noexcept;

	struct TestSuiteDesc
	{
		std::string_view m_Id;
		TestSuiteFunction m_Run = nullptr;

		[[nodiscard]] bool IsValid() const noexcept { return !m_Id.empty() && m_Run != nullptr; }
	};

	class ConsoleTestReporter final : public TestReporterBase
	{
	public:
		void OnSuiteStarted(std::string_view suiteName) noexcept override;
		void OnCheckCompleted(std::string_view checkName, bool succeeded) noexcept override;
		void OnSuiteFinished(
			std::string_view suiteName, const TestSummary& summary) noexcept override;
	};

	[[nodiscard]] bool RunTestSuite(
		const TestSuiteDesc& suite, TestReporterBase& reporter) noexcept;
}
