#include "Precompiled.h"
#include "DevelopGuiRegistry.h"
#include "DevelopGuiContext.h"
#include "StringUtils.h"

namespace gglab
{
	bool DevelopGuiRegistry::PathViewIterator::Next(std::string_view& outSegment) noexcept
	{
		while (!m_Rest.empty() && m_Rest.front() == '/')
		{
			// remove '/' from end
			m_Rest.remove_suffix(1);
		}

		if (m_Rest.empty())
		{
			return false;
		}

		const size_t pos = m_Rest.find('/');
		if (pos == std::string_view::npos)
		{
			outSegment = m_Rest;
			m_Rest = {};
		}
		else
		{
			outSegment = m_Rest.substr(0, pos);
			m_Rest = m_Rest.substr(pos + 1);
		}

		if (outSegment.empty())
		{
			return Next(outSegment);
		}

		return true;
	}

	void DevelopGuiRegistry::RegisterPanel(const DevelopGuiPanelDesc& desc) noexcept
	{
		PanelRuntime runtime{};
		runtime.m_Open = desc.m_DefaultOpen;
		runtime.m_Order = desc.m_Order;
		runtime.m_DrawFunc = desc.m_DrawFunc;

		runtime.m_FullPath = desc.m_Path ? desc.m_Path : "";
		const std::string_view fullPathView = runtime.m_FullPath;

		std::string_view titleForKey{};
		if (desc.m_Title && desc.m_Title[0] != '\0')
		{
			runtime.m_TitleStorage = desc.m_Title;
			//runtime.m_TitleText = runtime.m_TitleStorage;
			titleForKey = runtime.m_TitleText;
		}
		else
		{
			titleForKey = utils::FindLeaf(fullPathView);
			if (titleForKey.empty())
			{
				titleForKey = DefaultLeaf;
			}
		}
		runtime.m_Key = MakePanelKey(fullPathView, titleForKey, runtime.m_DrawFunc, m_Panels);

		m_Panels.push_back(std::move(runtime));
		m_IsBuilt = false;
	}

	void DevelopGuiRegistry::BuildMenuTree() noexcept
	{
		m_Root = MenuNode{};
		m_Root.m_Name.clear();

		// Leaf, Title, ImGui labels
		for (uint32_t index = 0; index < static_cast<uint32_t>(m_Panels.size()); ++index)
		{
			auto& panel = m_Panels[index];
			const std::string_view pathView(panel.m_FullPath);

			panel.m_Leaf = utils::FindLeaf(pathView);
			if (panel.m_Leaf.empty())
			{
				panel.m_Leaf = DefaultLeaf;
			}

			if (!panel.m_TitleStorage.empty())
			{
				panel.m_TitleText = panel.m_TitleStorage;
			}
			else
			{
				panel.m_TitleText = panel.m_Leaf;
			}

			panel.m_ImGuiMenuLabel = MakeImGuiLabeledId(panel.m_Leaf, panel.m_Key);
			panel.m_ImGuiWindowTitle = MakeImGuiLabeledId(panel.m_TitleText, panel.m_Key);
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(m_Panels.size()); ++index)
		{
			const auto& panel = m_Panels[index];

			PathViewIterator pathViewIt(std::string_view(panel.m_FullPath));
			std::string_view seg{};

			std::string_view prev{};
			bool hasPrev = false;

			MenuNode* node = &m_Root;

			while (pathViewIt.Next(seg))
			{
				if (!hasPrev)
				{
					prev = seg;
					hasPrev = true;
					continue;
				}

				node = FindOrCreateChildMenuNode(*node, prev);
				prev = seg;
			}

			if (!hasPrev)
			{
				m_Root.m_PanelIndices.push_back(index);
				continue;
			}

			node->m_PanelIndices.push_back(index);
		}

		SortTreeNodes(m_Root);

		m_IsBuilt = true;
	}

	void DevelopGuiRegistry::DrawMenuBar() noexcept
	{
		if (!m_IsBuilt)
		{
			BuildMenuTree();
		}

		if (!ImGui::BeginMainMenuBar())
		{
			return;
		}

		DrawMenuNode(m_Root);
		ImGui::EndMainMenuBar();
	}

	void DevelopGuiRegistry::DrawPanels(DevelopGuiContext& context) noexcept
	{
		auto* prevStore = context.m_StateStore;
		const uint64_t prevKey = context.m_CurrentPanelKey;

		context.m_StateStore = &m_StateStore;

		for (auto& panel : m_Panels)
		{
			if (!panel.m_Open)
			{
				continue;
			}

			context.m_CurrentPanelKey = panel.m_Key;

			if (ImGui::Begin(panel.m_ImGuiWindowTitle.c_str(), &panel.m_Open))
			{
				if (panel.m_DrawFunc)
				{
					panel.m_DrawFunc(context);
				}
				else
				{
					ImGui::TextUnformatted("Panel has no DrawFunc.");
				}
			}
			ImGui::End();
		}

		context.m_CurrentPanelKey = prevKey;
		context.m_StateStore = prevStore;
	}

