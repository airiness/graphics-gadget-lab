#pragma once
#include "RGArenaAllocator.h"
#include "RGGpuResourceAllocator.h"
#include "RGResource.h"
#include "RGPass.h"
#include "GraphicsTypes.h"
#include "StringId.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandList;
	class DX12ViewCache;
	class RGPassBase;
	template<typename PassData> class RGPass;
	template<typename PassData, typename ExecuteFunc> class RGPassConcrete;

	struct RGPassNode;
	struct RGResourceNode;

	class RenderGraph;

	struct RGExecuteContext
	{
		DX12CommandList* m_GraphicsCommandList = nullptr;
		DX12CommandList* m_ComputeCommandList = nullptr;
	};

	struct RGVirtualResourceBase
	{
		GGLAB_DEFINE_NESTED_TYPED_INDEX(Index, uint32_t);

		RGVirtualResourceBase() noexcept = default;
		virtual ~RGVirtualResourceBase() = default;
		virtual void Devirtualize(RGGpuResourceAllocator*) noexcept = 0;
		virtual void Destroy(RenderGraph&) noexcept = 0;

		StringId m_NameId;
		bool m_Imported = false;
		bool m_Devirtualized = false;
		int32_t m_RefCount = 0;

		RGPassNode* m_FirstUser = nullptr;
		RGPassNode* m_LastUser = nullptr;

		D3D12_RESOURCE_STATES m_CurrentStates = D3D12_RESOURCE_STATE_COMMON;

		RGResourceType m_ResourceType = RGResourceType::RGTexture;
	};

	template<typename RESOURCE>
	struct RGVirtualResource : RGVirtualResourceBase
	{
		using Desc = RESOURCE::Descriptor;
		using SubresourceDesc = RESOURCE::SubresourceDescriptor;
		using Usage = RESOURCE::Usage;

		Desc m_Desc = {};
		SubresourceDesc m_SubresourceDesc = {};
		Usage m_Usage = RESOURCE::DefaultNoneUsage;
		ResourceIndex m_GpuResourceIndex = {};

		void Devirtualize(RGGpuResourceAllocator* allocator) noexcept override;
		void Destroy(RenderGraph& rg) noexcept override;
	};

	struct RGResourceNode
	{
		GGLAB_DEFINE_NESTED_TYPED_INDEX(Index, uint32_t);

		RGResourceHandle m_ResourceHandle;
		RGVirtualResourceBase* m_VirtualResource = nullptr;

		RGPassNode* m_Writer = nullptr;
		std::vector<RGPassNode*> m_Readers;

		StringId NameId() const noexcept
		{
			return m_VirtualResource ? m_VirtualResource->m_NameId : StringId();
		}
	};

	struct RGPassNode
	{
		GGLAB_DEFINE_NESTED_TYPED_INDEX(Index, uint32_t);

		StringId m_NameId;
		bool m_SideEffect = false;
		bool m_Culled = false;

		struct Access
		{
			enum class Type : uint8_t
			{
				Read,
				Write,
			};

			RGResourceNode::Index m_ResourceNodeIndex{};
			uint64_t m_UsageBits = 0;
			RGResourceType m_ResourceType = RGResourceType::RGTexture;
			Type m_AccessType = Type::Read;
		};
		std::vector<Access> m_Accesses;

		std::vector<RGVirtualResourceBase*> m_DevirtualizeVirtualResources;
		std::vector<RGVirtualResourceBase*> m_DestroyVirtualResources;

		RGPassBase* m_Pass = nullptr;
	};

	struct RGResourceSlot
	{
		RGVirtualResourceBase::Index m_VirtualResourceIndex;
		RGResourceNode::Index m_ResourceNodeIndex;
		RGResourceHandle::Version m_Version = RGResourceHandle::UnintializedVersion;
	};

	class RenderGraph
	{
	public:
		struct CreateInfo
		{
			RGGpuResourceAllocator* m_GpuResourceAllocator = nullptr;
			DX12ViewCache* m_ViewCache = nullptr;
		};

		class RGBuilder
		{
		public:
			RGBuilder(RenderGraph& rg, RGPassNode::Index passNodeIndex) :
				m_RG(rg),
				m_PassNodeIndex(passNodeIndex)
			{
			}
			~RGBuilder() = default;

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Create(const char* name, const typename RESOURCE::Descriptor& desc = {}) noexcept
			{
				return m_RG.CreateInternal<RESOURCE>(name, desc);
			}

			RGTextureId CreateTexture(const char* name, const RGTextureDesc& desc = {}) noexcept
			{
				return Create<RGTextureResource>(name, desc);
			}

			RGBufferId CreateBuffer(const char* name, const RGBufferDesc& desc = {}) noexcept
			{
				return Create<RGBufferResource>(name, desc);
			}

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Read(RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage = RESOURCE::DefaultReadUsage) noexcept
			{
				return m_RG.ReadInternal<RESOURCE>(m_PassNodeIndex, resourceId, usage);
			}

			template<typename RESOURCE>
			RGResourceId<RESOURCE> Write(RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage = RESOURCE::DefaultWriteUsage) noexcept
			{
				return m_RG.WriteInternal<RESOURCE>(m_PassNodeIndex, resourceId, usage);
			}

			void SideEffect() noexcept
			{
				GGLAB_ASSERT_MSG(m_PassNodeIndex.IsValid(), "RGPassNode Index must be valid.");

				m_RG.m_PassNodes[m_PassNodeIndex.Value()].m_SideEffect = true;
			}

		private:
			RenderGraph& m_RG;
			RGPassNode::Index m_PassNodeIndex = RGPassNode::InvalidIndex;
		};

	public:
		explicit RenderGraph(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderGraph);
		~RenderGraph() noexcept;

		template<typename PassData, typename SetupFunc, typename ExecuteFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc, ExecuteFunc&& executeFunc) noexcept;

		template<typename PassData, typename SetupFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc) noexcept;

		// Add a pass only have execute function
		template<typename ExecuteFunc>
		auto* AddTrivialSideEffectPass(const char* passName, ExecuteFunc&& executeFunc) noexcept;

		void Compile() noexcept;
		void Execute(RGExecuteContext& executeContext) noexcept;
		void Retire(const DX12FencePoint& fencePoint) noexcept;

		DX12Texture* GetTexture(RGTextureId texId) noexcept;
		DX12Buffer* GetBuffer(RGBufferId bufId) noexcept;

		ResourceIndex GetResourceIndex(RGTextureId texId) noexcept;
		ResourceIndex GetResourceIndex(RGBufferId bufId) noexcept;

		DX12ViewCache* GetViewCache() const noexcept { return m_ViewCache; }
	private:
		template<typename RESOURCE>
		RGResourceId<RESOURCE> CreateInternal(const char* name, const typename RESOURCE::Descriptor& desc) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> ReadInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> WriteInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept;

		template<typename RESOURCE>
		ResourceIndex GetResourceIndexImpl(RGResourceId<RESOURCE> id) noexcept;

		template<typename RESOURCE>
		void MarkDestroyResourceIndex(ResourceIndex resIndex) noexcept;

		RGVirtualResourceBase* GetVirtualResource(RGResourceHandle handle) const noexcept;

		RGResourceNode& GetActiveResourceNode(RGResourceHandle handle) noexcept;

		RGPassNode& GetPassNode(RGPassNode::Index index) noexcept;

	private:
		template<typename RESOURCE>
		static void AccumulateUsageToVirtual(RGResourceNode& resourceNode, typename RESOURCE::Usage usage) noexcept;

	private:
		RGGpuResourceAllocator* m_GpuResourceAllocator = nullptr;
		DX12ViewCache* m_ViewCache = nullptr;

		RGArenaAllocator m_ArenaAllocator;

		std::vector<RGResourceNode> m_ResourceNodes;
		std::vector<RGPassNode> m_PassNodes;
		std::vector<RGVirtualResourceBase*> m_VirtualResouces;
		std::vector<RGResourceSlot> m_ResourceSlots;

		std::unordered_map<StringId, RGResourceHandle> m_NameHandleMap;

		std::vector<ResourceIndex> m_MarkedReleaseTextureIndices;
		std::vector<ResourceIndex> m_MarkedReleaseBufferIndices;

		friend class RGBuilder;

		template<typename RESOURCE>
		friend struct RGVirtualResource;
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
		passNode.m_NameId = StringId(passName);
		passNode.m_Pass = pass;

		RGPassNode::Index passIndex{ static_cast<uint32_t>(m_PassNodes.size()) };
		m_PassNodes.push_back(passNode);

		RGBuilder builder(*this, passIndex);
		setupFunc(builder, pass->GetData());

		return pass;
	}

	template<typename PassData, typename SetupFunc>
	inline auto* RenderGraph::AddPass(const char* passName, SetupFunc setupFunc) noexcept
	{
		using PassDataType = std::decay_t<PassData>;
		using PassType = RGPass<PassDataType>;

		GGLAB_ASSERT_MSG(passName && *passName, "Pass name must not be null.");

		auto* pass = m_ArenaAllocator.MakeTracked<PassType>();

		RGPassNode passNode = {};
		passNode.m_NameId = StringId(passName);
		passNode.m_Pass = pass;

		RGPassNode::Index passIndex{ static_cast<uint32_t>(m_PassNodes.size()) };
		m_PassNodes.push_back(passNode);

		RGBuilder builder(*this, passIndex);
		setupFunc(builder, pass->GetData());

		return pass;
	}

	template<typename ExecuteFunc>
	inline auto* RenderGraph::AddTrivialSideEffectPass(const char* passName, ExecuteFunc&& executeFunc) noexcept
	{
		struct EmptyData {};
		return AddPass<EmptyData>(passName,
			[](RGBuilder& builder, EmptyData&) { builder.SideEffect(); },
			[fn = std::forward<ExecuteFunc>(executeFunc)](DX12CommandList* commandList, EmptyData&)
			{
				fn(commandList);
			});
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::CreateInternal(const char* name, const typename RESOURCE::Descriptor& desc) noexcept
	{
		RGResourceHandle handle{ RGResourceHandle::Handle(static_cast<uint16_t>(m_ResourceSlots.size())), 1 };

		RGResourceSlot slot;
		slot.m_VirtualResourceIndex = static_cast<uint32_t>(m_VirtualResouces.size());
		slot.m_ResourceNodeIndex = static_cast<uint32_t>(m_ResourceNodes.size());
		slot.m_Version = 1;
		
		m_ResourceSlots.push_back(slot);

		RGVirtualResource<RESOURCE>* virtualResource = m_ArenaAllocator.MakeTracked<RGVirtualResource<RESOURCE>>();
		virtualResource->m_NameId = StringId(name);
		virtualResource->m_Devirtualized = false;
		virtualResource->m_Desc = desc;
		virtualResource->m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		virtualResource->m_Usage = RESOURCE::DefaultNoneUsage;
		m_VirtualResouces.push_back(virtualResource);

		RGResourceNode resourceNode
		{
			.m_ResourceHandle = handle,
			.m_VirtualResource = virtualResource,
		};
		m_ResourceNodes.push_back(resourceNode);

		// Emplace in name handle unordered map.
		m_NameHandleMap.emplace(virtualResource->m_NameId, handle);

		return RGResourceId<RESOURCE>{handle};
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::ReadInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must read valid resource.");

		const auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		GGLAB_ASSERT_MSG(slot.m_Version == resourceId.GetVersion(), "Read with stale handle.Version");

		RGResourceNode& resourceNode = m_ResourceNodes[slot.m_ResourceNodeIndex];
		RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];

		GGLAB_ASSERT_MSG(resourceNode.m_Writer != &passNode, "Pass can not read this resource and write same resource.");

		resourceNode.m_Readers.push_back(&passNode);

		AccumulateUsageToVirtual<RESOURCE>(resourceNode, usage);

		RGPassNode::Access access = {};
		access.m_ResourceNodeIndex = slot.m_ResourceNodeIndex;
		access.m_UsageBits = static_cast<uint64_t>(usage);
		access.m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		access.m_AccessType = RGPassNode::Access::Type::Read;
		passNode.m_Accesses.push_back(access);

		return resourceId;
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::WriteInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must write valid resource.");

		auto& slot = m_ResourceSlots[resourceId.GetHandle().Value()];
		RGResourceNode* curResourceNode = &m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
		RGPassNode& passNode = m_PassNodes[passNodeIndex.Value()];

		// version update
		if ((curResourceNode->m_Writer != nullptr) || (!curResourceNode->m_Readers.empty()))
		{
			++slot.m_Version;
			resourceId.m_Version = slot.m_Version;

			RGResourceNode nextResourceNode = {};
			nextResourceNode.m_ResourceHandle = resourceId;
			nextResourceNode.m_VirtualResource = curResourceNode->m_VirtualResource;
			slot.m_ResourceNodeIndex = static_cast<uint32_t>(m_ResourceNodes.size());
			m_ResourceNodes.push_back(nextResourceNode);

			curResourceNode = &m_ResourceNodes[slot.m_ResourceNodeIndex.Value()];
		}
		curResourceNode->m_Writer = &passNode;

		// Accumulate usage
		AccumulateUsageToVirtual<RESOURCE>(*curResourceNode, usage);

		// Imported resource -> pass side effect
		if (curResourceNode->m_VirtualResource && curResourceNode->m_VirtualResource->m_Imported)
		{
			passNode.m_SideEffect = true;
		}

		RGPassNode::Access access = {};
		access.m_ResourceNodeIndex = slot.m_ResourceNodeIndex;
		access.m_UsageBits = static_cast<uint64_t>(usage);
		access.m_ResourceType = RGResourceTraits<RESOURCE>::ResourceType;
		access.m_AccessType = RGPassNode::Access::Type::Write;
		passNode.m_Accesses.push_back(access);

		return resourceId;
	}

	template<typename RESOURCE>
	inline ResourceIndex RenderGraph::GetResourceIndexImpl(RGResourceId<RESOURCE> id) noexcept
	{
		if (!id.IsValid())
		{
			return {};
		}

		auto* vResourceBase = GetVirtualResource(id);
		if (!vResourceBase)
		{
			return {};
		}

		auto* vResource = static_cast<RGVirtualResource<RESOURCE>*>(vResourceBase);

		GGLAB_ASSERT_MSG(vResource->m_Devirtualized && vResource->m_GpuResourceIndex.IsValid(),
			"Resource not devirtualized yet. Check your pass' setup and usages.");

		return vResource->m_GpuResourceIndex;
	}

	template<typename RESOURCE>
	inline void RenderGraph::MarkDestroyResourceIndex(ResourceIndex resIndex) noexcept
	{
		if constexpr (std::is_same_v<RESOURCE, RGTextureResource>)
		{
			m_MarkedReleaseTextureIndices.push_back(resIndex);
		}
		else if constexpr (std::is_same_v<RESOURCE, RGBufferResource>)
		{
			m_MarkedReleaseBufferIndices.push_back(resIndex);
		}
	}

	template<typename RESOURCE>
	inline void RenderGraph::AccumulateUsageToVirtual(RGResourceNode& resourceNode, typename RESOURCE::Usage usage) noexcept
	{
		auto* virtualResource = static_cast<RGVirtualResource<RESOURCE>*>(resourceNode.m_VirtualResource);
		virtualResource->m_Usage |= usage;
	}

	// RGVirtualResource functions
	template<typename RESOURCE>
	inline void RGVirtualResource<RESOURCE>::Devirtualize(RGGpuResourceAllocator* allocator) noexcept
	{
		if (m_Devirtualized)
		{
			return;
		}

		m_CurrentStates = RGResourceTraits<RESOURCE>::ToState(static_cast<RGResourceTraits<RESOURCE>::Bits>(m_Usage));
		std::optional<D3D12_CLEAR_VALUE> clearValue = DefaultClearValue<Desc>(m_Desc);
		m_GpuResourceIndex = allocator->Acquire<Desc>(m_Desc, m_CurrentStates, clearValue);
		m_Devirtualized = true;
	}

	template<typename RESOURCE>
	inline void RGVirtualResource<RESOURCE>::Destroy(RenderGraph& rg) noexcept
	{
		if (!m_Devirtualized)
		{
			return;
		}
		rg.MarkDestroyResourceIndex<RESOURCE>(m_GpuResourceIndex);
		m_GpuResourceIndex.Reset();
		m_Devirtualized = false;
	}
}