/* AsyncRandomness.hpp
(C) 2013 Niall Douglas http://www.nedprod.com
Created: Oct 2013
*/

#ifndef BOOST_AFIO_ASYNCRANDOMNESS_H
#define BOOST_AFIO_ASYNCRANDOMNESS_H

#include "Int128_256.hpp"
#include "boost/lockfree/spsc_queue.hpp"
#include "../afio.hpp"

namespace boost {
	namespace afio {
		namespace detail {

			/* <random>'s generators are fairly hideously slow to tear down and set up, so keep an asynchronously
			filled 8Kb cache of entropy for fast individual Int128 or Int256 fills.
			*/
			static boost::lockfree::spsc_queue<unsigned long long, boost::lockfree::capacity<1024>> randomness_cache; // 8Kb
			static atomic<size_t> randomness_cache_left;
			inline void fill_randomness_cache()
			{
				const size_t amount=4096/sizeof(Int256); // Fill 4Kb at a time
				Int256 buffer[amount];
				Int256::FillQualityRandom(buffer, amount);
				unsigned long long *_buffer=(unsigned long long *) buffer;
				randomness_cache.push(_buffer, amount*4);
				randomness_cache_left+=amount*sizeof(Int256);
			}
			template<class T> inline void fetch_randomness_from_cache(T *ptr, size_t no=1)
			{
				size_t size=no*sizeof(T);
				if(!randomness_cache_left)
					fill_randomness_cache();
				else if(randomness_cache_left<4096)
					process_threadpool()->enqueue(fill_randomness_cache);
				size/=8;
				for(size_t n=0; n<size; n+=randomness_cache.pop(((unsigned long long *) ptr)+n, size-n));
			}


		}
	}
}

#endif
