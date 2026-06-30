#pragma once
#include "Graphics/RenderGraph/RGArenaAllocator.h"
#include "Graphics/Resource/TransientResourcePool.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/RenderGraph/RGPass.h"
#include "Graphics/RenderGraph/RGBlackboard.h"
#include "Graphics/RenderGraph/RGCompileDiagnostic.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/GraphicsTypes.h"
#include "Core/StringId.h"

#include <memory>

namespace gglab
{
	class RHIGraphicsCommandContext;
	class RHIComputeCommandContext;
	class RGPassBase;
	class RGExecutionPlan;
	class RGExecutor;
	class RGCompiler;
	template<typename PassData> class RGPass;
	template<typename PassData, typename ExecuteFunc> class RGPassConcrete;

	struct RGPassNode;
	struct RGResourceNode;

	class RenderGraph;

	struct RGBackendExecuteContext
	{
		RHIGraphicsCommandContext* m_GraphicsCommandContext = nullptr;
		RHIComputeCommandContext* m_ComputeCommandContext = nullptr;
	};

	struct RGExecuteContext
	{
		explicit RGExecuteContext(RGBackendExecuteContext backend = {}) noexcept :
			m_Backend(backend)
		{}

		RHITextureViewHandle GetViewHandle(RGTextureViewId viewId) const noexcept;
		RHIDescriptorHandle GetViewDescriptor(RGTextureViewId viewId) const noexcept;
		RHIGraphicsCommandContext* GetGraphicsCommandContext() const noexcept { return m_Backend.m_GraphicsCommandContext; }
		RHIComputeCommandContext* GetComputeCommandContext() const noexcept { return m_Backend.m_ComputeCommandContext; }

		RGBackendExecuteContext& GetBackend() noexcept { return m_Backend; }
		const RGBackendExecuteContext& GetBackend() const noexcept { return m_Backend; }

	private:
		RGBackendExecuteContext m_Backend{};
		const RGExecutionPlan* m_ExecutionPlan = nullptr;
		RHIDevice* m_Device = nullptr;

		friend class RGExecutor;
	};

	struct RGVirtualResourceBase
	{
		RGVirtualResourceBase() noexcept = default;
		virtual ~RGVirtualResourceBase() = default;
		virtual void Devirtualize(TransientResourcePool*, uint64_t usageBits) noexcept = 0;
		virtual void Release(
			std::vector<TransientTextureAllocation>& retireTextures,
			std::vector<TransientBufferAllocation>& retireBuffers) noexcept = 0;

		StringID m_NameId;
		bool m_Imported = false;
		bool m_Devirtualized = false;

		RHIResourceState m_InitialBarrierState = CommonRHIResourceState();
		std::optional<RHIResourceState> m_FinalBarrierState;
		std::optional<RHISubresourceRange> m_FinalBarrierSubresources;
		RGPassNodeIndex m_ExportPass = InvalidRGPassNodeIndex;
		RGResourceNodeIndex m_ExportResourceNodeIndex = InvalidRGResourceNodeIndex;

		RGResourceType m_ResourceType = RGResourceType::RGTexture;
	};

	template<typename RESOURCE>
	struct RGVirtualResource : RGVirtualResourceBase
	{
		using Desc = typename RESOURCE::Descriptor;
		using SubresourceDesc = typename RESOURCE::SubresourceDescriptor;
		using Access = typename RESOURCE::Access;
		using Handle = typename RGResourceTraits<RESOURCE>::Handle;
		using PhysicalAllocation = typename RGResourceTraits<RESOURCE>::PhysicalAllocation;

		Desc m_Desc = {};
		SubresourceDesc m_SubresourceDesc = {};
		PhysicalAllocation m_PhysicalAllocation{};
		Handle m_ImportedHandle{};

		void Devirtualize(TransientResourcePool* pool, uint64_t usageBits) noexcept override;
		void Release(
			std::vector<TransientTextureAllocation>& retireTextures,
			std::vector<TransientBufferAllocation>& retireBuffers) noexcept override;
	};

	struct RGResourceNode
	{
		RGResourceHandle m_ResourceHandle;
		RGVirtualResourceBase* m_VirtualResource = nullptr;

		RGResourceNodeIndex m_Previous = InvalidRGResourceNodeIndex;
		RGPassNodeIndex m_Writer = InvalidRGPassNodeIndex;
		std::vector<RGPassNodeIndex> m_Readers;

