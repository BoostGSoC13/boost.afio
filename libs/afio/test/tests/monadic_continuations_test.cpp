#include "test_functions.hpp"
#include "boost/function_types/result_type.hpp"
#include "boost/function_types/function_arity.hpp"
#include "boost/function_types/parameter_types.hpp"

namespace boost {
	namespace afio {
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

				// Extract the call spec of any callable incl lambdas
				template<class F> struct callable_spec
				{
					typedef decltype(&F::operator()) type;
					typedef typename boost::function_types::result_type<type>::type return_type;
					typedef typename boost::mpl::pop_front<typename boost::function_types::parameter_types<type>::type>::type parameter_types;
					static BOOST_CONSTEXPR_OR_CONST size_t parameter_types_len=boost::function_types::function_arity<type>::value-1;
				};

				// Append a type to a tuple
				template<class... Types, class T> std::tuple<Types..., T> tuple_append(std::tuple<Types...> t, T v)
				{
					return std::tuple_cat(std::move(t), std::make_tuple(std::move(v)));
				}
			}
			template<class T, class... Types> struct thenable;
			// Implements .then()
			template<class return_type, class thenables, class callable> struct thenable_impl
			{
				// Implements .then()
				thenable<return_type> operator()(thenables &prev, callable &&f)
				{
					static_assert(!std::is_same<callable, callable>::value, "thenable<T> not enabled for type T")
				}
			};
			template<class T, class... Types> std::tuple<Types..., T> make_tuple(thenable<T, Types...> v);
			// Wrapper for a type providing a continuation .then()
			template<class T, class... Types> struct thenable : public T
			{
				template<class T, class... Types> friend std::tuple<Types..., T> make_tuple(thenable<T, Types...> v);
				typedef T value_type;
				typedef std::tuple<Types...> prev_thens_type;
			private:
				prev_thens_type prev_thens;
			public:
				// Perfect forwarding constructor
				template<class... Args> thenable(prev_thens_type &&_prev_thens, Args &&... args) : T(std::forward<Args>(args)...), prev_thens(std::move(_prev_thens)) { }
				// Continues calling f from value
				template<class F> typename std::result_of<thenable_impl<typename detail::return_type<F>::type, thenable, F>(thenable&&, F&&)>::type then(F &&f)
				{
					// WARNING: Does a destructive move despite not being a rvalue *this overload
					//          FIXME: On newer compilers implement rvalue and lvalue *this overloads
					return thenable_impl<typename detail::return_type<F>::type, thenable, F>()(std::move(*this), std::forward<F>(f));
				}
			};
			// Placeholder type where idx indexes a thenable
			template<int idx> struct thenable_get_placeholder { static BOOST_CONSTEXPR_OR_CONST int value = idx; };
			// Implements filtering .then(callable(thenable<...>)) for lambdas etc
			// Slightly complex as must match lambda types taking one arg of thenable<...>
			template<class return_type, class... Types, class callable> struct thenable_impl<return_type, thenable<Types...>, callable>
			{
				thenable<return_type, Types...> operator()(thenable<Types...> &&prev, return_type f(thenable<Types...> &&))
				{
					auto ret=f(std::forward<thenable<async_io_op, Types...>>(prev));
					return thenable<return_type, Types...>(
						make_tuple(std::move(prev)),
						std::move(ret));
				}
			};
			// Convenience
			template<class T> thenable<T> make_thenable(T &&v)
			{
				return thenable<T>(std::tuple<>(), std::forward<T>(v));
			}
			// State extractor
			template<class T, class... Types> std::tuple<Types..., T> make_tuple(thenable<T, Types...> v)
			{
				return detail::tuple_append(std::move(v.prev_thens), std::forward<T>(std::move(v)));
			}
#if 0
			// when_all(thenable<...>)
			template<class T, class... Types> future<std:tuple<T, Types...>> when_all(thenable<T, Types...> v)
			{
				auto t(make_tuple(std::move(v)));
				// FIXME: Still to implement a composition suitable for boost::wait_for_all(iterator, iterator)
			}
#endif

