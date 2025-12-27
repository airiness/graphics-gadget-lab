#pragma once
namespace gglab
{
	class DX12CommandList;
	class RGBuilder;
	class RGPassBase
	{
	public:
		virtual ~RGPassBase() noexcept = default;

		virtual void Execute(DX12CommandList*) noexcept = 0;
	};

	template <typename PassData>
	class RGPass : public RGPassBase
	{
	public:
		PassData& GetData() noexcept { return m_PassData; }
		const PassData& GetData() const noexcept { return m_PassData; }

	protected:
		PassData m_PassData;
	};

	template <typename PassData, typename ExecuteFunc>
	class RGPassConcrete : public RGPass<PassData>
	{
	public:
		explicit RGPassConcrete(ExecuteFunc&& execute) noexcept :
			m_Execute(std::move(execute))
		{
		}

		void Execute(DX12CommandList* commandList) noexcept override final
		{
			m_Execute(commandList, this->m_PassData);
		}
	private:
		static_assert(std::is_invocable_v<ExecuteFunc, DX12CommandList*, PassData&>,
			"ExecuteFunc must be callable as RenderGraph Pass Executor.");

		ExecuteFunc m_Execute;
	};
}