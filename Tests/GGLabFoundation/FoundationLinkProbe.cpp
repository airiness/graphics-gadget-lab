namespace gglab::foundation::detail
{
	[[nodiscard]] bool FoundationLinkAnchor() noexcept;
}

int main()
{
	return gglab::foundation::detail::FoundationLinkAnchor() ? 0 : 1;
}