			// Overload tag type to invoke the right kind of close() overload etc
			template<class return_type, class _thenable_placeholder=void> struct async_io_op_tag;
			// Thenable item tag
			template<class return_type, class _thenable_placeholder> struct async_io_op_tag
			{
				static BOOST_CONSTEXPR_OR_CONST bool using_placeholder=true;
				static BOOST_CONSTEXPR_OR_CONST int thenable_idx=_thenable_placeholder::value;
				std::function<return_type(async_file_io_dispatcher_base *, async_io_op)> f;
				async_io_op_tag(std::function<return_type(async_file_io_dispatcher_base *, async_io_op)> _f) : f(std::move(_f)) { }
				return_type operator()() { } // Purely for return type deduction
			};
			// Implements .then(async_io_op_tag) for the tagged dispatch
			template<class return_type, class... Types, class tag_type> struct thenable_impl<return_type, thenable<async_io_op, Types...>, async_io_op_tag<return_type, tag_type>>
			{
				thenable<return_type, async_io_op, Types...> operator()(thenable<async_io_op, Types...> &&prev, async_io_op_tag<return_type, tag_type> &&f)
				{
					return thenable<return_type, async_io_op, Types...>(
						make_tuple(std::move(prev)),
						f.f(/*this*/prev.parent,
						/*h*/tag_type::value==-1 ? prev
						: std::get<(tag_type::value<0) ? (size_t) ((int) sizeof...(Types)+1+tag_type::value) : (size_t) tag_type::value>(make_tuple(prev))));
				}
			};
			// Immediate item tag
			template<class return_type> struct async_io_op_tag<return_type, void>
			{
				static BOOST_CONSTEXPR_OR_CONST bool using_placeholder=false;
				typedef void thenable_placeholder;
				std::function<return_type(async_file_io_dispatcher_base *)> f;
				async_io_op_tag(std::function<return_type(async_file_io_dispatcher_base *)> _f) : f(std::move(_f)) { }
				return_type operator()() { } // Purely for return type deduction
			};
			// Implements .then(async_io_op_tag) for the tagged dispatch
			template<class return_type, class... Types> struct thenable_impl<return_type, thenable<async_io_op, Types...>, async_io_op_tag<return_type>>
			{
				thenable<return_type, async_io_op, Types...> operator()(thenable<async_io_op, Types...> &&prev, async_io_op_tag<return_type> &&f)
				{
					return thenable<return_type, async_io_op, Types...>(
						make_tuple(std::move(prev)),
						f.f(/*this*/prev.parent));
				}
			};
			// Completion on a thenable item
			template<int tag_idx> inline async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>> completion(thenable_get_placeholder<tag_idx>, const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> callback)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>>(std::bind(
					(async_io_op(async_file_io_dispatcher_base::*)(
					const async_io_op &,
					const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> &))
					&async_file_io_dispatcher_base::completion,
					/*this*/_1, /*h*/_2,
					callback));
			}
			// Completion on an immediate item
			inline async_io_op_tag<async_io_op> completion(async_io_op h, const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> callback)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op>(std::bind(
					(async_io_op(async_file_io_dispatcher_base::*)(
					const async_io_op &,
					const std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>> &))
					&async_file_io_dispatcher_base::completion,
					/*this*/_1, h,
					callback));
			}
			// Truncate on a thenable item
			template<int tag_idx> inline async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>> truncate(thenable_get_placeholder<tag_idx>, off_t newsize)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>>(std::bind(
					(async_io_op(async_file_io_dispatcher_base::*)(
					const async_io_op &,
					off_t))
					&async_file_io_dispatcher_base::truncate,
					/*this*/_1, /*h*/_2,
					newsize));
			}
			// Truncate on an immediate item
			inline async_io_op_tag<async_io_op> truncate(async_io_op h, off_t newsize)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op>(std::bind(
					(async_io_op(async_file_io_dispatcher_base::*)(
					const async_io_op &,
					off_t))
					&async_file_io_dispatcher_base::truncate,
					/*this*/_1, h,
					newsize));
			}
			// Write on a thenable item
			template<int tag_idx, class T> inline async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>> write(thenable_get_placeholder<tag_idx>, std::initializer_list<T> buffers, off_t where)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op, thenable_get_placeholder<tag_idx>>(std::bind([](async_file_io_dispatcher_base *d, async_io_op h, std::initializer_list<T> buffers, off_t where)
					{
						return d->write(make_async_data_op_req(h, buffers, where));
					},
					/*this*/_1, /*h*/_2,
					buffers, where));
			}
