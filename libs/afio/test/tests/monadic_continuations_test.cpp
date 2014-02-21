#include "test_functions.hpp"
#include "boost/function_types/result_type.hpp"
using namespace boost::afio;

namespace continuations
{
	namespace detail
	{
		// Implement C++14 get<type>(tuple)
		template<int Index, class Search, class First, class... Types>
		struct get_internal
		{
			typedef typename get_internal<Index + 1, Search, Types...>::type type;
			static BOOST_CONSTEXPR_OR_CONST int index = Index;
		};
		template<int Index, class Search, class... Types>
		struct get_internal<Index, Search, Search, Types...>
		{
			typedef get_internal type;
			static BOOST_CONSTEXPR_OR_CONST int index = Index;
		};
		template<int Index, class Search, class T>
		struct get_internal<Index, Search, T>
		{
			typedef get_internal type;
			static BOOST_CONSTEXPR_OR_CONST int index = std::is_same<Search, T>::value ? Index : -1;
		};

		template<class T, class... Types>
		T get(std::pair<Types...> pair)
		{
			return std::get<get_internal<0, T, Types...>::type::index>(pair);
		}
		template<class T, class... Types>
		T get(std::tuple<Types...> tuple)
		{
			return std::get<get_internal<0, T, Types...>::type::index>(tuple);
		}
		template<class T> T get(T &&v)
		{
			return std::forward<T>(v);
		}

		// True if variadic typelist contains type T
		template<class T, class... Types> struct contains_type
		{
			static BOOST_CONSTEXPR_OR_CONST bool value=(-1!=get_internal<0, T, Types...>::type::index);
		};

		// Extract the return type of any callable incl lambdas
		template<class F> struct return_type
		{
			typedef typename boost::function_types::result_type<decltype(&F::operator())>::type type;
		};
	}
	template<class T> struct thenable;
	// Implements .then()
	template<class return_type, class continuation_type, class callable> struct thenable_impl
	{
		// Implements .then()
		thenable<return_type> operator()(continuation_type &prev, callable &&f)
		{
			static_assert(!std::is_same<callable, callable>::value, "thenable<T> not enabled for type T")
		}
	};
	// Wrapper for a type providing a continuation .then()
	template<class T> struct thenable : public T
	{
		// Perfect forwarding constructor
#ifndef _MSC_VER
		using T::T;
#else
		template<class... Args> thenable(Args &&... args) : T(std::forward<Args>(args)...) { }
#endif
		// Continues calling f from value
		template<class F> typename std::result_of<thenable_impl<typename detail::return_type<F>::type, T, F>(T&, F&&)>::type then(F &&f)
		{
			return thenable_impl<typename detail::return_type<F>::type, T, F>()(*this, std::forward<F>(f));
		}
	};
	// Convenience
	template<class T> thenable<T> make_thenable(T &&v)
	{
		return thenable<T>(std::forward<T>(v));
	}

	// Overload tag type to invoke the right kind of close() overload etc
	static async_io_op h;
	template<class return_type> struct async_io_op_tag
	{
		std::function<return_type(async_file_io_dispatcher_base *, async_io_op)> f;
		async_io_op_tag(std::function<return_type(async_file_io_dispatcher_base *, async_io_op)> _f) : f(std::move(_f)) { }
		return_type operator()() { } // Purely for return type deduction
	};
	// Implements .then(async_io_op_tag) for the tagged dispatch
	template<class return_type> struct thenable_impl<return_type, async_io_op, async_io_op_tag<return_type>>
	{
		thenable<return_type> operator()(async_io_op &prev, async_io_op_tag<return_type> &&f)
		{
			return f.f(/*this*/prev.parent, /*h*/prev);
		}
	};
	inline async_io_op_tag<async_io_op> completion(const async_io_op &req, const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> callback)
	{
		using std::placeholders::_1;
		using std::placeholders::_2;
		return async_io_op_tag<async_io_op>(std::bind(
			(async_io_op (async_file_io_dispatcher_base::*)(
				const async_io_op &,
				const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> &))
			&async_file_io_dispatcher_base::completion,
			/*this*/_1, /*h*/_2,
			callback));
	}
#if 0
		Unknown,
		UserCompletion,
		dir,
		rmdir,
		file,
		rmfile,
		symlink,
		rmsymlink,
		sync,
		close,
		read,
		write,
		truncate,
		barrier,
		enumerate,
		adopt,
#endif

	// Implements async_io_op.then(callable(async_io_op, completion_t)) for lambdas etc
	template<class _> struct thenable_impl<async_file_io_dispatcher_base::completion_returntype, async_io_op, /*catch lambdas*/_>
	{
		thenable<async_io_op> operator()(async_io_op &prev, async_file_io_dispatcher_base::completion_t f)
		{
			return prev.parent->completion(prev, std::make_pair(async_op_flags::none, std::function<async_file_io_dispatcher_base::completion_t>(std::move(f))));
		}
	};
#if 0
	// Implements async_io_op.then(callable()) for lambdas etc
	template<class return_type, class callable> struct thenable_impl<return_type, async_io_op, callable>
	{
		thenable<return_type> operator()(async_io_op &prev, callable &&f)
		{
			return prev.parent->call(prev, std::forward<callable>(f));
		}
	};
#endif
}

