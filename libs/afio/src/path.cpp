#include "../../../boost/afio/path.hpp"



namespace boost{
	namespace afio{
		std::pair< future< bool >, async_io_op > Path::remove_ent(const async_io_op & req, const directory_entry& ent)
		{
			auto func = [this, &ent]() -> bool {
				return this->remove_ent(ent);
			};
			
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::add_ent(const async_io_op & req, const directory_entry& ent)
		{
			auto func = [this, &ent]() -> bool {
				return this->add_ent(ent);
			};
			return dispatcher->call(req, func);
		}

		std::pair< future< void >, async_io_op > Path::schedule(const async_io_op & req)
		{
			auto func = [this](){
				this->schedule();
			};
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::add_handler(const async_io_op & req, Handler& h)
		{
			auto func = [this, &h]() -> bool {
				return this->add_handler(h);
			};
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::remove_handler(const async_io_op & req, Handler& h)
		{
			auto func = [this, &h]() -> bool {
				return this->remove_handler(h);
			};
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
			auto t_source(dispatcher->threadsource().lock());
			
			//why don't the other timers work????
			boost::asio::high_resolution_timer t(t_source->io_service());
			t.expires_from_now(milli_sec(500));
			//boost::asio::deadline_timer t(t_source->io_service(), boost::posix_time::seconds(1));
			
			t.async_wait(std::bind(&Path::monitor, this, &t));
			t_source->io_service().run();
		}

		void Path::monitor(const boost::system::error_code& ec, boost::asio::high_resolution_timer* t)
		{
			if(!ec)
			{

				auto dir(dispatcher->dir(boost::afio::async_path_op_req(name)));
				
				
	    		 std::pair<std::vector<boost::afio::directory_entry>, bool> list;
	    		 async_io_op my_op;
			    // This is used to reset the enumeration to the start
			    bool restart=true;
			    do{				        
			        auto enumerate(dispatcher->enumerate(boost::afio::async_enumerate_op_req(dir,boost::afio::directory_entry::compatibility_maximum())));
			        restart=false;
			        list=enumerate.first.get();			        
			        my_op = enumerate.second;
			    } while(list.second);

			    auto new_ents = std::make_shared<std::vector<directory_entry>>(std::move(list.first));

			    auto handle_ptr = my_op.h->get();
			    std::vector<async_io_op> ops;
				ops.reserve(new_ents->size());
				std::vector<Handler> closures;
				closures.reserve(new_ents->size());
				std::vector<std::function<directory_entry()>> full_stat;
				full_stat.reserve(new_ents->size());
			    BOOST_FOREACH( auto &i, *new_ents)
			    {
			    	full_stat.push_back( [&i, &handle_ptr]()->directory_entry&{ i.fetch_lstat(handle_ptr); return i; } );
					ops.push_back(my_op);
			    }

			    auto stat_ents(dispatcher->call(ops, full_stat));

			    std::vector<std::function<void()>> comp_funcs;
		    	comp_funcs.reserve(new_ents->size());
		    	BOOST_FOREACH(auto &i, stat_ents.first)
		    	{
		    		auto func = [this, &i, &handle_ptr](){this->compare_entries(i, handle_ptr);};
		    		//std::bind(&Path::compare_entries, this, stat_ents.first[i].get(), handle_ptr)
		    		//dispatcher->call(stat_ents.second[i], func)
		    		comp_funcs.push_back(func);
		    	}
		    

			    auto compare(dispatcher->call(stat_ents.second,comp_funcs));

			    auto comp_barrier(dispatcher->barrier(compare.second));



				std::vector<std::function<void()>> clean_funcs;		    	
		    	clean_funcs.reserve(dict.size());
		    	BOOST_FOREACH(auto &i, dict)
		    		clean_funcs.push_back([this, &i](){ this->clean(i.second);});
		    			    	
			    auto clean_dict(dispatcher->call(comp_barrier, clean_funcs)); 

			    auto barrier_move(dispatcher->barrier(clean_dict.second));

			    std::vector<std::function<bool()>> move_funcs;
			    move_funcs.reserve(new_ents->size());
			    BOOST_FOREACH(auto &i, *new_ents)
			      	move_funcs.push_back([this, &i](){return this->add_ent(i);});
			      		
			    			    
			    auto remake_dict(dispatcher->call(barrier_move, move_funcs)); 
			    auto fut = &remake_dict.first;
			    when_all(fut->begin(), fut->end()).wait();

			    t->expires_at(t->expires_at() + milli_sec(500) );
			    t.async_wait(std::bind(&Path::monitor, this, t));
			}

		}

		void Path::clean(directory_entry& ent)
		{
			// anything left in dict has been deleted
			dir_event event;
			event.eventNo=++(*eventcounter);
			event.setDeleted();
			BOOST_FOREACH(auto &i, handlers)
				dispatcher->call(async_io_op(), [i, event](){ i.second(event); });

			remove_ent(ent);
		}

		void Path::compare_entries(future<directory_entry>& fut, std::shared_ptr< async_io_handle> dirh)
		{
			//static std::atomic<int> eventcounter(0);
			dir_event* ret = nullptr;
			try
			{
				auto entry = fut.get();

				
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
					ret->eventNo = ++(*eventcounter);
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
				ret->eventNo=++(*eventcounter);
				ret->setCreated();
			}
			catch(std::exception &e)
			{
				std::cout << "another type of error occured that is ruining this\n" << e.what() <<std::endl;;
				throw(e);
			}
			if(ret)
			{
				auto event = *ret;
				BOOST_FOREACH(auto &i, handlers)
					dispatcher->call(async_io_op(), [i, event](){ i.second(event); });
			}
		}

		bool Path::add_handler(Handler& h)
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

		bool Path::remove_handler(Handler& h)
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