	void DevelopGuiRegistry::ClearPanels() noexcept
	{
		m_Panels.clear();
		m_Root = {};
		m_IsBuilt = false;
	}

	void DevelopGuiRegistry::ClearStateStore() noexcept
	{
		m_StateStore.Clear();
	}

	void DevelopGuiRegistry::Reset() noexcept
	{
		ClearPanels();
		ClearStateStore();
	}

	DevelopGuiRegistry::MenuNode* DevelopGuiRegistry::FindOrCreateChildMenuNode(
		MenuNode& parent, std::string_view name) noexcept
	{
		const StringID id(name);

		if (auto iterator = parent.m_ChildrenMap.find(id);
			iterator != parent.m_ChildrenMap.end())
		{
			GGLAB_ASSERT(iterator->second->m_Name == name);
			return iterator->second.get();
		}

		auto node = std::make_unique<MenuNode>();
		node->m_Id = id;
		node->m_Name.assign(name.data(), name.size());

		auto [insertIt, result] = parent.m_ChildrenMap.emplace(id, std::move(node));
		return insertIt->second.get();
	}

	void DevelopGuiRegistry::SortTreeNodes(MenuNode& node) noexcept
	{
		node.m_ChildrenSorted.clear();
		node.m_ChildrenSorted.reserve(node.m_ChildrenMap.size());
		for (auto& kv : node.m_ChildrenMap)
		{
			node.m_ChildrenSorted.push_back(kv.second.get());
		}

		// Sort by MenuNode name
		std::sort(node.m_ChildrenSorted.begin(), node.m_ChildrenSorted.end(),
			[](const MenuNode* a, const MenuNode* b)
			{
				return a->m_Name < b->m_Name;
			});

		// Sort panel indices
		std::sort(node.m_PanelIndices.begin(), node.m_PanelIndices.end(),
			[this](uint32_t indexA, uint32_t indexB)
			{
				const auto& panelA = m_Panels[indexA];
				const auto& panelB = m_Panels[indexB];

				if (panelA.m_Order != panelB.m_Order)
				{
					return panelA.m_Order < panelB.m_Order;
				}

				return panelA.m_Leaf < panelB.m_Leaf;
			});

		for (auto* child : node.m_ChildrenSorted)
		{
			SortTreeNodes(*child);
		}
	}

	void DevelopGuiRegistry::DrawMenuNode(MenuNode& node) noexcept
	{
		for (auto* child : node.m_ChildrenSorted)
		{
			if (ImGui::BeginMenu(child->m_Name.c_str()))
			{
				DrawMenuNode(*child);
				ImGui::EndMenu();
			}
		}

		for (auto panelIndex : node.m_PanelIndices)
		{
			auto& panel = m_Panels[panelIndex];
			ImGui::MenuItem(panel.m_ImGuiMenuLabel.c_str(), nullptr, &panel.m_Open);
		}
	}

	uint64_t DevelopGuiRegistry::MakePanelKey(std::string_view fullPath,
		std::string_view titleForKey,
		DevelopGuiDrawFunc drawFunc,
		const std::vector<PanelRuntime>& existingPanels) noexcept
	{
		// "path|title"
		std::string baseInput;
		baseInput.reserve(fullPath.size() + 1 + titleForKey.size());
		baseInput.append(fullPath);
		baseInput.push_back('|');
		baseInput.append(titleForKey);

		auto existsKey = [&existingPanels](uint64_t key) noexcept
			{
				for (const auto& panel : existingPanels)
				{
					if (panel.m_Key == key)
					{
						return true;
					}
				}
				return false;
			};

		uint64_t key = Crc64(baseInput);
		if (!existsKey(key))
		{
			return key;
		}

		// Collision process, use drawFunc and dupIndex to expand the key
		uint32_t dupIndex = 0;
		while (true)
		{
			std::string dis = baseInput;
			dis.push_back('|');

			std::format_to(std::back_inserter(dis), "{:016X}", static_cast<uint64_t>(reinterpret_cast<uintptr_t>(drawFunc)));

			dis.push_back('|');
			std::format_to(std::back_inserter(dis), "{}", dupIndex);

			key = Crc64(dis);

			if (!existsKey(key))
			{
				return key;
			}

			++dupIndex;
		}
	}

	std::string DevelopGuiRegistry::MakeImGuiLabeledId(std::string_view display, uint64_t key) noexcept
	{
		std::string labeledId;

		constexpr size_t MakerLength = 2;
		constexpr size_t KeyLength = 16;

		labeledId.reserve(display.size() + MakerLength + KeyLength);
		labeledId.append(display);
		std::format_to(std::back_inserter(labeledId), "##{:016X}", key);

		return labeledId;
	}
}