		StringID NameId() const noexcept
		{
			return m_VirtualResource ? m_VirtualResource->m_NameId : StringID();
		}
	};

	struct RGPassNode
	{
		struct Access
		{
			RGResourceNodeIndex m_ResourceNodeIndex{};
			uint64_t m_AccessValue = 0;
			RHIStage m_Stages = RHIStage::None;
			RGResourceType m_ResourceType = RGResourceType::RGTexture;
			RGDependencyAccess m_DependencyAccess = RGDependencyAccess::Read;
			std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
		};

		StringID m_NameId;
		bool m_SideEffect = false;
		std::vector<Access> m_Accesses;

		RGPassBase* m_Pass = nullptr;
	};

	struct RGResourceSlot
	{
		RGVirtualResourceIndex m_VirtualResourceIndex;
		RGResourceNodeIndex m_ResourceNodeIndex;
		RGResourceHandle::Version m_Version = RGResourceHandle::UnintializedVersion;
	};

	struct RGSnapshot;
	void BuildRenderGraphSnapshot(const RenderGraph& rg, RGSnapshot& outSnapshot) noexcept;

	class RenderGraph
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TransientResourcePool* m_TransientResourcePool = nullptr;
		};

		class RGBuilder
		{
		public:
			RGBuilder(RenderGraph& rg, RGPassNodeIndex passNodeIndex) :
				m_RG(rg),
				m_PassNodeIndex(passNodeIndex)
			{}
			~RGBuilder() = default;

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Create(const char* name, const RESOURCE::Descriptor& desc = {}) noexcept
			{
				return m_RG.CreateInternal<RESOURCE>(name, desc);
			}

			RGTextureId CreateTexture(const char* name, const RHITextureDesc& desc = {}) noexcept
			{
				return Create<RGTextureResource>(name, desc);
			}

			RGBufferId CreateBuffer(const char* name, const RHIBufferDesc& desc = {}) noexcept
			{
				return Create<RGBufferResource>(name, desc);
			}

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Import(const char* name,
				typename RGResourceTraits<RESOURCE>::Handle handle,
				const typename RESOURCE::Descriptor& desc,
				typename RESOURCE::Access initialAccess) noexcept
			{
				return m_RG.ImportInternal<RESOURCE>(name, handle, desc, initialAccess);
			}

			RGTextureId ImportTexture(const char* name,
				RHITextureHandle texture,
				const RHITextureDesc& desc,
				RGTextureAccess initialAccess) noexcept
			{
				return Import<RGTextureResource>(name, texture, desc, initialAccess);
			}

			template<RHITextureViewType T>
			RGTextureViewId CreateView(
				RGTextureId textureId,
				std::optional<RHITextureViewDesc> desc = std::nullopt) noexcept
			{
				return m_RG.CreateTextureView<T>(textureId, desc);
			}

			RGBufferId ImportBuffer(const char* name,
				RHIBufferHandle buffer,
				const RHIBufferDesc& desc,
				RGBufferAccess initialAccess) noexcept
			{
				return Import<RGBufferResource>(name, buffer, desc, initialAccess);
			}

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Read(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access = RESOURCE::DefaultReadAccess,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.ReadInternal<RESOURCE>(
					m_PassNodeIndex,
					resourceId,
					access,
					ToRHIStages(access),
					subresources);
			}

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Read(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.ReadInternal<RESOURCE>(m_PassNodeIndex, resourceId, access, stages, subresources);
			}

			template<typename RESOURCE>
			[[nodiscard("Write returns the next resource version. Use WriteInPlace when the resource id should be updated immediately.")]]
			RGResourceId<RESOURCE> Write(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access = RESOURCE::DefaultWriteAccess,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.WriteInternal<RESOURCE>(
					m_PassNodeIndex,
					resourceId,
					access,
					ToRHIStages(access),
					subresources);
			}

			template<typename RESOURCE>
			[[nodiscard("Write returns the next resource version. Use WriteInPlace when the resource id should be updated immediately.")]]
			RGResourceId<RESOURCE> Write(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.WriteInternal<RESOURCE>(m_PassNodeIndex, resourceId, access, stages, subresources);
			}

			template<typename RESOURCE>
			void WriteInPlace(RGResourceId<RESOURCE>& resourceId,
				typename RESOURCE::Access access = RESOURCE::DefaultWriteAccess,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				resourceId = Write(resourceId, access, subresources);
			}

			template<typename RESOURCE>
			void WriteInPlace(RGResourceId<RESOURCE>& resourceId,
				typename RESOURCE::Access access,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				resourceId = Write(resourceId, access, stages, subresources);
			}

			template<typename RESOURCE>
			[[nodiscard("ReadWrite returns the next resource version. Use ReadWriteInPlace when the resource id should be updated immediately.")]]
			RGResourceId<RESOURCE> ReadWrite(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.ReadWriteInternal<RESOURCE>(
					m_PassNodeIndex,
					resourceId,
					access,
					ToRHIStages(access),
					subresources);
			}

			template<typename RESOURCE>
			[[nodiscard("ReadWrite returns the next resource version. Use ReadWriteInPlace when the resource id should be updated immediately.")]]
			RGResourceId<RESOURCE> ReadWrite(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access access,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				return m_RG.ReadWriteInternal<RESOURCE>(m_PassNodeIndex, resourceId, access, stages, subresources);
			}

			template<typename RESOURCE>
			void ReadWriteInPlace(RGResourceId<RESOURCE>& resourceId,
				typename RESOURCE::Access access,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				resourceId = ReadWrite(resourceId, access, subresources);
			}

			template<typename RESOURCE>
			void ReadWriteInPlace(RGResourceId<RESOURCE>& resourceId,
				typename RESOURCE::Access access,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				resourceId = ReadWrite(resourceId, access, stages, subresources);
			}

			template<typename RESOURCE>
			void Export(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access finalAccess,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				m_RG.ExportInternal<RESOURCE>(
					m_PassNodeIndex,
					resourceId,
					finalAccess,
					ToRHIStages(finalAccess),
					subresources);
			}

			template<typename RESOURCE>
			void Export(RGResourceId<RESOURCE> resourceId,
				typename RESOURCE::Access finalAccess,
				RHIStage stages,
				std::optional<typename RESOURCE::SubresourceDescriptor> subresources = std::nullopt) noexcept
			{
				m_RG.ExportInternal<RESOURCE>(m_PassNodeIndex, resourceId, finalAccess, stages, subresources);
			}

			void SideEffect() noexcept
			{
				GGLAB_ASSERT_MSG(m_PassNodeIndex.IsValid(), "RGPassNode Index must be valid.");

				m_RG.m_PassNodes[m_PassNodeIndex.Value()].m_SideEffect = true;
			}

			RGBlackboard& GetBlackboard() noexcept { return m_RG.m_Blackboard; }
			const RGBlackboard& GetBlackboard() const noexcept { return m_RG.m_Blackboard; }

		private:
			RenderGraph& m_RG;
			RGPassNodeIndex m_PassNodeIndex = InvalidRGPassNodeIndex;
		};

	public:
		explicit RenderGraph(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderGraph);
		~RenderGraph() noexcept;

		template<typename PassData, typename SetupFunc, typename ExecuteFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc, ExecuteFunc&& executeFunc) noexcept;

		template<typename PassData, typename SetupFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc) noexcept;

		// Add an always - executed pass with no pass data or resource declarations.
		template<typename ExecuteFunc>
		auto* AddTrivialSideEffectPass(const char* passName, ExecuteFunc&& executeFunc) noexcept;

		[[nodiscard]] bool Compile() noexcept;
		void Execute(RGExecuteContext& executeContext) noexcept;
		void Retire(const RHIFencePoint& fencePoint) noexcept;

		[[nodiscard]] const RGExecutionPlan* GetExecutionPlan() const noexcept { return m_ExecutionPlan.get(); }
		[[nodiscard]] const std::vector<RGCompileDiagnostic>& GetCompileDiagnostics() const noexcept
		{
			return m_CompileDiagnostics;
		}

		RGBlackboard& GetBlackboard() noexcept { return m_Blackboard; }
		const RGBlackboard& GetBlackboard() const noexcept { return m_Blackboard; }

	private:
		struct TextureViewRecord
		{
			RGTextureId m_Texture{};
			std::optional<RHITextureViewDesc> m_Desc{};
			RHITextureViewType m_Type = RHITextureViewType::RenderTarget;
		};

		template<typename RESOURCE>
		RGResourceId<RESOURCE> CreateInternal(const char* name,
			const typename RESOURCE::Descriptor& desc) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> ImportInternal(const char* name,
			typename RGResourceTraits<RESOURCE>::Handle handle,
			const typename RESOURCE::Descriptor& desc,
			typename RESOURCE::Access initialAccess) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> ReadInternal(RGPassNodeIndex passNodeIndex,
			RGResourceId<RESOURCE> resourceId,
			RESOURCE::Access access,
			RHIStage stages,
			std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> WriteInternal(RGPassNodeIndex passNodeIndex,
			RGResourceId<RESOURCE> resourceId,
			RESOURCE::Access access,
			RHIStage stages,
			std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> ReadWriteInternal(RGPassNodeIndex passNodeIndex,
			RGResourceId<RESOURCE> resourceId,
			RESOURCE::Access access,
			RHIStage stages,
			std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept;

		template<typename RESOURCE>
		void ExportInternal(RGPassNodeIndex passNodeIndex,
			RGResourceId<RESOURCE> resourceId,
			RESOURCE::Access finalAccess,
			RHIStage stages,
			std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept;

		template<RHITextureViewType T>
		RGTextureViewId CreateTextureView(
			RGTextureId textureId,
			std::optional<RHITextureViewDesc> desc) noexcept;

		RGVirtualResourceBase* GetVirtualResource(RGResourceHandle handle) const noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransientResourcePool* m_TransientResourcePool = nullptr;

		RGArenaAllocator m_ArenaAllocator;
		RGBlackboard m_Blackboard;

		std::vector<RGResourceNode> m_ResourceNodes;
		std::vector<RGPassNode> m_PassNodes;
		std::vector<RGVirtualResourceBase*> m_VirtualResources;
		std::vector<RGResourceSlot> m_ResourceSlots;
		std::vector<TextureViewRecord> m_TextureViews;

		std::unordered_map<StringID, RGResourceHandle> m_NameHandleMap;

		std::vector<TransientTextureAllocation> m_MarkedRetireTextures;
		std::vector<TransientBufferAllocation> m_MarkedRetireBuffers;
		std::unique_ptr<RGExecutionPlan> m_ExecutionPlan;
		std::vector<RGCompileDiagnostic> m_CompileDiagnostics;
		bool m_BuildValid = true;

		friend class RGBuilder;
		friend class RGCompiler;

		friend void BuildRenderGraphSnapshot(const RenderGraph& rg, RGSnapshot& outSnapshot) noexcept;
	};

	template<typename PassData, typename SetupFunc, typename ExecuteFunc>
	inline auto* RenderGraph::AddPass(const char* passName, SetupFunc setupFunc, ExecuteFunc&& executeFunc) noexcept
	{
		using PassDataType = std::decay_t<PassData>;
		using ExecuteFuncType = std::decay_t<ExecuteFunc>;
		using PassType = RGPassConcrete<PassDataType, ExecuteFuncType>;

		GGLAB_ASSERT_MSG(passName && *passName, "Pass name must not be null.");

		auto* pass = m_ArenaAllocator.MakeTracked<PassType>(std::forward<ExecuteFunc>(executeFunc));

		RGPassNode passNode = {};
		passNode.m_NameId = StringID(passName);
		passNode.m_Pass = pass;

		RGPassNodeIndex passIndex{ static_cast<uint32_t>(m_PassNodes.size()) };
		m_PassNodes.push_back(passNode);

		RGBuilder builder(*this, passIndex);
		setupFunc(builder, pass->GetData());

		return pass;
	}

	template<RHITextureViewType T>
	inline RGTextureViewId RenderGraph::CreateTextureView(
		RGTextureId textureId,
		std::optional<RHITextureViewDesc> desc) noexcept
	{
		GGLAB_ASSERT_MSG(textureId.IsValid(),
			"RenderGraph::CreateTextureView requires a valid texture id.");
		if (!textureId.IsValid())
		{
			return InvalidRGTextureViewId;
		}

		const auto* virtualResource = GetVirtualResource(textureId);
		GGLAB_ASSERT_MSG(
			virtualResource && virtualResource->m_ResourceType == RGResourceType::RGTexture,
			"RenderGraph::CreateTextureView requires a texture resource.");
		if (!virtualResource ||
			virtualResource->m_ResourceType != RGResourceType::RGTexture)
		{
			return InvalidRGTextureViewId;
		}

		if (desc)
		{
			desc->m_Type = T;
		}

		TextureViewRecord view{
			.m_Texture = textureId,
			.m_Desc = desc,
			.m_Type = T,
		};

		GGLAB_ASSERT_MSG(m_TextureViews.size() < RGTextureViewId::InvalidValue,
			"RenderGraph texture view id overflow.");
		if (m_TextureViews.size() >= RGTextureViewId::InvalidValue)
		{
			return InvalidRGTextureViewId;
		}

		const RGTextureViewId viewId{
			static_cast<RGTextureViewId::ValueType>(m_TextureViews.size())
		};
		m_TextureViews.push_back(std::move(view));
		return viewId;
	}

	template<typename PassData, typename SetupFunc>
	inline auto* RenderGraph::AddPass(const char* passName, SetupFunc setupFunc) noexcept
	{
		using PassDataType = std::decay_t<PassData>;

		return AddPass<PassDataType>(passName, std::move(setupFunc),
			[](RGExecuteContext&, PassDataType&) noexcept {});
	}

	template<typename ExecuteFunc>
	inline auto* RenderGraph::AddTrivialSideEffectPass(const char* passName, ExecuteFunc&& executeFunc) noexcept
	{
		struct EmptyData {};
		return AddPass<EmptyData>(passName,
			[](RGBuilder& builder, EmptyData&) { builder.SideEffect(); },
			[fn = std::forward<ExecuteFunc>(executeFunc)](RGExecuteContext& executeContext, EmptyData&)
			{
				fn(executeContext);
			});
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::CreateInternal(const char* name,
		const typename RESOURCE::Descriptor& desc) noexcept
	{
		using RHIUsage = std::remove_cvref_t<decltype(desc.m_Usage)>;
		GGLAB_ASSERT_MSG(desc.m_Usage == RHIUsage::None,
			"RenderGraph resource usage is inferred from reachable pass accesses during Compile().");

		RGResourceHandle handle{ RGResourceHandle::Handle(static_cast<uint16_t>(m_ResourceSlots.size())), 1 };

		RGResourceSlot slot;
		slot.m_VirtualResourceIndex = RGVirtualResourceIndex{ static_cast<uint32_t>(m_VirtualResources.size()) };
		slot.m_ResourceNodeIndex = RGResourceNodeIndex{ static_cast<uint32_t>(m_ResourceNodes.size()) };
		slot.m_Version = 1;

		m_ResourceSlots.push_back(slot);

		RGVirtualResource<RESOURCE>* virtualResource = m_ArenaAllocator.MakeTracked<RGVirtualResource<RESOURCE>>();
		virtualResource->m_NameId = StringID(name);
		virtualResource->m_Devirtualized = false;
		virtualResource->m_Desc = desc;
		virtualResource->m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		m_VirtualResources.push_back(virtualResource);

		RGResourceNode resourceNode
		{
			.m_ResourceHandle = handle,
			.m_VirtualResource = virtualResource,
		};
		m_ResourceNodes.push_back(resourceNode);

		// Emplace in name handle unordered map.
		auto [iter, inserted] = m_NameHandleMap.emplace(virtualResource->m_NameId, handle);
		GGLAB_ASSERT_MSG(inserted, "CreateInternal duplicated resource name.");

		return RGResourceId<RESOURCE>{handle};
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::ImportInternal(const char* name,
		typename RGResourceTraits<RESOURCE>::Handle importedHandle,
		const typename RESOURCE::Descriptor& desc,
		typename RESOURCE::Access initialAccess) noexcept
	{
		GGLAB_ASSERT_MSG(name && *name, "Import name must be valid.");
		GGLAB_ASSERT_MSG(importedHandle.IsValid(), "Import RHI resource handle must be valid.");
		GGLAB_ASSERT_MSG(m_Device->IsAlive(importedHandle), "Import RHI resource handle must be live.");

		RGResourceHandle handle{ RGResourceHandle::Handle(static_cast<uint16_t>(m_ResourceSlots.size())), 1 };

		RGResourceSlot slot;
		slot.m_VirtualResourceIndex = RGVirtualResourceIndex{ static_cast<uint32_t>(m_VirtualResources.size()) };
		slot.m_ResourceNodeIndex = RGResourceNodeIndex{ static_cast<uint32_t>(m_ResourceNodes.size()) };
		slot.m_Version = 1;

		m_ResourceSlots.push_back(slot);

		RGVirtualResource<RESOURCE>* virtualResource = m_ArenaAllocator.MakeTracked<RGVirtualResource<RESOURCE>>();
		virtualResource->m_NameId = StringID(name);
		virtualResource->m_Imported = true;
		virtualResource->m_Devirtualized = true;	// import resource alreay devirtualized.
		virtualResource->m_InitialBarrierState = ToRHIResourceState(initialAccess);
		virtualResource->m_Desc = desc;
		virtualResource->m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		virtualResource->m_ImportedHandle = importedHandle;
		m_VirtualResources.push_back(virtualResource);

		RGResourceNode resourceNode
		{
			.m_ResourceHandle = handle,
			.m_VirtualResource = virtualResource,
		};
		m_ResourceNodes.push_back(resourceNode);

		// Emplace in name handle unordered map.
		auto [iter, inserted] = m_NameHandleMap.emplace(virtualResource->m_NameId, handle);
		GGLAB_ASSERT_MSG(inserted, "ImportInternal duplicated resource name.");

		return RGResourceId<RESOURCE>{handle};
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::ReadInternal(RGPassNodeIndex passNodeIndex,
		RGResourceId<RESOURCE> resourceId,
		typename RESOURCE::Access resourceAccess,
		RHIStage stages,
		std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must read valid resource.");
		GGLAB_ASSERT_MSG(stages != RHIStage::None, "Read requires a non-empty pipeline stage.");
		if (!resourceId.IsValid() || stages == RHIStage::None)
		{
			m_BuildValid = false;
			return {};
		}

		const auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		GGLAB_ASSERT_MSG(slot.m_Version == resourceId.GetVersion(), "Read with stale handle.Version");
		if (slot.m_Version != resourceId.GetVersion())
		{
			m_BuildValid = false;
			return {};
		}

		RGResourceNode& resourceNode = m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
		GGLAB_ASSERT_MSG(!resourceNode.m_VirtualResource->m_ExportPass.IsValid(),
			"A RenderGraph resource may not be read after Export.");
		if (resourceNode.m_VirtualResource->m_ExportPass.IsValid())
		{
			m_BuildValid = false;
			return {};
		}
		RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];
		const RGPassNodeIndex stablePassNodeIndex = passNodeIndex;

		GGLAB_ASSERT_MSG(resourceNode.m_Writer != stablePassNodeIndex, "Pass can not read this resource and write same resource.");

		resourceNode.m_Readers.push_back(stablePassNodeIndex);

		RGPassNode::Access access = {};
		access.m_ResourceNodeIndex = slot.m_ResourceNodeIndex;
		access.m_AccessValue = static_cast<uint64_t>(resourceAccess);
		access.m_Stages = stages;
		access.m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		access.m_DependencyAccess = RGDependencyAccess::Read;
		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			access.m_Subresources = subresources;
		}
		passNode.m_Accesses.push_back(access);

		return resourceId;
	}

	template<typename RESOURCE>
	inline void RenderGraph::ExportInternal(RGPassNodeIndex passNodeIndex,
		RGResourceId<RESOURCE> resourceId,
		typename RESOURCE::Access finalAccess,
		RHIStage stages,
		std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must export valid resource.");
		GGLAB_ASSERT_MSG(stages != RHIStage::None, "Export requires a non-empty pipeline stage.");
		if (!resourceId.IsValid() || stages == RHIStage::None)
		{
			m_BuildValid = false;
			return;
		}

		const auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		GGLAB_ASSERT_MSG(slot.m_Version == resourceId.GetVersion(), "Export with stale handle.Version");
		if (slot.m_Version != resourceId.GetVersion())
		{
			m_BuildValid = false;
			return;
		}

		auto* virtualResource = GetVirtualResource(resourceId);
		GGLAB_ASSERT_MSG(virtualResource != nullptr, "Export resource must exist.");
		GGLAB_ASSERT_MSG(virtualResource->m_Imported, "Only imported resources may declare an exported state.");
		if (!virtualResource || !virtualResource->m_Imported)
		{
			m_BuildValid = false;
			return;
		}
		GGLAB_ASSERT_MSG(!virtualResource->m_ExportPass.IsValid(),
			"A RenderGraph resource may only be exported once.");
		if (virtualResource->m_ExportPass.IsValid())
		{
			m_BuildValid = false;
			return;
		}

		virtualResource->m_FinalBarrierState = ToRHIResourceState(finalAccess, stages);
		virtualResource->m_ExportPass = passNodeIndex;
		virtualResource->m_ExportResourceNodeIndex = slot.m_ResourceNodeIndex;
		m_PassNodes[passNodeIndex.Value()].m_SideEffect = true;
		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			virtualResource->m_FinalBarrierSubresources = subresources;
		}
		else
		{
			virtualResource->m_FinalBarrierSubresources = std::nullopt;
		}
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::WriteInternal(RGPassNodeIndex passNodeIndex,
		RGResourceId<RESOURCE> resourceId,
		typename RESOURCE::Access resourceAccess,
		RHIStage stages,
		std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must write valid resource.");
		GGLAB_ASSERT_MSG(stages != RHIStage::None, "Write requires a non-empty pipeline stage.");
		if (!resourceId.IsValid() || stages == RHIStage::None)
		{
			m_BuildValid = false;
			return {};
		}

		auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		GGLAB_ASSERT_MSG(slot.m_Version == resourceId.GetVersion(), "Write with stale handle.Version");
		if (slot.m_Version != resourceId.GetVersion())
		{
			m_BuildValid = false;
			return {};
		}
		const RGResourceNodeIndex previousNodeIndex = slot.m_ResourceNodeIndex;
		RGResourceNode* curResourceNode = &m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
		GGLAB_ASSERT_MSG(!curResourceNode->m_VirtualResource->m_ExportPass.IsValid(),
			"A RenderGraph resource may not be written after Export.");
		if (curResourceNode->m_VirtualResource->m_ExportPass.IsValid())
		{
			m_BuildValid = false;
			return {};
		}
		RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];
		const RGPassNodeIndex stablePassNodeIndex = passNodeIndex;

		// version update
		if ((curResourceNode->m_Writer.IsValid()) || (!curResourceNode->m_Readers.empty()))
		{
			++slot.m_Version;
			resourceId.m_Version = slot.m_Version;

			RGResourceNode nextResourceNode = {};
			nextResourceNode.m_ResourceHandle = resourceId;
			nextResourceNode.m_VirtualResource = curResourceNode->m_VirtualResource;
			nextResourceNode.m_Previous = previousNodeIndex;
			slot.m_ResourceNodeIndex = RGResourceNodeIndex{ static_cast<uint32_t>(m_ResourceNodes.size()) };
			m_ResourceNodes.push_back(nextResourceNode);

			curResourceNode = &m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
		}
		curResourceNode->m_Writer = stablePassNodeIndex;

		// Imported resource -> pass side effect
		if (curResourceNode->m_VirtualResource && curResourceNode->m_VirtualResource->m_Imported)
		{
			passNode.m_SideEffect = true;
		}

		RGPassNode::Access access = {};
		access.m_ResourceNodeIndex = slot.m_ResourceNodeIndex;
		access.m_AccessValue = static_cast<uint64_t>(resourceAccess);
		access.m_Stages = stages;
		access.m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		access.m_DependencyAccess = RGDependencyAccess::Write;
		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			access.m_Subresources = subresources;
		}
		passNode.m_Accesses.push_back(access);

		return resourceId;
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::ReadWriteInternal(RGPassNodeIndex passNodeIndex,
		RGResourceId<RESOURCE> resourceId,
		typename RESOURCE::Access resourceAccess,
		RHIStage stages,
		std::optional<typename RESOURCE::SubresourceDescriptor> subresources) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must read-write valid resource.");
		GGLAB_ASSERT_MSG(stages != RHIStage::None, "ReadWrite requires a non-empty pipeline stage.");
		if (!resourceId.IsValid() || stages == RHIStage::None)
		{
			m_BuildValid = false;
			return {};
		}

		auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		GGLAB_ASSERT_MSG(slot.m_Version == resourceId.GetVersion(), "ReadWrite with stale handle.Version");
		if (slot.m_Version != resourceId.GetVersion())
		{
			m_BuildValid = false;
			return {};
		}

		const RGResourceNodeIndex previousNodeIndex = slot.m_ResourceNodeIndex;
		RGResourceNode& previousNode = m_ResourceNodes[previousNodeIndex.Value()];
		GGLAB_ASSERT_MSG(!previousNode.m_VirtualResource->m_ExportPass.IsValid(),
			"A RenderGraph resource may not be read-written after Export.");
		if (previousNode.m_VirtualResource->m_ExportPass.IsValid())
		{
			m_BuildValid = false;
			return {};
		}
		RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];
		const RGPassNodeIndex stablePassNodeIndex = passNodeIndex;

		GGLAB_ASSERT_MSG(previousNode.m_Writer != stablePassNodeIndex,
			"Pass can not read-write a resource it already writes.");
		GGLAB_ASSERT_MSG(previousNode.m_Writer.IsValid() || previousNode.m_VirtualResource->m_Imported,
			"ReadWrite requires existing contents. Use Write to initialize a transient resource.");

		previousNode.m_Readers.push_back(stablePassNodeIndex);
		auto* virtualResource = previousNode.m_VirtualResource;

		++slot.m_Version;
		resourceId.m_Version = slot.m_Version;
		RGResourceNode nextResourceNode = {};
		nextResourceNode.m_ResourceHandle = resourceId;
		nextResourceNode.m_VirtualResource = virtualResource;
		nextResourceNode.m_Previous = previousNodeIndex;
		nextResourceNode.m_Writer = stablePassNodeIndex;
		slot.m_ResourceNodeIndex = RGResourceNodeIndex{ static_cast<uint32_t>(m_ResourceNodes.size()) };
		m_ResourceNodes.push_back(nextResourceNode);

		if (virtualResource && virtualResource->m_Imported)
		{
			passNode.m_SideEffect = true;
		}

		passNode.m_Accesses.push_back(
			{
				.m_ResourceNodeIndex = previousNodeIndex,
				.m_AccessValue = static_cast<uint64_t>(resourceAccess),
				.m_Stages = stages,
				.m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType,
				.m_DependencyAccess = RGDependencyAccess::ReadWrite,
				.m_Subresources = [&]() -> std::optional<RHISubresourceRange>
				{
					if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
					{
						return subresources;
					}
					else
					{
						return std::nullopt;
					}
				}(),
			});
		return resourceId;
	}

	// RGVirtualResource functions
	template<typename RESOURCE>
	inline void RGVirtualResource<RESOURCE>::Devirtualize(
		TransientResourcePool* pool,
		uint64_t usageBits) noexcept
	{
		if (m_Devirtualized)
		{
			return;
		}

		if (m_Imported)
		{
			GGLAB_ASSERT_MSG(m_ImportedHandle.IsValid(), "Imported resource handle is invalid.");
			m_Devirtualized = true;
			return;
		}

		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			auto compiledDesc = m_Desc;
			compiledDesc.m_Usage = static_cast<RHITextureUsage>(usageBits);
			m_PhysicalAllocation = pool->AcquireTexture(compiledDesc);
		}
		else if constexpr (std::is_same_v<RESOURCE, RGBufferResource>)
		{
			auto compiledDesc = m_Desc;
			compiledDesc.m_Usage = static_cast<RHIBufferUsage>(usageBits);
			m_PhysicalAllocation = pool->AcquireBuffer(compiledDesc);
		}
		m_Devirtualized = m_PhysicalAllocation.IsValid();
		GGLAB_ASSERT_MSG(m_Devirtualized, "Failed to acquire a transient physical resource.");
	}

	template<typename RESOURCE>
	inline void RGVirtualResource<RESOURCE>::Release(
		std::vector<TransientTextureAllocation>& retireTextures,
		std::vector<TransientBufferAllocation>& retireBuffers) noexcept
	{
		if (!m_Devirtualized)
		{
			return;
		}

		if (m_Imported)
		{
			return;
		}

		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			retireTextures.push_back(std::move(m_PhysicalAllocation));
		}
		else if constexpr (std::is_same_v<RESOURCE, RGBufferResource>)
		{
			retireBuffers.push_back(std::move(m_PhysicalAllocation));
		}
		m_PhysicalAllocation.Reset();
		m_Devirtualized = false;
	}
}
