#pragma once
#include "RGArenaAllocator.h"
#include "GpuResourceRegistry.h"
#include "RGResource.h"
#include "RGPass.h"
#include "StringId.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class RGPassBase;
	template<typename PassData> class RGPass;
	template<typename PassData, typename ExecuteFunc> class RGPassConcrete;

	struct RGPassNode;
	struct RGResourceNode;

	struct RGVirtualResourceBase
	{
		virtual ~RGVirtualResourceBase() = default;
		virtual void Devirtualize(GpuResourceRegistry& gpuResourceRegistry) noexcept = 0;
		virtual void Destroy(GpuResourceRegistry& gpuResourceRegistry) noexcept = 0;

		StringId m_NameId;
		bool m_Imported = false;
		bool m_Devirtualized = false;
		int32_t m_RefCount = 0;

		RGPassNode* m_FirstUser = nullptr;
		RGPassNode* m_LastUser = nullptr;

		D3D12_RESOURCE_STATES m_CurrentStates = D3D12_RESOURCE_STATE_COMMON;

		using Index = int32_t;
		static constexpr Index InvalidIndex = static_cast<Index>(-1);
		using GpuResourceIndex = int32_t;
		static constexpr GpuResourceIndex InvalidGpuResourceIndex = static_cast<GpuResourceIndex>(-1);
	};

	template<typename RESOURCE>
	struct RGVirtualResource : RGVirtualResourceBase
	{
		using Desc = RESOURCE::Descriptor;
		using SubresourceDesc = RESOURCE::SubresourceDescriptor;
		using Usage = RESOURCE::Usage;

		Desc m_Desc = {};
		SubresourceDesc m_SubresourceDesc = {};
		Usage m_Usage = RESOURCE::DefaultReadUsage;
		GpuResourceIndex m_GpuResourceIndex = InvalidGpuResourceIndex;

		void Devirtualize(GpuResourceRegistry& gpuResourceRegistry) noexcept override
		{
			if (m_Devirtualized)
			{
				return;
			}
			m_CurrentStates = ToD3D12ResourceStates<Usage>(m_Usage);
			m_GpuResourceIndex = gpuResourceRegistry.AcquireResource<Desc>(m_Desc);
			m_Devirtualized = true;
		}

		void Destroy(GpuResourceRegistry& gpuResourceRegistry) noexcept override
		{
			if (!m_Devirtualized)
			{
				return;
			}
			gpuResourceRegistry.ReleaseResource<Desc>(m_GpuResourceIndex);
			m_GpuResourceIndex = InvalidGpuResourceIndex;
			m_Devirtualized = false;
		}
	};

	struct RGResourceNode
	{
		using Index = int32_t;
		static constexpr Index InvalidIndex = static_cast<Index>(-1);

		RGResourceHandle m_ResourceHandle;

		RGVirtualResourceBase* m_VirtualResource = nullptr;

		RGPassNode* m_Writer = nullptr;
		std::vector<RGPassNode*> m_Readers;

		// TODO: can be template?
		RGTextureUsage m_TextureUsage = RGTextureUsage::None;
		RGBufferUsage m_BufferUsage = RGBufferUsage::None;

		StringId NameId() const noexcept
		{
			return m_VirtualResource ? m_VirtualResource->m_NameId : StringId();
		}
	};

	struct RGPassNode
	{
		using Index = int32_t;
		static constexpr Index InvalidIndex = static_cast<Index>(-1);

		StringId m_NameId;
		bool m_SideEffect = false;
		bool m_Culled = false;

		std::vector<int32_t> m_ReadResources;
		std::vector<int32_t> m_WriteResources;

		std::vector<RGVirtualResourceBase*> m_DevirtualizeVirtualResources;
		std::vector<RGVirtualResourceBase*> m_DestroyVirtualResources;

		RGPassBase* m_Pass = nullptr;
	};

	struct RGResourceSlot
	{
		RGVirtualResourceBase::Index m_VirtualResourceId = RGVirtualResourceBase::InvalidIndex;
		RGResourceNode::Index m_ResourceNodeId = RGResourceNode::InvalidIndex;
		RGResourceHandle::Version m_Version = RGResourceHandle::UnintializedVersion;
	};

	class RenderGraph
	{
	public:
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

		private:
			RenderGraph& m_RG;
			RGPassNode::Index m_PassNodeIndex = RGPassNode::InvalidIndex;
		};

	public:
		explicit RenderGraph(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderGraph);
		~RenderGraph() noexcept;

		template<typename PassData, typename SetupFunc, typename ExecuteFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc, ExecuteFunc&& executeFunc) noexcept;

		template<typename PassData, typename SetupFunc>
		auto* AddPass(const char* passName, SetupFunc setupFunc) noexcept;

		void Compile() noexcept;
		void Execute() noexcept;

	private:
		template<typename RESOURCE>
		RGResourceId<RESOURCE> CreateInternal(const char* name, const typename RESOURCE::Descriptor& desc) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> ReadInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept;

		template<typename RESOURCE>
		RGResourceId<RESOURCE> WriteInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		GpuResourceRegistry m_GpuResourceRegistry;
		RGArenaAllocator m_ArenaAllocator;

		std::vector<RGResourceNode> m_ResourceNodes;
		std::vector<RGPassNode> m_PassNodes;
		std::vector<RGVirtualResourceBase*> m_VirtualResouces;
		std::vector<RGResourceSlot> m_ResourceSlots;

		std::unordered_map<StringId, RGResourceHandle> m_NameHandleMap;

		friend class RGBuilder;
	};

	template<typename PassData, typename SetupFunc, typename ExecuteFunc>
	inline auto* RenderGraph::AddPass(const char* passName, SetupFunc setupFunc, ExecuteFunc&& executeFunc) noexcept
	{
		using PassDataType = std::decay_t<PassData>;
		using ExecuteFuncType = std::decay_t<ExecuteFunc>;
		using PassType = RGPassConcrete<PassDataType, ExecuteFuncType>;

		GGLAB_ASSERT_MSG(passName != "", "Pass name must not be null.");

		auto* pass = m_ArenaAllocator.MakeTracked<PassType>(std::forward<ExecuteFunc>(executeFunc));

		RGPassNode passNode = {};
		passNode.m_NameId = StringId(passName);
		passNode.m_Pass = pass;

		uint32_t passIndex = static_cast<uint32_t>(m_PassNodes.size());
		m_PassNodes.push_back(passNode);

		RGBuilder builder(*this, passIndex);
		setupFunc(builder, pass->GetData());

		return pass;
	}

	template<typename PassData, typename SetupFunc>
	inline auto* RenderGraph::AddPass(const char* passName, SetupFunc setupFunc) noexcept
	{
		using PassDataType = std::decay_t<PassData>;
		using PassType = RGPassConcrete<PassDataType>;

		GGLAB_ASSERT_MSG(passName != "", "Pass name must not be null.");

		auto* pass = m_ArenaAllocator.MakeTracked<PassType>();

		RGPassNode passNode = {};
		passNode.m_NameId = StringId(passName);
		passNode.m_Pass = pass;

		uint32_t passIndex = static_cast<uint32_t>(m_PassNodes.size());
		m_PassNodes.push_back(passNode);

		RGBuilder builder(*this, passIndex);
		setupFunc(builder, PassData());

		return pass;
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::CreateInternal(const char* name, const typename RESOURCE::Descriptor& desc) noexcept
	{
		RGResourceHandle handle
		{
			static_cast<RGResourceHandle::Handle>(m_ResourceSlots.size()),
			static_cast<RGResourceHandle::Version>(1)
		};

		RGResourceSlot slot
		{
			.m_VirtualResourceId = static_cast<RGVirtualResourceBase::Index>(m_VirtualResouces.size()),
			.m_ResourceNodeId = static_cast<RGResourceNode::Index>(m_ResourceNodes.size()),
			.m_Version = static_cast<RGResourceHandle::Version>(1)
		};
		m_ResourceSlots.push_back(slot);

		RGVirtualResource<RESOURCE>* virtualResource = m_ArenaAllocator.MakeTracked<RGVirtualResource<RESOURCE>>();
		virtualResource->m_NameId = StringId(name);
		virtualResource->m_Devirtualized = false;
		virtualResource->m_Desc = desc;
		virtualResource->m_Usage = RESOURCE::DefaultNoneUsage;
		m_VirtualResouces.push_back(virtualResource);

		RGResourceNode resourceNode
		{
			.m_ResourceHandle = handle,
			.m_VirtualResource = virtualResource,
		};
		m_ResourceNodes.push_back(resourceNode);

		return RGResourceId<RESOURCE>{handle};
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::ReadInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept
	{
		GGLAB_ASSERT_MSG(resourceId.IsValid(), "Must read valid resource.");

		return RGResourceId<RESOURCE>();
	}

	template<typename RESOURCE>
	inline RGResourceId<RESOURCE> RenderGraph::WriteInternal(RGPassNode::Index passNodeIndex, RGResourceId<RESOURCE> resourceId, typename RESOURCE::Usage usage) noexcept
	{
		return RGResourceId<RESOURCE>();
	}
}