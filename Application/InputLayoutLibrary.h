#pragma once

namespace gglab
{
	enum class BuiltinInputLayout : uint32_t
	{
		P3,				// Position(3)
		P3T2,			// Position(3), TexCoord(2)
		P3N3,			// Position(3), Normal(3)
		P3N3T2,			// Position(3), Normal(3), TexCoord(2)

		Count
	};

	struct InputElement
	{
		const char* m_SemanticName;
		uint32_t m_SemanticIndex;
		DXGI_FORMAT m_Format;
		uint32_t m_InputSlot;
		uint32_t m_AlignedByteOffset;
		D3D12_INPUT_CLASSIFICATION m_InputSlotClass;
		uint32_t m_InstanceDataStepRate;
	};

	struct InputLayoutView
	{
		D3D12_INPUT_LAYOUT_DESC m_Desc{};

		operator const D3D12_INPUT_LAYOUT_DESC& () const noexcept { return m_Desc; }
	};

	class InputLayoutLibrary
	{
	public:
		static InputLayoutLibrary& GetInstance() noexcept;

		void IntializeBuiltinLayouts() noexcept;

		InputLayoutView Get(BuiltinInputLayout layoutId) const noexcept;
		std::optional<InputLayoutView> Get(std::string_view key) const noexcept;

		void Register(std::string_view key, std::initializer_list<InputElement> inputElements) noexcept;
		InputLayoutView RegisterAndGet(std::string_view key, std::initializer_list<InputElement> inputElements) noexcept;

	private:
		InputLayoutLibrary() noexcept = default;

	private:
		struct LayoutStorage
		{
			std::vector<std::unique_ptr<std::string>> m_SemanticNamePool;
			std::unique_ptr<D3D12_INPUT_ELEMENT_DESC[]> m_Elements;
			uint32_t m_Count = 0;
		};

		std::unordered_map<BuiltinInputLayout, std::vector<InputElement>> m_InputLayoutMap;
	};
}