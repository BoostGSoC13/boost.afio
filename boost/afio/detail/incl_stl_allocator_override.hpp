	// Override for all STL uses of the type
	template<> class allocator<BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE> : public boost::afio::detail::aligned_allocator<BOOST_AFIO_TYPE_TO_BE_OVERRIDEN_FOR_STL_ALLOCATOR_USAGE>
	{
	public:
		allocator() BOOST_NOEXCEPT_OR_NOTHROW
		{}

		template <class U> allocator(const allocator<U>&) BOOST_NOEXCEPT_OR_NOTHROW
		{}
	};
