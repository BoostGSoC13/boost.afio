#include "path.hpp"



namespace boost{
	namespace afio{
		std::pair< future< bool >, async_io_op > Path::remove_ent(const async_io_op & req, const directory_entry& ent)
		{
			auto func = std::bind(remove_ent, ent);
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::add_ent(const async_io_op & req, const directory_entry& ent)
		{
			auto func = std::bind(add_ent, ent);
			return dispatcher->call(req, func);
		}

		std::pair< future< void >, async_io_op > Path::schedule(const async_io_op & req)
		{
			return dispatcher->call(req, schedule);
		}

		std::pair< future< bool >, async_io_op > Path::add_handler(const async_io_op & req, const Handler& h)
		{
			auto func = std::bind(add_handler, ent);
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::remove_handler(const async_io_op & req, const Handler& h)
		{
			auto func = std::bind(remove_handler, ent);
			return dispatcher->call(req, func);
		}


		bool Path::remove_ent(const directory_entry& ent)
		{
			sp_lock.lock();
			try
			{
				if(dict.erase(ent) > 0)
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

		bool Path::add_ent(const directory_entry& ent)
		{
			sp_lock.lock();
			try
			{
				
				if(dict.emplace(ent, ent).second)
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

		void Path::schedule()
		{
			auto io = dispatcher->threadsource()->io_service();
			boost::asio::high_resoloution_timer t(io, boost::afio::chrono::miliseconds(500));
			
			t.async_wait(std::bind(&Path::monitor, this));
			t.run();
		}

		void Path::monitor(const boost::system::error_code& ec, boost::asio::high_resoloution_timer* t)
		{
			if(!ec)
			{
				auto dir(dispatcher->dir(boost::afio::async_path_op_req(name)));
				
				
	    		 std::pair<std::vector<boost::afio::directory_entry>, bool> list;
	    		 async_io_op my_op;
			    // This is used to reset the enumeration to the start
			    bool restart=true;
			    do{				        
			        auto enumerate(dispatcher->enumerate(dir,boost::afio::directory_entry::compatibility_maximum()));
			        restart=false;
			        list=enumeration.first.get();			        
			        my_op = enumerate.second;
			    } while(list.second);

			    auto new_ents = std::make_shared<std::vector<directory_entry>>(std::move(list.first));

			    auto handle_ptr = my_op.h->get();
			    std::vector<async_io_op> ops;
				ops.reserve(new_ents->size());
				std::vector<Handler> closures;
				closures.reserve(new_ents->size());
				std::vector<std::function<void()>> full_stat;
				full_stat.reserve(new_ents->size());
			    BOOST_FOREACH( auto &i, *new_ents)
			    {
			    	full_stat.push_back( [&i, &handle_ptr](){ i.full_lstat(handle_ptr); return i; } );
					ops.push_back(my_op);
			    }

			    auto stat_ents(dispatcher->call(ops, full_stat));

			    auto compare(dispatcher->call(stat_ents.second,[](){
			    	
			    	std::vector<std::function<void()>> funcs;
			    	funcs.reserve(new_ents->size());
			    	for (int i = 0; i < new_ents->size(); ++i)
			    	{
			    		auto func = std::bind(compare_entries, this, stat_ents.fist[i].get(), handle_ptr)
			    		//dispatcher->call(stat_ents.second[i], func)
			    		funcs.push_back(func);
			    	}
			    	return dispatcher->call(stat_ents.second, funcs);
			    }; ));

			    auto barrier(boost::afio::barrier(compare.first.begin(), compare.first.end()));

			    auto clean_dict(dispatcher->call(barrier, [](){
			    	std::vector<std::function<void()>> funcs;
			    	std::vector<async_io_op> ops;
			    	funcs.reserve(dict.size());
			    	ops.reserve(dict.size());
			    	BOOST_FOREACH(auto &i, dict)
			    	{
			    		ops.push_back(barrier);
			    		funcs.push_back(std::bind(Path::clean, this, i));
			    	}
			    	return dispatcher->call(ops, funcs);
			    }; ));

			    auto barrier_move(boost::afio::barrier(clean_dict.first.begin(), clean_dict.first.end()));

			    auto remake_dict(dispatcher->call(barrier_move, [&new_ents, &barrier_move](){
			    	BOOST_FOREACH(auto &i, new_ents)
				    {
				    	ops.push_back(barrier_move);
				    	funcs.push_back(std::bind(add_ent, this, i));
				    }
				    return dispatcher->call(ops, funcs);
			    }));
			    auto fut = remake_dict.first.get();
			    when_all(fut.begin(), fut.end()).wait();
			}

		}

		void clean(directory_entry& ent)
		{
			// anything left in dict has been deleted
			dir_event event();
			event.eventNo=++(parent->parent->eventcounter);
			event.setDeleted();
			BOOST_FOREACH(auto &i, handlers)
				dispatcher->call(async_io_op(), std::bind(i, event));

			remove_ent(ent);
		}

		void Path::compare_entries(directory_entry& entry, std::shared_ptr< async_io_handle> dirh)
		{
			static std::atomic<int> eventcounter(0);
			dir_event* ret = nullptr;
			try
			{
				
				// try to find the directory_entry
				// if it exists, then determine if anything has changed
				// if the hash is successful then it has the same inode and ctim
				auto temp =dict.at(entry);
				
				bool changed = false, renamed = false, modified = false, security = false;
				if(temp.name() != entry.name())
				{
					changed = true;
					renamed = true;
				}
				if(temp.st_mtim(dirh)!= entry.st_mtim(dirh)
						|| temp.st_size(dirh) != entry.st_size(dirh) 
						|| temp.st_allocated(dirh) != entry.st_allocated(dirh))
				{
					modified = true;
					changed = true;
				}
				if(temp.st_type(dirh) != entry.st_type(dirh))// mode is linux only...
				{
					changed = true;
					security = true;
				}
				//BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
				if(changed)
				{
					ret = new dir_event();
					ret->eventNo = ++eventcounter;
					ret->setRenamed(renamed);
					ret->setModified(modified);
					ret->setSecurity(security);
				}

				// we found this entry, so remove it to later 
				// determine what has been deleted
				// this shouldn't invalidate any iterators, but maybe its still not good
				remove_ent(temp);
			}
			catch(std::out_of_range &e)
			{
				//std::cout << "we have a new file: "  << entry.name() <<std::endl;
				//We've never seen this before
				ret = new dir_event();
				ret->eventNo=++eventcounter;
				ret->setCreated();
			}
			catch(std::exception &e)
			{
				std::cout << "another type of error occured that is ruining this\n" << e.what() <<std::endl;;
				throw(e);
			}
			if(ret)
				BOOST_FOREACH(auto &i, handlers)
					dispatcher->call(async_io_op(), std::bind(i, *ret));				
		}

		bool Path::add_handler(const Handler& h)
		{
			sp_lock.lock();
			try
			{
				if(handlers.emplace(&h, h).second)
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

		bool Path::remove_handler(const Handler& h)
		{
			sp_lock.lock();
			try
			{
				if(handlers.erase(&h) > 0)
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

	}
}