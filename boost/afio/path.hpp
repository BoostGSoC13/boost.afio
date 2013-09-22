#ifndef BOOST_AFIO_PATH_HPP
#define BOOST_AFIO_PATH_HPP

#include "afio.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#ifdef STD_MTX
#include <mutex>
#else
#include <boost/thread/mutex.hpp>
#endif
#include <boost/smart_ptr/detail/spinlock.hpp>
//#include <boost/asio/high_resolution_timer.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>



namespace boost{
	namespace afio{
// libstdc++ doesn't come with std::lock_guard
#define BOOST_AFIO_SPIN_LOCK_GUARD boost::lock_guard<boost::detail::spinlock>
		
		typedef boost::afio::directory_entry directory_entry;
		
		typedef unsigned long event_no;
		typedef unsigned int event_flag;
		typedef boost::filesystem::path dir_path;

		struct BOOST_AFIO_DECL event_flags
		{
			event_flag modified	: 1;		//!< When an entry is modified
			event_flag created	: 1;		//!< When an entry is created
			event_flag deleted	: 1;		//!< When an entry is deleted
			event_flag renamed	: 1;		//!< When an entry is renamed
			event_flag attrib	: 1;		//!< When the attributes of an entry are changed 
			event_flag security	: 1;		//!< When the security of an entry is changed
		};

		struct BOOST_AFIO_DECL dir_event
		{
			event_no eventNo;				//!< Non zero event number index
			event_flags flags;				//!< bit-field of director events
			dir_path path; 					//!< Path to event/file

			dir_event() : eventNo(0) { flags.modified = false; flags.created = false; flags.deleted = false; flags.attrib = false; flags.security = false; flags.renamed = false;}
			dir_event(int) : eventNo(0) { flags.modified = true; flags.created = true; flags.deleted = true; flags.attrib = true; flags.security = true; flags.renamed = true;}
			operator event_flags() const throw()
			{
				return this->flags;
			}
			//! Sets the modified bit
			dir_event &setModified(bool v=true) throw()		{ flags.modified=v; return *this; }
			//! Sets the created bit
			dir_event &setCreated(bool v=true) throw()		{ flags.created=v; return *this; }
			//! Sets the deleted bit
			dir_event &setDeleted(bool v=true) throw()		{ flags.deleted=v; return *this; }
			//! Sets the renamed bit
			dir_event &setRenamed(bool v=true) throw()		{ flags.renamed=v; return *this; }
			//! Sets the attrib bit
			dir_event &setAttrib(bool v=true) throw()		{ flags.attrib=v; return *this; }
			//! Sets the security bit
			dir_event &setSecurity(bool v=true) throw()		{ flags.security=v; return *this; }
		};//end dir_event

		//forward declare dir_monitor
		class dir_monitor;
		class BOOST_AFIO_DECL Path
		{	
			typedef std::function<void(dir_event)> Handler; 
			//typedef chrono::duration<double, ratio<1, 1000>> milli_sec;
			//typedef boost::chrono::milliseconds milli_sec;
			typedef boost::posix_time::millisec milli_sec;

			friend class boost::afio::dir_monitor;

		private:
		
			//private data
			boost::mutex mtx;
			boost::detail::spinlock sp_lock;
			std::shared_ptr<boost::afio::async_file_io_dispatcher_base> dispatcher;
			//std::weak_ptr<boost::afio::dir_monitor> monitor;
			dir_path name;
			std::unordered_map<directory_entry, directory_entry> dict;
			//std::shared_ptr<std::vector<directory_entry>> ents;
			std::unordered_map<Handler*, Handler> handlers;
			std::shared_ptr<std::atomic<int>> eventcounter;
			std::shared_ptr<boost::asio::deadline_timer> timer;

			//private member functions
			bool remove_ent(const directory_entry& ent);
			bool add_ent(const directory_entry& ent);
			bool add_ent(future<directory_entry>& fut){ return add_ent(fut.get());}
			void schedule();
			bool add_handler(Handler* h);
			bool remove_handler(Handler* h);
			//void monitor(boost::asio::high_resolution_timer* t);
			void monitor(boost::asio::deadline_timer* t);
			void compare_entries(future<directory_entry>& fut, std::shared_ptr< async_io_handle> dirh);
			bool clean(directory_entry& ent);
			
		public:

			//constructors
			Path(std::shared_ptr<boost::afio::async_file_io_dispatcher_base> _dispatcher, const dir_path& _path, std::shared_ptr<std::atomic<int>> evt_ctr): dispatcher(_dispatcher), name(std::filesystem::absolute(_path)), eventcounter(evt_ctr) 
			{
				auto dir(dispatcher->dir(boost::afio::async_path_op_req( name)));		
	    		std::pair<std::vector<boost::afio::directory_entry>, bool> list;
	    		async_io_op my_op;
			    bool restart=true;
			    do{				        
			        auto enumerate(dispatcher->enumerate(boost::afio::async_enumerate_op_req(dir,boost::afio::directory_entry::compatibility_maximum())));
			        restart=false;
			        list=enumerate.first.get();			        
			        my_op = enumerate.second;
			    } while(list.second);
			    auto size = list.first.size();
				auto handle_ptr = my_op.h->get();
			    std::vector<async_io_op> stat_ops;
				stat_ops.reserve(size);
				std::vector<Handler> closures;
				closures.reserve(size);
				std::vector<std::function<directory_entry()>> full_stat;
				full_stat.reserve(size);
			    BOOST_FOREACH( auto &i, list.first)
			    {
			    	full_stat.push_back( [&i, &handle_ptr]()->directory_entry&{ i.fetch_lstat(handle_ptr); return i; } );
					stat_ops.push_back(my_op);
			    }

			    auto stat_ents(dispatcher->call(stat_ops, full_stat));

			    std::vector<std::function<bool()>> funcs;
			    funcs.reserve(size);
			    BOOST_FOREACH(auto &i, stat_ents.first)
			       	funcs.push_back([&]()-> bool { return this->add_ent(i); } );
			    
			    auto make_dict(dispatcher->call(stat_ents.second, funcs));
			  
			    when_all(make_dict.first.begin(), make_dict.first.end()).wait();
			    assert(dict.size() == size);
			    
			}

			Path(const Path& o): name(std::filesystem::absolute(o.name)), dispatcher(o.dispatcher), dict(o.dict), handlers(o.handlers), eventcounter(o.eventcounter) {}
			Path(Path&& o):  name(std::move(std::filesystem::absolute(o.name))), dispatcher(std::move(o.dispatcher)), dict(std::move(o.dict)), handlers(std::move(o.handlers)), eventcounter(std::move(o.eventcounter)) {}


			// public member functions
			std::pair< future< bool >, async_io_op > remove_ent(const async_io_op & req, const directory_entry& ent);
			std::pair< future< bool >, async_io_op > add_ent(const async_io_op & req, const directory_entry& ent);
			std::pair< future< void >, async_io_op > schedule(const async_io_op & req);
			std::pair< future< bool >, async_io_op > add_handler(const async_io_op & req, Handler* h);
			std::pair< future< bool >, async_io_op > remove_handler(const async_io_op & req, Handler* h);
		};

	}
}
			

#endif