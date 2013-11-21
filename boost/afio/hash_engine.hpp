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
		//! Performs a round of hashing
		static std::vector<bool> hash_round(std::vector<std::tuple<op_type *, const char *, size_t>> work);

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
		typedef detail::aligned_allocator<value_type, 32> allocator_type;
		struct op_type
		{
			value_type hash;                   // 32
			char d[64];                        // 64
			size_t pos, length;                // 8-16
			promise<value_type *> done;        // size is implementation dependent
			op_type() : hash(*(value_type *) hash_impl_base::int_init_iv()), pos(0), length(0), done(boost::promise<value_type *>(boost::allocator_arg_t(), allocator_type()))
			{
				memset(d, 0, sizeof(d));
			}
			future<value_type *> get_future()
			{
				return done.get_future();
			}
		};
		typedef detail::aligned_allocator<op_type, 32> op_allocator_type;
		static BOOST_CONSTEXPR_OR_CONST size_t round_size=64;
		static BOOST_CONSTEXPR_OR_CONST size_t min_round_size=64;
#if BOOST_AFIO_HAVE_M128 || defined(BOOST_AFIO_HAVE_NEON128)
		static BOOST_CONSTEXPR std::array<size_t, 2> stream_impls() { std::array<size_t, 2> ret; ret[0]=4; ret[1]=1; return ret; }
#else
		static BOOST_CONSTEXPR std::array<size_t, 1> stream_impls() { std::array<size_t, 1> ret; ret[0]=1; return ret; }
