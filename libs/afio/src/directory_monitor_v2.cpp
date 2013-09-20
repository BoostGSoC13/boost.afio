#include "../../../boost/afio/directory_monitor_v2.hpp"

namespace boost{
	namespace afio{
		
		
		std::pair< future< bool >, async_io_op > dir_monitor::remove(const async_io_op & req, const path& path, Handler& handler)
		{
			auto func = std::bind(&dir_monitor::remove_path, this, path, handler);
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > dir_monitor::add(const async_io_op & req, const path& path, Handler& handler)
		{
			auto func = std::bind(&dir_monitor::add_path, this, path, handler);
			return dispatcher->call(req, func);
		}
		

		bool dir_monitor::remove_path(const path& path, Handler& handler)
		{
			//lock this durring removal
			mtx.lock();
			Path* p = nullptr;
			try
			{
				p = &hash.at(path);
			}
			catch(std::out_of_range &e)
			{
				// if it wasn't in the hash we're not monitoring it
				// consider an exception here with a useful message
				return false;
			}
			
			if(!p)
				return false;
			// if we can't find the handler return false
			// again an exception might be more informative
			if(!p->remove_handler(handler))
				return false;

			// if this path doesn't have any more handlers, 
			// then the monitoring is complete
			if(p->handlers.empty())
				hash_remove(path);

			// we have successfully found the path and handler,
			// removed them, and done any clean up necessary
			return true;
		}


		bool dir_monitor::add_path(const path& path, Handler& handler)
		{
			Path* p;
			try
			{
				
				p = &hash.at(path);

			}
			catch(std::out_of_range& e)
			{
				p = new Path(dispatcher, path, eventcounter);
				if(!hash_insert(path, *p))
					return false;
			}
			p->add_handler(handler);
			p->schedule();
			return true;
		}

		bool dir_monitor::hash_insert(const path& path, const Path& dir)
		{
			sp_lock.lock();
			try{
				
				if(hash.emplace(path, dir).second)
					return true;
				else
					return false;
			}
			catch(std::exception& e)
			{
				//std::cout << "error inserting " << path.string() 
				//	<< " into the hash_table: "<< e.what()<<std::endl;
				return false;
			}
		}
		bool dir_monitor::hash_remove(const path& path)
		{
			sp_lock.lock();
			try
			{
				if(hash.erase(path) > 0)
					return true;
				else 
					return false;
			}
			catch(...)//this should probably be more specific
			{
				//throw;//should this throw???
				return false;
			}
		}

	}
}

		