#if 0
			// Write on an immediate item
			template<class T> inline async_io_op_tag<async_io_op> write(async_io_op h, std::initializer_list<T> buffers, off_t where)
			{
				using std::placeholders::_1;
				using std::placeholders::_2;
				return async_io_op_tag<async_io_op>(std::bind([](async_file_io_dispatcher_base *d, async_io_op h, std::initializer_list<T> buffers, off_t where)
					{
						return d->write(make_async_data_op_req(h, buffers, where));
					},
					/*this*/_1, h,
					buffers, where));
			}
#endif
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
			template<class... Types, class _> struct thenable_impl<async_file_io_dispatcher_base::completion_returntype, thenable<async_io_op, Types...>, /*catch lambdas*/_>
			{
				thenable<async_io_op, async_io_op, Types...> operator()(thenable<async_io_op, Types...> &&prev, async_file_io_dispatcher_base::completion_t f)
				{
					return thenable<async_io_op, async_io_op, Types...>(
						make_tuple(std::move(prev)),
						prev.parent->completion(prev, std::make_pair(async_op_flags::none, std::function<async_file_io_dispatcher_base::completion_t>(std::move(f)))));
				}
			};
			// Implements async_io_op.then(callable()) for lambdas etc
			template<class return_type, class... Types, class _> struct thenable_impl<return_type, thenable<async_io_op, Types...>, _>
			{
				// Ordinary callable
				template<class callable> typename std::enable_if<!(detail::callable_spec<callable>::parameter_types_len==1
					&& std::is_same<
						typename boost::mpl::at_c<typename detail::callable_spec<callable>::parameter_types, 0>::type,
						thenable<async_io_op, Types...> &&
					>::value),
					thenable<shared_future<return_type>, async_io_op, Types...>>::type
					operator()(thenable<async_io_op, Types...> &&prev, callable &&f)
				{
					return thenable<shared_future<return_type>, async_io_op, Types...>(
						make_tuple(std::move(prev)),
						prev.parent->call(prev, std::forward<callable>(f)).first);
				}
				// Filtering callable
				template<class callable> typename std::enable_if<(detail::callable_spec<callable>::parameter_types_len==1
					&& std::is_same<
						typename boost::mpl::at_c<typename detail::callable_spec<callable>::parameter_types, 0>::type,
						thenable<async_io_op, Types...> &&
					>::value),
					thenable<return_type, async_io_op, Types...>>::type
					operator()(thenable<async_io_op, Types...> &&prev, callable &&f)
				{
					auto ret=f(std::forward<thenable<async_io_op, Types...>>(prev));
					return thenable<return_type, async_io_op, Types...>(
						make_tuple(std::move(prev)),
						std::move(ret));
				}
			};
#ifdef BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
			// Implements shared_future<T>.then(callable()) for lambdas etc
			template<class return_type, class T, class... Types, class callable> struct thenable_impl<return_type, thenable<shared_future<T>, Types...>, callable>
			{
				thenable<shared_future<return_type>, shared_future<T>, Types...> operator()(thenable<shared_future<T>, Types...> &&prev, callable &&f)
				{
					return thenable<shared_future<return_type>, shared_future<T>, Types...>(
						make_tuple(std::move(prev)),
						prev.then(std::forward<callable>(f)));
				}
			};
#endif
		}
	}
}

