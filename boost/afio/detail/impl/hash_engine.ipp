/* hash_engine
Provides an asynchonous batch data hashing engine
(C) 2013 Niall Douglas http://www.nedprod.com/
File Created: Nov 2013
*/

#include "../../hash_engine.hpp"

namespace boost { namespace afio { namespace hash {

/* We define a second thread pool for hashing because reusing the i/o engine
would introduce unhelpful LL cache sloshing effects with certain patterns of
workload. Also, we really do want exactly the number of CPUs for this threadpool,
whereas the i/o engine threadpool needs as many threads as queue depth on some
platforms */
std::shared_ptr<std_thread_pool> hash_threadpool()
{
	static std::weak_ptr<std_thread_pool> shared;
	static boost::detail::spinlock lock;
	std::shared_ptr<std_thread_pool> ret(shared.lock());
	if(!ret)
	{
		BOOST_AFIO_LOCK_GUARD<boost::detail::spinlock> lockh(lock);
		ret=shared.lock();
		if(!ret)
		{
			shared=ret=std::make_shared<std_thread_pool>(get_number_of_cpus());
		}
	}
	return ret;
}

} } }