#endif
		static std::vector<bool> hash_round(std::vector<std::tuple<op_type *, const char *, size_t>> work)
		{
			std::vector<bool> ret(work.size());
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
				for(size_t i=0; i<batch; i++)
					if(!get<1>(work[n+i]))
					{
						get<0>(work[n+i])->done.set_value(&get<0>(work[n+i])->hash);
						ret[n+i]=true;
					}
			}
			return ret;
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
			value_type hash;                   // 16
			promise<value_type *> done;          // size is implementation dependent
			op_type() : hash(*(value_type *) hash_impl_base::int_init_iv()), done(boost::promise<value_type *>(boost::allocator_arg_t(), allocator_type()))
			{
			}
			future<value_type *> get_future()
			{
				return done.get_future();
			}
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
			promise<value_type *> done;          // size is implementation dependent
			op_type() : hash(*(value_type *) hash_impl_base::int_init_iv()), done(boost::promise<value_type *>(boost::allocator_arg_t(), allocator_type()))
			{
			}
			future<value_type *> get_future()
			{
				return done.get_future();
			}
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
			detail::spinlock<size_t> lock; // Locked while a thread is working on this op
			state_ptr_t state; // Needs to be kept separate due to alignment requirements
			future<typename hash_impl::value_type *> hash_value;
			detail::spinlock<size_t> queue_lock; std::deque<std::pair<std::unique_ptr<promise<block>>, block>> queue;
			size_t offset;
			bool terminated;
			op(typename hash_impl::allocator_type &alloc, typename hash_impl::op_allocator_type &op_alloc) : state(int_state_creator(op_alloc)), hash_value(state->get_future()), offset(0), terminated(false) { }
		};
		typedef std::shared_ptr<op> op_t;
	private:
		detail::spinlock<size_t> scheduledlock; std::deque<op_t> scheduled;							// ops waiting to be processed
		detail::spinlock<size_t> schedulelock; std::vector<std::vector<op_t>> schedule;				// ops being processed by threadsource
		std::atomic<size_t> liveworkers; // Number of pinned threads taken from thread source
	public:
		//! Constructs a new hash engine using the specified threadsource
		async_hash_engine(std::shared_ptr<thread_source> _threadsource) : threadsource(std::move(_threadsource)), schedule(threadsource->workers()), liveworkers(0)
		{
			BOOST_FOREACH(auto &i, schedule)
			{
				i.reserve(hash_impl::stream_impls()[0]);
			}
		}

		/*! \brief Begins a number of hashing operations

		\return The requested number of new hashing operations
		\param no The number of new hashing operations desired
		*/
		std::vector<op_t> begin(size_t no)
		{
			typename hash_impl::allocator_type alloc;
			typename hash_impl::op_allocator_type op_alloc;
			std::vector<op_t> ret; ret.reserve(no);
			for(size_t n=0; n<no; n++)
			{
				ret.push_back(std::make_shared<op>(alloc, op_alloc));
			}
			return ret;
		}
		/*! \brief Begins a single of hash operations

		\return A new hashing operation
		*/
		op_t begin()
		{
			op_t ret=std::make_shared<op>(typename hash_impl::allocator_type(), typename hash_impl::op_allocator_type());
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
				bool add_to_scheduled=false;
				BOOST_BEGIN_MEMORY_TRANSACTION(o.lock)
				{
					if(o.queue.empty())
						add_to_scheduled=true;
					o.queue.push_back(std::make_pair(std::move(p), i.second));
				}
				BOOST_END_MEMORY_TRANSACTION(o.lock)
				if(add_to_scheduled)
				{
					BOOST_BEGIN_MEMORY_TRANSACTION(scheduledlock)
					{
						scheduled.push_back(i.first);
					}
					BOOST_END_MEMORY_TRANSACTION(scheduledlock)
					do_scheduling=true;
				}
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
			// Collapse as many threads of work into as few as possible
			size_t workersneeded=0;
			{
				bool reallydone=false;
				BOOST_AFIO_LOCK_GUARD<decltype(schedulelock)> lockh(schedulelock);
				for(auto to1=schedule.begin(); to1!=schedule.end() && !reallydone; ++to1)
				{
					// Resize this slot to maximum capacity
					to1->resize(to1->capacity());
					workersneeded++;
					for(auto to2=to1->begin(); to2!=to1->end() && !reallydone; ++to2)
					{
						// Are you an empty slot?
						if(!*to2)
						{
							if(!scheduled.empty()) // probably threadsafe
							{
								BOOST_BEGIN_MEMORY_TRANSACTION(scheduledlock)
								{
									if(!scheduled.empty())
									{
										std::swap(*to2, scheduled.front());
										scheduled.pop_front();
									}
								}
								BOOST_END_MEMORY_TRANSACTION(scheduledlock)
							}
						}
						// Still empty? See if I can coalesce threads into SIMD
						if(to1->capacity()>1 && !*to2)
						{
							bool done=false;
							reallydone=true;
							// Find the last work item and swap it to here
							for(auto from1=schedule.rbegin(); from1!=schedule.rend() && !done; ++from1)
							{
								for(auto from2=from1->rbegin(); from2!=from1->rend() && !done; ++from2)
								{
									if(!*from2)
										continue;
									else
										reallydone=false;
									if(*to2==*from2)
									{
										done=true;
										break;
									}
									(*from2)->lock.lock();
									std::swap(*from2, *to2);
									(*to2)->lock.unlock();
									done=true;
									break;
								}
								// Remove any null op refs from end
								while(!from1->back())
									from1->pop_back();
							}
						}
					}
				}
				// If the last thread is not a full SIMD round, split it amongst more threads
				/*if(schedule[workersneeded].size()<schedule[workersneeded].capacity())
				{

				}*/
			}
			// schedule is now optimal, so enqueue more workers if needs be
			size_t newworker;
			while((newworker=++liveworkers)<workersneeded)
			{
				threadsource->enqueue(std::bind([this](size_t schedule_idx)
				{
					std::vector<op_t> &myschedule=schedule[schedule_idx];
					for(;;)
					{
						// Stop the scheduler from running while I lock my work items to prevent the scheduler messing with them
						{
							BOOST_AFIO_LOCK_GUARD<decltype(schedulelock)> lockh(schedulelock);
							// Do I have no work? If not, return this thread to its source.
							if(myschedule.empty())
							{
								--liveworkers;
								return;
							}
							BOOST_FOREACH(auto &i, myschedule)
							{
								i->lock.lock();
							}
						}
						// Construct a work queue
						std::vector<std::tuple<typename hash_impl::op_type *, const char *, size_t>> work; work.reserve(myschedule.size());
						BOOST_FOREACH(auto &i, myschedule)
						{
							op *o=i.get();
							// Should be safe to skip locking here
							block &b=o->queue.front().second;
							work.push_back(std::make_tuple(o->state.get(), b.data+o->offset, b.length-o->offset));
						}
						std::vector<bool> workfinished=hash_impl::hash_round(work);
						bool reschedule=false;
						for(size_t n=0; n<myschedule.size(); n++)
						{
							op *o=myschedule[n].get();
							if(workfinished[n])
								o->terminated=true;
							block &b=o->queue.front().second;
							o->offset+=hash_impl::round_size;
							if(o->offset>=b.length)
							{
								bool done=false;
								// Indicate this block is now done
								std::unique_ptr<promise<block>> &prom=o->queue.front().first;
								if(prom) prom->set_value(b);
								BOOST_BEGIN_MEMORY_TRANSACTION(o->queue_lock)
								{
									o->queue.pop_front();
									done=o->queue.empty();
								}
								BOOST_END_MEMORY_TRANSACTION(o->queue_lock)
								o->offset=0;
								if(done)
								{
									// No more queue
									myschedule[n].reset();
									reschedule=true;
								}
							}
							o->lock.unlock();
						}
						if(reschedule)
							int_doscheduling();
					}
				}, newworker));
			}
		}
	};

} } }

#if BOOST_AFIO_HEADERS_ONLY == 1
#undef BOOST_AFIO_VALIDATE_INPUTS // Let BOOST_AFIO_NEVER_VALIDATE_INPUTS take over
#include "detail/impl/hash_engine.ipp"
#endif

#endif
