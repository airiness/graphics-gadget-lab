#pragma once
#include "Core/StringId.h"
#include "Graphics/RenderGraph/RGCompileDiagnostic.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	class RGBarrierPlanner;
	class RGCompiler;
	class RGPassBase;
	struct RGVirtualResourceBase;

	struct RGPassDependencyEdge
	{
		RGPassNodeIndex m_From = InvalidRGPassNodeIndex;
		RGPassNodeIndex m_To = InvalidRGPassNodeIndex;
		RGResourceNodeIndex m_ResourceNodeIndex = InvalidRGResourceNodeIndex;
		RGDependencyReason m_Reason = RGDependencyReason::WriterToReader;
	};

	struct RGBarrierIntent
	{
		RGVirtualResourceIndex m_Resource = InvalidRGVirtualResourceIndex;
		RHIResourceState m_Before = CommonRHIResourceState();
		RHIResourceState m_After = CommonRHIResourceState();
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGCompiledAccess
	{
		RGResourceNodeIndex m_ResourceNodeIndex = InvalidRGResourceNodeIndex;
		RGVirtualResourceIndex m_Resource = InvalidRGVirtualResourceIndex;
		uint64_t m_AccessValue = 0;
		RHIStage m_Stages = RHIStage::None;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		RGDependencyAccess m_DependencyAccess = RGDependencyAccess::Read;
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
	};

	struct RGCompiledPass
	{
		RGPassNodeIndex m_Declaration = InvalidRGPassNodeIndex;
		StringID m_NameId;
		RGPassBase* m_Executor = nullptr;
		bool m_SideEffect = false;
		bool m_Culled = true;
		int32_t m_ExecutionOrder = -1;
		std::vector<RGCompiledAccess> m_Accesses;
		std::vector<RGPassNodeIndex> m_Dependencies;
		std::vector<RGPassNodeIndex> m_Dependents;
		std::vector<RGBarrierIntent> m_PreBarriers;
		std::vector<RGBarrierIntent> m_PostBarriers;
		std::vector<RGVirtualResourceIndex> m_AcquireResources;
		std::vector<RGVirtualResourceIndex> m_ReleaseResources;
	};

	struct RGCompiledResource
	{
		RGVirtualResourceIndex m_Declaration = InvalidRGVirtualResourceIndex;
		RGVirtualResourceBase* m_Resource = nullptr;
		StringID m_NameId;
		RGResourceType m_ResourceType = RGResourceType::RGTexture;
		bool m_Imported = false;
		int32_t m_RefCount = 0;
		RGPassNodeIndex m_FirstUser = InvalidRGPassNodeIndex;
		RGPassNodeIndex m_LastUser = InvalidRGPassNodeIndex;
		uint64_t m_UsageBits = 0;
		RHIResourceState m_InitialState = CommonRHIResourceState();
		std::optional<RHIResourceState> m_FinalState;
		std::optional<RHISubresourceRange> m_FinalSubresources;
		RGPassNodeIndex m_ExportPass = InvalidRGPassNodeIndex;
		RGResourceNodeIndex m_ExportResourceNode = InvalidRGResourceNodeIndex;
	};

	struct RGCompiledTextureView
	{
		RGVirtualResourceIndex m_Resource = InvalidRGVirtualResourceIndex;
		RHITextureViewDesc m_Desc{};
	};

	class RGExecutionPlan final
	{
	public:
		RGExecutionPlan(const RGExecutionPlan&) = delete;
		RGExecutionPlan& operator=(const RGExecutionPlan&) = delete;
		RGExecutionPlan(RGExecutionPlan&&) noexcept = default;
		RGExecutionPlan& operator=(RGExecutionPlan&&) noexcept = default;
		~RGExecutionPlan() noexcept = default;

		[[nodiscard]] const std::vector<RGCompiledPass>& GetPasses() const noexcept { return m_Passes; }
		[[nodiscard]] const std::vector<RGCompiledResource>& GetResources() const noexcept { return m_Resources; }
		[[nodiscard]] const std::vector<RGCompiledTextureView>& GetTextureViews() const noexcept { return m_TextureViews; }
		[[nodiscard]] const std::vector<RGPassDependencyEdge>& GetDependencyEdges() const noexcept { return m_DependencyEdges; }
		[[nodiscard]] const std::vector<RGPassNodeIndex>& GetExecutionOrder() const noexcept { return m_ExecutionOrder; }
		[[nodiscard]] const std::vector<RGCompileDiagnostic>& GetDiagnostics() const noexcept { return m_Diagnostics; }

	private:
		RGExecutionPlan() noexcept = default;

		std::vector<RGCompiledPass> m_Passes;
		std::vector<RGCompiledResource> m_Resources;
		std::vector<RGCompiledTextureView> m_TextureViews;
		std::vector<RGPassDependencyEdge> m_DependencyEdges;
		std::vector<RGPassNodeIndex> m_ExecutionOrder;
		std::vector<RGCompileDiagnostic> m_Diagnostics;

		friend class RGBarrierPlanner;
		friend class RGCompiler;
	};
}
