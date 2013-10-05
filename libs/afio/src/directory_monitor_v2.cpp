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
			std::cout << "Trying to remove the Path\n";
			BOOST_AFIO_LOCK_GUARD <boost::mutex> lk(mtx);
			Path* p = nullptr;
			bool found = true;
			//std::cout << "creating the BOOST_AFIO_SPIN_LOCK_GUARD\n";
			
			{
				//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
				{
					auto temp = hash.find(path);
					if(temp == hash.end())
						found = false;
					else
						p = &(temp->second);
				}
			}
			//std::cout << "destroying the BOOST_AFIO_SPIN_LOCK_GUARD\n";


			if(!found)
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
			std::cout << "Removing Handler!\n";
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
			std::cout << "removed path\n";
			// we have successfully found the path and handler,
			// removed them, and done any clean up necessary
			return true;
		}


		//figure out how to make this work with the spinlock. 
		bool dir_monitor::add_path(const path& path, Handler* handler)
		{
			BOOST_AFIO_LOCK_GUARD <boost::mutex> lk(mtx);
			Path* p;
			bool scheduled = false;

			if(hash.empty())
			{
				size_t poll_rate = 50;
				//auto t_source = dispatcher->threadsource().lock()
				timer = std::make_shared<boost::asio::deadline_timer>(dispatcher->threadsource()->io_service(), milli_sec(poll_rate));
				
			}
			//std::cout << "creating the BOOST_AFIO_SPIN_LOCK_GUARD\n";
			
			{
				//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
				{
					auto it = hash.find(path);
					if(it == hash.end())
						p = new Path(dispatcher, path, eventcounter, timer);
					else
					{
						scheduled = true;
						p = &(it->second);
					}
				}
			}//end of spin_lock scope
			//std::cout << "Destroying the BOOST_AFIO_SPIN_LOCK_GUARD\n";

			//std::cout << "going to try to add a new handler\n";
			if(!p->add_handler(handler))
				return false;			
			//std::cout << "added the Handler\n";
			
			if(!scheduled) 
			{
				p->schedule();
				//std::cout << "Scheduled\n";
				if(!hash_insert(path, *p))
				{
					std::cout << "something wrong with the insertion\n";
					delete p;
					p = nullptr;
					return false;
				}
			}
			std::cout << "added path\n";
			return true;
		}// end add_path()
		

		bool dir_monitor::hash_insert(const path& path, const Path& dir)
		{
			//std::cout <<"try to aquire lock...\n";
			//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try{
				//std::cout <<"try to insert into hash...\n";
				if(hash.insert(std::make_pair(path, std::move(dir))).second)
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
		}// end hash_inssert()

		bool dir_monitor::hash_remove(const path& path)
		{
			std::cout << "removing path from hash\n";
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
		}// end hash_remove()

	}// namespace afio
}// namespace boost

		