BOOST_AFIO_AUTO_TEST_CASE(monadic_continuations_test, "Tests that the monadic continuations framework works as expected", 10)
{
	using namespace boost::afio;
	auto dispatcher=make_async_file_io_dispatcher();

	try
	{
		// Bring in the ADL tagged function invoker type. The -1 means to fetch
		// the last item from the thenable most recently calculated
		continuations::thenable_get_placeholder<-1> _;
		// In the future, all dispatcher APIs will return thenable<> anyway
		// but for now monkey patch continuations support in
		auto openedfile(continuations::make_thenable(dispatcher->file(async_path_op_req("example_file.txt", file_flags::ReadWrite))));

		// Test decay to underlying type
		auto _dis(openedfile.parent);
		// Test lambda with thenable input works as expected
		openedfile.then([](continuations::thenable<async_io_op> &&t) { return async_io_op(t); });
		// Test lambda on async_io_op with completion spec invokes completion() not call()
		openedfile.then([](size_t id, async_io_op op) -> std::pair<bool, std::shared_ptr<async_io_handle>> { return std::make_pair(true, op.get()); });
		// Test tagged function found via ADL
		openedfile.then(completion(_, std::make_pair(
			async_op_flags::none,
			std::function<async_file_io_dispatcher_base::completion_t>(
				[](size_t id, async_io_op op) -> std::pair<bool, std::shared_ptr<async_io_handle>> {
					return std::make_pair(true, op.get());
				}))));

		// Test initiator plus two tagged functions output three output states
		auto output=openedfile
			.then(truncate(_, 12))
			/* Need to avoid ADL and template argument deduction here, it segfaults VS2013 */
			.then(continuations::write<-1, const char *>(_, { "He", "ll", "o ", "Wo", "rl", "d\n" }, 0));
		// Type generated ought to be a thenable chain of async_io_ops
		static_assert(std::is_same<decltype(output), continuations::thenable<async_io_op, async_io_op, async_io_op>>::value, "output does not have correct type!");
		// Extract a tuple of outputs from a thenable chain
		auto output_values=make_tuple(output);
		static_assert(std::is_same<decltype(output_values), std::tuple<async_io_op, async_io_op, async_io_op>>::value, "output_values does not have correct type!");
		assert(std::get<0>(output_values).id==openedfile.id);
		// Compose a future from a thenable chain
		auto output_future=when_all(output);

		// Test chaining a future generic lambda to async_io_ops
		auto output2=output.then([]{
			return 5;
		});
		// Type generated ought to be a chain
		static_assert(std::is_same<decltype(output2), continuations::thenable<shared_future<int>, async_io_op, async_io_op, async_io_op>>::value, "output2 does not have correct type!");
		// Extract a tuple of outputs from a thenable chain
		auto output_values2=make_tuple(output2);
		static_assert(std::is_same<decltype(output_values2), std::tuple<async_io_op, async_io_op, async_io_op, shared_future<int>>>::value, "output_values2 does not have correct type!");

#ifdef BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION
		// Test chaining an async_io_op to a future (not implemented yet by Boost.Thread)
		auto output3=output2.then(truncate(openedfile /* explicit handle */, 12));
#endif
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
		// Bring in the ADL tagged function invoker type. The -1 means to fetch
		// the last item from the thenable most recently calculated
		continuations::thenable_get_placeholder<-1> _;
		std::array<char, 12> buffer;
		// This line monkey patches in the continuations support. Later AFIO will have dispatcher
		// always return a thenable<T>, thus making this line redundant.
		auto openedfile(make_thenable(dispatcher->file(async_path_op_req("example_file.txt", file_flags::ReadWrite))));
		when_all(openedfile
			.then(truncate(_, 12))
			.then(write(_, { "He", "ll", "o ", "Wo", "rl", "d\n" }, 0))
			.then(sync(_))
			.then(read(_, buffer, 0))
			.then(close(_))
			.then(rmfile(_, "example_file.txt"))
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