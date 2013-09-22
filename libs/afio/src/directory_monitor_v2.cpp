#include "../../../boost/afio/directory_monitor_v2.hpp"

namespace boost{
	namespace afio{
		
		
		std::pair< future< bool >, async_io_op > dir_monitor::remove(const async_io_op & req, const path& path, Handler* handler)
		{
			auto func = std::bind(&dir_monitor::remove_path, this, path, handler);
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > dir_monitor::add(const async_io_op & req, const path& path, Handler* handler)
		{
			auto func = std::bind(&dir_monitor::add_path, this, path, handler);
			return dispatcher->call(req, func);
		}
		

		bool dir_monitor::remove_path(const path& path, Handler* handler)
		{
			//lock this durring removal
			//BOOST_AFIO_LOCK_GUARD <recursive_mutex> lk(mtx);
			Path* p = nullptr;
			try
			{
				p = &hash.at(path);
			}
			catch(std::out_of_range &e)
			{
				// if it wasn't in the hash we're not monitoring it
				// consider an exception here with a useful message
				std::cout << "couldn't find the path to remove it...\n";
				return false;
			}
			
			if(!p)
			{
				std::cout << "this is impossible!\n";
				return false;
			}
			// if we can't find the handler return false
			// again an exception might be more informative
			if(!p->remove_handler(handler))
			{
				std::cout << "couldn't remove the handler!\n";
				return false;
			}

			// if this path doesn't have any more handlers, 
			// then the monitoring is complete
			if(p->handlers.empty())
				hash_remove(path);

			// we have successfully found the path and handler,
			// removed them, and done any clean up necessary
			return true;
		}


		bool dir_monitor::add_path(const path& path, Handler* handler)
		{
			//BOOST_AFIO_LOCK_GUARD <recursive_mutex> lk(mtx);
			Path* p;
			try
			{	
				p = &hash.at(path);
				//std::cout << "going to try to add a new handler\n";
				p->add_handler(handler);
				//std::cout << "added the Handler\n";
				//p->schedule();
				//std::cout << "Scheduled\n";
				return true;
			}
			catch(std::out_of_range& e)
			{
				//std::cout << "making a new Path ...\n";
				p = new Path(dispatcher, path, eventcounter);
				//std::cout << "Created the Path\n";
				//std::cout << "going to try to add a new handler\n";
				p->add_handler(handler);
				//std::cout << "added the Handler\n";
				p->schedule();
				//std::cout << "Scheduled\n";
				if(!hash_insert(path, *p))
				{
					std::cout << "something wrong with the insertion\n";
					delete p;
					p = nullptr;
					return false;
				}
				//std::cout << "completed the catch block\n";
			}
			
		}

		bool dir_monitor::hash_insert(const path& path, const Path& dir)
		{
			//std::cout <<"try to aquire lock...\n";
			//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try{
				//std::cout <<"try to insert into hash...\n";
				if(hash.emplace(path, std::move(dir)).second)
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
			//std::cout << "removing path from hash\n";
			//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
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

		