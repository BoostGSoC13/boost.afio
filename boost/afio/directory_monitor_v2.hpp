#ifndef BOOST_AFIO_DIR_MONITOR_HPP
#define BOOST_AFIO_DIR_MONITOR_HPP

#include "afio.hpp"
#include <unordered_map>//may need to move this after some Hash() declarations
#include <vector>
#include <memory>
#include <stdexcept>
#include "boost/smart_ptr/detail/spinlock.hpp"
#include "path.hpp"

namespace boost{
	namespace afio{

		class BOOST_AFIO_DECL dir_monitor: public std::enable_shared_from_this<dir_monitor>
		{
			friend class boost::afio::Path;
		public:
			typedef std::filesystem::path path;
			typedef std::function<void(dir_event)> Handler; 
			typedef boost::posix_time::millisec milli_sec;

			//constructors
			dir_monitor(){}
			dir_monitor(std::shared_ptr<boost::afio::async_file_io_dispatcher_base> _dispatcher):dispatcher(_dispatcher), eventcounter(std::make_shared<std::atomic<int>>(0)) {}
			virtual ~dir_monitor()
			{
				//BOOST_AFIO_SPIN_LOCK_GUARD sp_lk(sp_lock);
				BOOST_AFIO_LOCK_GUARD <boost::mutex> lk(mtx);
				if(timer)
					timer->cancel();
			}

			//public functions
			/*! \brief  Schedules the removal of a handler on an associated directory after the preceding operation
				
				\note This will also stop the monitoring of the directory if there are no associated handlers after removal.
				monitoring can begin again after adding a new handler.

				\return A future vector of directory entries with a boolean returning false if done.
			    \param req A precondition op handle. If default constructed, the precondition is null.
			    
			   
			    \exceptionmodelstd
			    \qexample{enumerate_example}
			*/
			std::pair< future< bool >, async_io_op > remove(const async_io_op & req, const path& path, Handler* handler);
			std::pair< future< bool >, async_io_op > add(const async_io_op & req, const path& path, Handler* handler);

			//accessors
			inline std::shared_ptr<boost::afio::async_file_io_dispatcher_base> get_dispatcher(){return dispatcher;}
			inline std::unordered_map<path, Path>& get_hash(){return hash;}
			inline bool add_p(const path& path, Handler* handler){ return add_path(path, handler); }
			inline bool remove_p(const path& path, Handler* handler){ return remove_path(path, handler); }
			//change time interval???

		private:

			//private data
			std::shared_ptr<boost::afio::async_file_io_dispatcher_base> dispatcher;
			boost::mutex mtx;
			std::unordered_map<path, Path> hash;
			boost::detail::spinlock sp_lock;
			std::shared_ptr<std::atomic<int>> eventcounter;
			std::shared_ptr<boost::asio::deadline_timer> timer;

			// private member functions
			bool remove_path(const path& path, Handler* handler);
			bool add_path(const path& path, Handler* handler);
			bool hash_insert(const path& path, const Path& dir);
			bool hash_remove(const path& path);
			
		};

		inline std::shared_ptr<dir_monitor> make_monitor(std::shared_ptr<boost::afio::async_file_io_dispatcher_base> _dispatcher){return std::make_shared<dir_monitor>( _dispatcher);}
	}//namespace afio
}//namespace boost
#endif