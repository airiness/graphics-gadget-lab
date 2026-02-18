#pragma once
namespace gglab
{
	struct RGExecuteContext;
	class RGBuilder;
	class RGPassBase
	{
	public:
		virtual ~RGPassBase() noexcept = default;

		virtual void Execute(RGExecuteContext&) noexcept = 0;
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

		void Execute(RGExecuteContext& executeContext) noexcept override final
		{
			m_Execute(executeContext, this->m_PassData);
		}
	private:
		static_assert(std::is_invocable_v<ExecuteFunc, RGExecuteContext&, PassData&>,
			"ExecuteFunc must be callable as RenderGraph Pass Executor.");

		ExecuteFunc m_Execute;
	};
}