BOOST_AFIO_AUTO_TEST_CASE(monadic_continuations_test, "Tests that the monadic continuations framework works as expected", 10)
{
	auto dispatcher=make_async_file_io_dispatcher();

	try
	{
		using namespace continuations;
		auto openedfile(make_thenable(dispatcher->file(async_path_op_req("example_file.txt", file_flags::ReadWrite))));
		auto _dis(openedfile.parent); // test decay to underlying type
		// Test with lambda with completion spec
		openedfile.then([](size_t id, async_io_op op) -> std::pair<bool, std::shared_ptr<async_io_handle>> { return std::make_pair(true, op.get()); });
		// Test with tagged function
		openedfile.then(completion(h, std::make_pair(
			async_op_flags::none,
			std::function<async_file_io_dispatcher_base::completion_t>(
				[](size_t id, async_io_op op) -> std::pair<bool, std::shared_ptr<async_io_handle>> {
					return std::make_pair(true, op.get());
				}))));
	}
	catch(...)
	{
		std::cerr << boost::current_exception_diagnostic_information(true) << std::endl;
		throw;
	}
#if 0
	// ************ Continuations code, see original code this expands into at bottom ************
	try
	{
		using namespace continuations;
		std::array<char, 12> buffer;
		// This line monkey patches in the continuations support. Later AFIO will have dispatcher
		// always return a thenable<T>, thus making this line redundant.
		auto openedfile(make_thenable(dispatcher->file(async_path_op_req("example_file.txt", file_flags::ReadWrite))));
		when_all(openedfile
			.then(truncate(h, 12))
			.then(write(h, { "He", "ll", "o ", "Wo", "rl", "d\n" }, 0))
			.then(sync(h))
			.then(read(h, buffer, 0))
			.then(close(h))
			.then(rmfile(h, "example_file.txt"))
			).wait();
	}
	catch(...)
	{
		std::cerr << boost::current_exception_diagnostic_information(true) << std::endl;
		throw;
	}
#endif

	// ************ Original code ************
	try
	{
		// Schedule an opening of a file called example_file.txt
		async_path_op_req req("example_file.txt", file_flags::ReadWrite);
		async_io_op openfile(dispatcher->file(req)); /*< schedules open file as soon as possible >*/

		// Something a bit surprising for many people is that writing off
		// the end of a file in AFIO does NOT extend the file and writes
		// which go past the end will simply fail instead. Why not?
		// Simple: that's the convention with async file i/o. You must 
		// explicitly extend files before writing, like this:
		async_io_op resizedfile(dispatcher->truncate(openfile, 12)); /*< schedules resize file ready for writing after open file completes >*/

		// Config a write gather. You could do this of course as a batch
		// of writes, but a write gather has optimised host OS support in most
		// cases, so it's one syscall instead of many.
		std::vector<boost::asio::const_buffer> buffers;
		buffers.push_back(boost::asio::const_buffer("He", 2));
		buffers.push_back(boost::asio::const_buffer("ll", 2));
		buffers.push_back(boost::asio::const_buffer("o ", 2));
		buffers.push_back(boost::asio::const_buffer("Wo", 2));
		buffers.push_back(boost::asio::const_buffer("rl", 2));
		buffers.push_back(boost::asio::const_buffer("d\n", 2));
		// Schedule the write gather to offset zero
		async_io_op written(dispatcher->write(
			make_async_data_op_req(resizedfile, std::move(buffers), 0))); /*< schedules write after resize file completes >*/

		// Schedule making sure the previous batch has definitely reached physical storage
		// This won't complete until the write is on disc
		async_io_op stored(dispatcher->sync(written)); /*< schedules sync after write completes >*/

		// Schedule filling this array from the file. Note how convenient std::array
		// is and completely replaces C style char buffer[bytes]
		std::array<char, 12> buffer;
		async_io_op read(dispatcher->read(
			make_async_data_op_req(stored, buffer, 0))); /*< schedules read after sync completes >*/

		// Schedule the closing and deleting of example_file.txt after the contents read
		req.precondition=dispatcher->close(read); /*< schedules close file after read completes >*/
		async_io_op deletedfile(dispatcher->rmfile(req)); /*< schedules delete file after close completes >*/

		// Wait until the buffer has been filled, checking all steps for errors
		when_all({ openfile, resizedfile, written, stored, read, req.precondition /*close*/, deletedfile }).wait(); /*< waits for all operations to complete, throwing any exceptions encountered >*/
	}
	catch(...)
	{
		std::cerr << boost::current_exception_diagnostic_information(true) << std::endl;
		throw;
	}
}