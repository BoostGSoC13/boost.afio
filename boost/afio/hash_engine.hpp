/* hash_engine
Provides an asynchonous batch data hashing engine
(C) 2013 Niall Douglas http://www.nedprod.com/
File Created: Nov 2013
*/

#ifndef BOOST_AFIO_BATCH_HASH_ENGINE_HPP
#define BOOST_AFIO_BATCH_HASH_ENGINE_HPP

#include "afio.hpp"
#include "detail/Int128_256.hpp"
#include <deque>

namespace boost { namespace afio {
	//! Namespace containing batch, parallel, hash implementations
	namespace hash {

	//! Abstract base type for hash implementations
	struct hash_impl_base
	{
		/*! Unique id for this hash implementation. I used http://www.fileformat.info/tool/hash.htm with the
		MD5 of the name of the hash type, but there is a C++11 constexpr compile-time MD5 implementation at
		https://github.com/mfontanini/Programs-Scripts/blob/master/constexpr_hashes/md5.h.
		*/
		static BOOST_CONSTEXPR_OR_CONST detail::Int128 unique_id;
		//! Type used to store a hash of this type
		typedef unsigned char value_type;
		//! Type used to store an op of this type
		typedef unsigned char op_type;
		//! Allocator used to allocate a hash of this type
		typedef std::allocator<value_type> allocator_type;
		//! Allocator used to allocate an op of this type
		typedef std::allocator<op_type> op_allocator_type;
		//! Bytes in a hash round of this type i.e. the ideal granularity with which data will be processed
		static BOOST_CONSTEXPR_OR_CONST size_t round_size=0;
		//! Minimum bytes in a hash round of this type i.e. the minimum granularity with which data will be processed
		static BOOST_CONSTEXPR_OR_CONST size_t min_round_size=0;
		//! Stream implementations available
		static BOOST_CONSTEXPR std::array<size_t, 1> stream_impls() { std::array<size_t, 1> ret; ret[0]=1; return ret; }

