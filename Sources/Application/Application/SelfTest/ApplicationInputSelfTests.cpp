#include "Application/SelfTest/ApplicationInputSelfTests.h"

#include "Application/Demo/DemoTypes.h"
#include "Application/Input/ApplicationCameraInput.h"
#include "ApplicationInput.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/WindowsInputMapping.h"

#include <array>

namespace gglab
{
	void RunApplicationInputSelfTests(SelfTestContext& context) noexcept
	{
		const std::span<const WindowsKeyMapping> mappings = GetWindowsKeyMappings();
		std::array<bool, AppInputKeyCount> mappedKeys{};
		bool mappingTableIsUnique = mappings.size() == AppInputKeyCount;
		for (const WindowsKeyMapping& mapping : mappings)
		{
			const size_t index = static_cast<size_t>(mapping.m_Key);
			if (index >= mappedKeys.size() || mappedKeys[index])
			{
				mappingTableIsUnique = false;
				continue;
			}
			mappedKeys[index] = true;
		}
		context.Check(mappingTableIsUnique && MapWindowsVirtualKey('W') == AppInputKey::W &&
			MapWindowsVirtualKey(0x1b) == AppInputKey::Escape &&
			MapWindowsVirtualKey(0xffff) == AppInputKey::Count,
			"Windows virtual keys map explicitly and uniquely to the neutral key contract");

		context.Check(!IsValidKeyCodeValue(-1) && IsValidKeyCodeValue(0) &&
			IsValidKeyCodeValue(0xfe) && !IsValidKeyCodeValue(0xff) &&
			!IsValidKeyCodeValue(0x100),
			"Windows keyboard storage rejects negative and out-of-range virtual keys");

		ApplicationInput input;
		ApplicationInputSnapshot snapshot{};
		snapshot.m_KeysHeld[static_cast<size_t>(AppInputKey::W)] = true;
		snapshot.m_KeysHeld[static_cast<size_t>(AppInputKey::LeftShift)] = true;
		snapshot.m_PointerDelta = { 3.0f, -4.0f };
		input.Publish(snapshot);
		const CameraInput cameraInput = BuildCameraInput(input);
		context.Check(cameraInput.m_Front && cameraInput.m_Accelerate &&
			cameraInput.m_IsMouseRelative && cameraInput.m_MouseDelta.m_X == 3.0f &&
			cameraInput.m_MouseDelta.m_Y == -4.0f,
			"Camera input is derived once from the neutral application state");

		DemoServices services{};
		services.m_Renderer = reinterpret_cast<Renderer*>(1);
		services.m_AssetManager = reinterpret_cast<AssetManager*>(1);
		services.m_ShaderManager = reinterpret_cast<ShaderManager*>(1);
		services.m_TaskSystem = reinterpret_cast<TaskSystem*>(1);
		services.m_Input = &input;
		services.m_Time = reinterpret_cast<Time*>(1);
		services.m_DebugDraw = reinterpret_cast<DebugDrawContext*>(1);
		services.m_EnvironmentAssetController =
			reinterpret_cast<EnvironmentAssetController*>(1);
		context.Check(services.IsValid(),
			"DemoServices accepts the neutral application input contract");
		services.m_Input = nullptr;
		context.Check(!services.IsValid(),
			"DemoServices still rejects a missing required input service");
	}
}
