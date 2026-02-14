#pragma once
#include "StringId.h"
#include "DevelopGuiStateStore.h"

namespace gglab
{
	struct DevelopGuiContext;
	using DevelopGuiDrawFunc = void(*)(DevelopGuiContext&);

	struct DevelopGuiPanelDesc
	{
		const char* m_Path = nullptr;
		const char* m_Title = nullptr;

		int32_t m_Order = 0;
		bool m_DefaultOpen = false;

		DevelopGuiDrawFunc m_DrawFunc = nullptr;
	};

	class DevelopGuiRegistry
	{
	private:
		struct PanelRuntime
		{
			std::string m_FullPath;
			std::string_view m_Leaf{};
			std::string m_TitleStorage;
			std::string_view m_TitleText{};

			int32_t m_Order = 0;
			bool m_Open = false;

			DevelopGuiDrawFunc m_DrawFunc = nullptr;

			uint64_t m_Key = 0;
			std::string m_ImGuiMenuLabel;
			std::string m_ImGuiWindowTitle;
		};

		struct MenuNode
		{
			StringID m_Id{};
			std::string m_Name;

			std::unordered_map<StringID, std::unique_ptr<MenuNode>> m_ChildrenMap;
			std::vector<MenuNode*> m_ChildrenSorted;
			std::vector<uint32_t> m_PanelIndices;
		};

		class PathViewIterator
		{
		public:
			explicit PathViewIterator(std::string_view path) noexcept : m_Rest(path) {}
			bool Next(std::string_view& outSegment) noexcept;

		private:
			std::string_view m_Rest{};
		};

	public:
		DevelopGuiRegistry() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiRegistry);
		~DevelopGuiRegistry() = default;

		void RegisterPanel(const DevelopGuiPanelDesc& desc) noexcept;
		void BuildMenuTree() noexcept;

		void DrawMenuBar() noexcept;
		void DrawPanels(DevelopGuiContext& context) noexcept;

		void ClearPanels() noexcept;
		void ClearStateStore() noexcept;
		void Reset() noexcept;

	private:
		MenuNode* FindOrCreateChildMenuNode(MenuNode& parent, std::string_view name) noexcept;

		void SortTreeNodes(MenuNode& node) noexcept;
		void DrawMenuNode(MenuNode& node) noexcept;

		static uint64_t MakePanelKey(std::string_view fullPath,
			std::string_view titleForKey,
			DevelopGuiDrawFunc drawFunc,
			const std::vector<PanelRuntime>& existingPanels) noexcept;
		static std::string MakeImGuiLabeledId(std::string_view display, uint64_t key) noexcept;

		static constexpr std::string_view DefaultLeaf = "Panel";
	private:
		DevelopGuiStateStore m_StateStore;

		std::vector<PanelRuntime> m_Panels;
		MenuNode m_Root{};

		bool m_IsBuilt = false;
	};
}