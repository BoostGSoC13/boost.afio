#ifndef BOOST_AFIO_DIR_MONITOR_HPP
#define BOOST_AFIO_DIR_MONITOR_HPP

#include "afio.hpp"
#include <unordered_map>//may need to move this after some Hash() declarations
#include <vector>
#include <memory>
#include "boost/smart_ptr/detail/spinlock.hpp"
#include "path.hpp"

namespace boost{
	namespace afio{

		class dir_monitor
		{
			typedef std::filesystem::path path;
			typedef std::function<void(dir_event)> Handler; 
		public:
			//constructors
			dir_monitor(){}
			dir_monitor(std::shared_ptr<boost::afio::async_file_io_dispatcher_base> _dispatcher):dispatcher(_dispatcher){}

			//public functions
			/*! \brief  Schedules the removal of a handler on an associated directory after the preceding operation
				
				\note This will also stop the monitoring of the directory if there are no associated handlers after removal.
				monitoring can begin again after adding a new handler.

				\return A future vector of directory entries with a boolean returning false if done.
			    \param req A precondition op handle. If default constructed, the precondition is null.
			    
			   
			    \exceptionmodelstd
			    \qexample{enumerate_example}
			*/
			std::pair< future< bool >, async_io_op > remove(const async_io_op & req, const path& path, const Handler& handler);
			std::pair< future< bool >, async_io_op > add(const async_io_op & req, const path& path, const Handler& handler);
			//change time interval???

		private:

			//private data
			std::shared_ptr<boost::afio::async_io_dispatcher_base> dispatcher;
			recursive_mutex mtx;//consider a non-recursive mutex
			std::unordered_map<path, Path> hash;
			boost::detail::spinlock sp_lock;

			// private member functions
			bool remove_path(const path& path, const Handler& handler);
			bool add_path(const path& path, const Handler& handler);
			bool hash_insert(const path& path, const Path& dir);
			bool hash_remove(const path& path);
		};
	}//namespace afio
}//namespace boost
#endif