		// For convenience for subclasses
		static BOOST_CONSTEXPR const unsigned *int_init_iv()
		{
			// First 32 bits of the fractional parts of the square roots of the first 8 primes 2..19
			static BOOST_CONSTEXPR BOOST_AFIO_TYPEALIGNMENT(32) const unsigned int_iv[]={ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
				0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
			return int_iv;
		}
	};
	//! Personality type for a SHA256 hash
	struct SHA256 : hash_impl_base
	{
		static BOOST_CONSTEXPR detail::Int128 unique_id()
		{
			static BOOST_CONSTEXPR BOOST_AFIO_TYPEALIGNMENT(16) const unsigned int_iv[]={ 0x5adf05b5, 0xf3b412a8, 0x8a0b4220, 0xe5202f95 }; // b505df5aa812b4f320420b8a952f20e5 = SHA256
			return *((detail::Int128 *) int_iv);
		}
		typedef detail::Int256 value_type;
		struct op_type                         // 128 bytes each
		{
			value_type hash;                   // 32
			char d[64];                        // 64
			size_t pos, length;                // 8-16
			char ___pad[32-2*sizeof(size_t)];  // 16-24
			op_type()
			{
				memset(this, 0, sizeof(*this));
				hash=*(value_type *) hash_impl_base::int_init_iv();
			}
		};
		typedef detail::aligned_allocator<value_type, 32> allocator_type;
		typedef detail::aligned_allocator<op_type, 32> op_allocator_type;
		static BOOST_CONSTEXPR_OR_CONST size_t round_size=64;
		static BOOST_CONSTEXPR_OR_CONST size_t min_round_size=64;
#if BOOST_AFIO_HAVE_M128 || defined(BOOST_AFIO_HAVE_NEON128)
		static BOOST_CONSTEXPR std::array<size_t, 2> stream_impls() { std::array<size_t, 2> ret; ret[0]=4; ret[1]=1; return ret; }
#else
		static BOOST_CONSTEXPR std::array<size_t, 1> stream_impls() { std::array<size_t, 1> ret; ret[0]=1; return ret; }
#endif
		static void hash_round(std::vector<std::tuple<op_type *, const char *, size_t>> work)
		{
			size_t batch=work.size()>=4 ? 4 : 1;
			for(size_t n=0; n<work.size(); n+=batch)
			{
				batch=(work.size()-n)>=4 ? 4 : 1;
				if(4==batch)
				{
					// SIMD
					std::cout << "4-SHA256" << std::endl;
				}
				else
				{
					// Singles
					std::cout << "SHA256" << std::endl;
				}
			}
		}
	};
	//! Personality type for a 128 bit CityHash
	struct CityHash128 : hash_impl_base
	{
		static BOOST_CONSTEXPR detail::Int128 unique_id()
		{
			static BOOST_CONSTEXPR BOOST_AFIO_TYPEALIGNMENT(16) const unsigned int_iv[]={ 0x3a1afedc, 0xf8c3a1ef, 0xdb8df12d, 0x95714c49 }; // dcfe1a3aefa1c3f82df18ddb494c7195 = CityHash128
			return *((detail::Int128 *) int_iv);
		}
		typedef detail::Int128 value_type;
		struct op_type
		{
			value_type hash;
			op_type() : hash(*(value_type *) hash_impl_base::int_init_iv()) { }
		};
		typedef detail::aligned_allocator<value_type, 16> allocator_type;
		typedef detail::aligned_allocator<op_type, 16> op_allocator_type;
		static BOOST_CONSTEXPR_OR_CONST size_t round_size=256;
		static BOOST_CONSTEXPR_OR_CONST size_t min_round_size=128;
		static BOOST_CONSTEXPR std::array<size_t, 1> stream_impls() { std::array<size_t, 1> ret; ret[0]=1; return ret; }
	};
	//! Personality type for a 128 bit SpookyHash
	struct SpookyHash128 : hash_impl_base
	{
		static BOOST_CONSTEXPR detail::Int128 unique_id()
		{
			static BOOST_CONSTEXPR BOOST_AFIO_TYPEALIGNMENT(16) const unsigned int_iv[]={ 0xebd78b3b, 0x3767c454, 0xbc39062b, 0xb49c5fd5 }; // 3b8bd7eb54c467372b0639bcd55f9cb4 = SpookyHash128
			return *((detail::Int128 *) int_iv);
		}
		typedef detail::Int128 value_type;
		struct op_type
		{
			value_type hash;
			op_type() : hash(*(value_type *) hash_impl_base::int_init_iv()) { }
		};
		typedef detail::aligned_allocator<value_type, 16> allocator_type;
		typedef detail::aligned_allocator<op_type, 16> op_allocator_type;
		static BOOST_CONSTEXPR_OR_CONST size_t round_size=256;
		static BOOST_CONSTEXPR_OR_CONST size_t min_round_size=192;
		static BOOST_CONSTEXPR std::array<size_t, 1> stream_impls() { std::array<size_t, 1> ret; ret[0]=1; return ret; }
	};

	/*! \brief Returns the process threadpool for hashing data

	On first use, this instantiates a default std_thread_pool running cpu_count threads which will remain until its shared count reaches zero.
	\ingroup process_threadpool
	*/
	BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC std::shared_ptr<std_thread_pool> hash_threadpool();

