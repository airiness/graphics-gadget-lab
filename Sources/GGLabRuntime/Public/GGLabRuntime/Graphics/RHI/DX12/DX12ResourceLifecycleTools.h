#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	class RHIContext;

	enum class DX12TestResourceType : uint8_t
	{
		Texture,
		Buffer,
	};

	// Value observations only. IDs address this tools instance's own test resources;
	// slot indices/generations are diagnostic values, never mutation handles.
	struct DX12TestResourceSnapshot
	{
		uint64_t m_Id = 0;
		DX12TestResourceType m_Type = DX12TestResourceType::Texture;
		std::string m_Name;
		uint32_t m_Index = 0;
		uint32_t m_Generation = 0;
		bool m_Alive = false;
		bool m_DestroyRequested = false;
	};

	struct DX12ResourceLifecycleSnapshot
	{
		std::vector<DX12TestResourceSnapshot> m_Resources;
		std::string m_LastTestResult = "No lifecycle test has run.";
	};

	class DX12ResourceLifecycleViewBase
	{
	public:
		virtual ~DX12ResourceLifecycleViewBase() = default;
		[[nodiscard]] virtual DX12ResourceLifecycleSnapshot GetSnapshot() const noexcept = 0;
	};

	// Explicit DX12 experiments, invoked synchronously on the application/render
	// thread during tooling draw. Test allocations are never submitted as frame
	// resources. SignalAndRecordUse records a synthetic graphics fence, not a draw.
	// FlushAndCollect and RunLifecycleTest deliberately wait for submitted GPU work.
	// The backend adapter owns queue, retirement and invalid/stale-handle policy.
	class DX12ResourceLifecycleControlBase
	{
	public:
		virtual ~DX12ResourceLifecycleControlBase() = default;
		virtual void AddTexture() noexcept = 0;
		virtual void AddBuffer() noexcept = 0;
		virtual void DestroyResource(uint64_t id) noexcept = 0;
		virtual void DestroyAll() noexcept = 0;
		virtual void SignalAndRecordUse() noexcept = 0;
		virtual void RemoveDestroyedRow(uint64_t id) noexcept = 0;
		virtual void ClearDestroyedRows() noexcept = 0;
		virtual void ProbeInvalidDestroy() noexcept = 0;
		virtual void ProbeInvalidCreate() noexcept = 0;
		virtual void CollectCompleted() noexcept = 0;
		virtual void FlushAndCollect() noexcept = 0;
		virtual void RunLifecycleTest() noexcept = 0;
	};

	class DX12ResourceLifecycleToolsBase : public DX12ResourceLifecycleViewBase,
		public DX12ResourceLifecycleControlBase
	{
	public:
		~DX12ResourceLifecycleToolsBase() override = default;
	};

	// Returns null for non-DX12 contexts. Creation allocates no GPU resources.
	// The host must destroy the tools while context is alive, at its quiescent
	// tooling shutdown point. Destruction requests retirement of remaining owned
	// test resources; final fence collection still belongs to the context.
	[[nodiscard]] std::unique_ptr<DX12ResourceLifecycleToolsBase>
		CreateDX12ResourceLifecycleTools(RHIContext& context) noexcept;
}
