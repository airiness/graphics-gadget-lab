#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"

#include <cstdint>
#include <imgui.h>

namespace gglab::devtools
{
	void DrawVector3Text(const char* label, const Vector3& value) noexcept
	{
		ImGui::Text("%s: (%.4f, %.4f, %.4f)", label, value.m_X, value.m_Y, value.m_Z);
	}

	void DrawMatrix4x4Tree(const char* label, const Matrix& matrix) noexcept
	{
		if (!ImGui::TreeNode(label))
		{
			return;
		}

		ImGui::PushID(label);
		if (ImGui::BeginTable("Matrix", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
		{
			const float rows[4][4] = {
				{ matrix.m_11, matrix.m_12, matrix.m_13, matrix.m_14 },
				{ matrix.m_21, matrix.m_22, matrix.m_23, matrix.m_24 },
				{ matrix.m_31, matrix.m_32, matrix.m_33, matrix.m_34 },
				{ matrix.m_41, matrix.m_42, matrix.m_43, matrix.m_44 },
			};
			for (const auto& row : rows)
			{
				ImGui::TableNextRow();
				for (uint32_t column = 0; column < 4; ++column)
				{
					ImGui::TableSetColumnIndex(static_cast<int>(column));
					ImGui::Text("% .5f", row[column]);
				}
			}
			ImGui::EndTable();
		}
		ImGui::PopID();

		ImGui::TreePop();
	}
}