	/*! \class async_hash_engine
	\brief Schedules the asynchronous hashing of blocks of data
	*/
	template<class hash_impl> class async_hash_engine : std::enable_shared_from_this<async_hash_engine<hash_impl>>
	{
		std::shared_ptr<thread_source> threadsource;
	public:
		//! A block of data to be added to the specified hashing operation
		struct block
		{
			const char *data;						//!< The block of memory to be hashed
			size_t length;							//!< Number of bytes to be hashed
			void *p;								//!< Optional user supplied pointer
			std::function<void(block&)> *done;		//!< Optional block processed callback
			block(const char *_data=nullptr, size_t _length=0, void *_p=nullptr, std::function<void(block&)> *_done=nullptr) : data(_data), length(_length), p(_p), done(_done) { }
		};
		struct op
		{
		private:
			// Need to jump through some hoops due to the state alignment problem. C++11/14 still makes
			// this surprisingly harder than it ought to be.
			struct int_state_deleter
			{
				void operator()(typename hash_impl::op_type *p) const
				{
					typename hash_impl::op_allocator_type alloc;
					alloc.destroy(p);
					alloc.deallocate(p, 1);
				}
			};
			typedef std::unique_ptr<typename hash_impl::op_type, int_state_deleter> state_ptr_t;
			static state_ptr_t int_state_creator(typename hash_impl::op_allocator_type &alloc)
			{
				auto p=alloc.allocate(1);
				try
				{
					// Can't use construct() as it needs a const ref temporary which would be misaligned
					new(p) typename hash_impl::op_type;
				}
				catch(...)
				{
					alloc.deallocate(p, 1);
					throw;
				}
				return state_ptr_t(p, int_state_deleter());
			}
		public:
			state_ptr_t state; // Needs to be kept separate due to alignment requirements
			future<typename hash_impl::value_type> hash_value;
			detail::spinlock<size_t> lock; std::deque<std::pair<std::unique_ptr<promise<block>>, block>> queue;
			size_t offset;
			bool terminated;
			op(typename hash_impl::op_allocator_type &alloc) : state(int_state_creator(alloc)), offset(0), terminated(false) { }
		};
		typedef std::shared_ptr<op> op_t;
	private:
		detail::spinlock<size_t> opslock; std::deque<std::weak_ptr<op>> ops;
		detail::spinlock<size_t> schedulerlock;
		atomic<size_t> non_empty_queues, scheduled;
	public:
		//! Constructs a new hash engine using the specified threadsource
		async_hash_engine(std::shared_ptr<thread_source> _threadsource) : threadsource(std::move(_threadsource)), non_empty_queues(0), scheduled(0) { }

		/*! \brief Begins a number of hashing operations

		\return The requested number of new hashing operations
		\param no The number of new hashing operations desired
		*/
		std::vector<op_t> begin(size_t no)
		{
			typename hash_impl::op_allocator_type alloc;
			std::vector<op_t> ret; ret.reserve(no);
			for(size_t n=0; n<no; n++)
			{
				ret.push_back(std::make_shared<op>(alloc));
				BOOST_BEGIN_MEMORY_TRANSACTION(opslock)
				{
					ops.push_back(ret.back());
				}
				BOOST_END_MEMORY_TRANSACTION(opslock)
			}
			return ret;
		}
		/*! \brief Begins a single of hash operations

		\return A new hashing operation
		*/
		op_t begin()
		{
			op_t ret=std::make_shared<op>(typename hash_impl::op_allocator_type());
			BOOST_BEGIN_MEMORY_TRANSACTION(opslock)
			{
				ops.push_back(ret);
			}
			BOOST_END_MEMORY_TRANSACTION(opslock)
			return ret;
		}
		/*! \brief Adds new blocks of data to be hashed to their given hashing operation

		\return A batch of futures corresponding to the input batch, becoming ready when
		that block has been fully consumed. Empty unless retfutures is true.
		\param reqs A batch of new blocks of data to be hashed to their given hashing
		operations. A block pointer of null means to terminate that hashing operation.
		\param retfutures Set to true to return a batch of futures.
		*/
		std::vector<future<block>> add(std::vector<std::pair<op_t, block>> reqs, bool retfutures=false)
		{
			std::vector<future<block>> ret; if(retfutures) ret.reserve(reqs.size());
			bool do_scheduling=false;
			BOOST_FOREACH(auto &i, reqs)
			{
				std::unique_ptr<promise<block>> p;
				if(retfutures)
				{
					p=std::unique_ptr<promise<block>>(new promise<block>);
					ret.push_back(p->get_future());
				}
				op &o=*i.first;
				BOOST_BEGIN_MEMORY_TRANSACTION(o.lock)
				{
					if(o.queue.empty())
					{
						++non_empty_queues;
						do_scheduling=true;
					}
					o.queue.push_back(std::make_pair(std::move(p), i.second));
				}
				BOOST_END_MEMORY_TRANSACTION(o.lock)
			}
			if(do_scheduling)
				int_doscheduling();
			return ret;
		}
		/*! \brief Adds a new block of data to be hashed to its given hashing operation

		\param op The operator to add the block to.
		\param b The block to add.
		*/
		void add(op_t op, block b)
		{
			std::vector<std::pair<op_t, block>> reqs(1, std::make_pair(std::move(op), std::move(b)));
			add(reqs);
		}
	private:
		void int_doscheduling()
		{
			if(!schedulerlock.try_lock())
				return;
			auto unlock=detail::Undoer([this]{ schedulerlock.unlock(); });
			if(_int_doscheduling())
				unlock.dismiss();
		}
		bool _int_doscheduling()
		{
			// Remove any stale ops from the front
			BOOST_BEGIN_MEMORY_TRANSACTION(opslock)
			{
				op_t temp;
				while(ops.front().expired() || !(temp=ops.front().lock()) || temp->terminated)
					ops.pop_front();
			}
			BOOST_END_MEMORY_TRANSACTION(opslock)
			size_t workers=threadsource->workers();
			std::vector<std::vector<op_t>> ops_set(workers);
			BOOST_FOREACH(auto &i, ops_set)
				i.reserve(hash_impl::stream_impls()[0]);
			auto opsit=ops_set.begin();
			// Lock the shared pointers for the first valid set of ops for available workers
			BOOST_BEGIN_MEMORY_TRANSACTION(opslock)
			{
				for(auto it=ops.begin(); it!=ops.end() && opsit!=ops_set.end(); ++it)
				{
					auto o=it->lock();
					if(o && !o->terminated && !o->queue.empty() /* probably thread safe */)
					{
						opsit->push_back(std::move(o));
						if(opsit->size()==hash_impl::stream_impls()[0])
							if(++opsit==ops_set.end())
								break;
					}
				}
			}
			BOOST_END_MEMORY_TRANSACTION(opslock)
			// Enqueue a round of hashing
			BOOST_FOREACH(auto &i, ops_set)
			{
				++scheduled;
				threadsource->enqueue(std::bind([this](std::vector<op_t> workrefs)
				{
					std::vector<std::tuple<typename hash_impl::op_type *, const char *, size_t>> work; work.reserve(workrefs.size());
					for(size_t n=0; n<workrefs.size(); n++)
					{
						op *o=workrefs[n].get();
						// Should be safe to skip locking here
						block &b=o->queue.front().second;
						work.push_back(std::make_tuple(o->state.get(), b.data+o->offset, b.length-o->offset));
					}
					hash_impl::hash_round(work);
					for(size_t n=0; n<workrefs.size(); n++)
					{
						op *o=workrefs[n].get();
						block &b=o->queue.front().second;
						if(nullptr==b.data)
							o->terminated=true;
						else
							o->offset+=hash_impl::round_size;
						if(o->offset>=b.length || o->terminated)
						{
							// Indicate this block is now done
							std::unique_ptr<promise<block>> &prom=o->queue.front().first;
							if(prom) prom->set_value(b);
							BOOST_BEGIN_MEMORY_TRANSACTION(o->lock)
							{
								o->queue.pop_front();
							}
							BOOST_END_MEMORY_TRANSACTION(o->lock)
							o->offset=0;
						}
					}
					if(!--scheduled)
					{
						_int_doscheduling();
					}
				}, i));
			}
			return !ops_set.empty(); // leave scheduler lock locked
		}
	};

} } }

#if BOOST_AFIO_HEADERS_ONLY == 1
#undef BOOST_AFIO_VALIDATE_INPUTS // Let BOOST_AFIO_NEVER_VALIDATE_INPUTS take over
#include "detail/impl/hash_engine.ipp"
#endif

#endif
