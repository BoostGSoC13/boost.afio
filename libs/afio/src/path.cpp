#include "../../../boost/afio/path.hpp"
#include "../../../boost/afio/directory_monitor_v2.hpp"


size_t poll_rate = 250;
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

		std::pair< future< bool >, async_io_op > Path::add_handler(const async_io_op & req, Handler* h)
		{
			auto func = [this, h]() -> bool {
				return this->add_handler(h);
			};
			return dispatcher->call(req, func);
		}

		std::pair< future< bool >, async_io_op > Path::remove_handler(const async_io_op & req, Handler* h)
		{
			auto func = [this, h]() -> bool {
				return this->remove_handler(h);
			};
			return dispatcher->call(req, func);
		}


		bool Path::remove_ent(const directory_entry& ent)
		{
			BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				if(0 < dict.erase(ent) )
				{	
					//std::cout << "remove_ent was successful\n";
					return true;
				}
				else 
				{
					std::cout << "the ent could not be removed\n";
					return false;
				}
			}
			catch(std::exception &e)//this should probably be more specific
			{
				std::cout << "remove_ent had an error\n" << e.what()<< std::endl;
				//throw;//should this throw???
				return false;
			}
		}

		bool Path::add_ent(const directory_entry& ent)
		{
			//sp_lock.lock();
			// libstdc++ doesn't come with std::lock_guard
			BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				if(dict.emplace(ent, ent).second)
					return true;
				else
					return false;
			}
			catch(std::exception& e)
			{
				std::cout << "error inserting " << ent.name() 
					<< " into the hash_table: "<< e.what()<<std::endl;
				return false;
			}
		}

		void Path::schedule()
		{
			auto t_source(dispatcher->threadsource().lock());
			
			//why don't the other timers work????
			//boost::asio::high_resolution_timer t(t_source->io_service(), milli_sec(500));
			timer = std::make_shared<boost::asio::deadline_timer>(t_source->io_service(), milli_sec(poll_rate));
			
			timer->async_wait(std::bind(&Path::monitor, this, timer.get()));
			//std::cout << "Setup the async callback\n";
			dispatcher->call(async_io_op(), [t_source](){t_source->io_service().run();});
			//std::cout <<"setup was successful\n";
		}

		//void Path::monitor(boost::asio::high_resolution_timer* t)
		void Path::monitor(boost::asio::deadline_timer* t)
		{
			//try{
			//t->expires_at(t->expires_at() + milli_sec(poll_rate) );
			t->expires_from_now( milli_sec(poll_rate) );
		    t->async_wait(std::bind(&Path::monitor, this, t));
		//}catch(std::exception &e){std::cout << "the main issue is from the timer\n" <<e.what() <<std::endl;}
			
			BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
			std::cout << "monitor has been called !!!\n";
		
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
			//std::vector<Handler> closures;
			//closures.reserve(new_ents->size());
			std::vector<std::function<directory_entry()>> full_stat;
			full_stat.reserve(new_ents->size());
		    BOOST_FOREACH( auto &i, *new_ents)
		    {
		    	full_stat.push_back( [&i, &handle_ptr]()->directory_entry&{ i.fetch_lstat(handle_ptr); return i; } );
				ops.push_back(my_op);
		    }

		    auto stat_ents(dispatcher->call(ops, full_stat));
std::pair<std::vector<future<bool>>, std::vector<async_io_op>> clean_dict;
		  //  when_all(stat_ents.second.begin(), stat_ents.second.end()).wait();
		    
		    std::vector<std::function<void()>> comp_funcs;
	    	comp_funcs.reserve(new_ents->size());
	    	std::pair<std::vector<future<void>>, std::vector<async_io_op>> compare;
	    	try{
	    	BOOST_FOREACH(auto &i, stat_ents.first)
	    	{
	    		auto func = [this, &i, &handle_ptr](){
	    			try{ this->compare_entries(i, handle_ptr);}
	    			catch(std::exception &e){std::cout << " error from compare_lambda\n" << e.what() <<std::endl; }
	    		};
	    		comp_funcs.push_back(func);
	    	}
	    
		    compare = (dispatcher->call(stat_ents.second, comp_funcs));
		    when_all(compare.second.begin(), compare.second.end()).wait();
		    }catch(...){std::cout << "It really was the compre!\n";}

		    auto comp_barrier(dispatcher->barrier(compare.second));
		try{    
		    // there is a better way to schedule this but I'm missing it. 
		    // I want dict to hold only the deleted files, but I need to schedule
		    // the next op in the lambda, and then I don't know how to 
		    // finish the rest
		    when_all(comp_barrier.begin(), comp_barrier.end()).wait();

		   // std::cout << "comparisons have finished\n";
			std::vector<std::function<bool()>> clean_funcs;
			std::vector<async_io_op> clean_ops;
			clean_ops.reserve(dict.size());
	    	clean_funcs.reserve(dict.size());
	    	BOOST_FOREACH(auto &i, dict)
	    	{
	    		clean_ops.push_back(comp_barrier.front());
	    		clean_funcs.push_back([this, &i](){
	    			try{ return this->clean(i.second);}
	    			catch(std::exception &e){std::cout << "error from the clean lambda\n"<< e.what() <<std::endl;}
	    		});
	    	}
	    	
	    	//std::vector<async_io_op> barrier_move;
	    			    	
		    clean_dict = ( dispatcher->call(clean_ops, clean_funcs)); 

		    //when_all(clean_dict.first.begin(), clean_dict.first.end()).wait();
	}catch(std::exception &e){std::cout <<"Cleaning the dictionary is the issue!!!!!!!!!!\n" << e.what()<<std::endl;}

		    auto barrier_move = (dispatcher->barrier(clean_dict.second));
try{
		  //  when_all(barrier_move.begin(), barrier_move.end()).wait();
		    
	
		    
		   // std::cout <<"Dictionary has been cleaned. Size: " <<dict.size() <<std::endl;
		    //assert(dict.empty());

		    std::vector<std::function<bool()>> move_funcs;
		    std::vector<async_io_op> move_ops;
		    move_funcs.reserve(new_ents->size());
		    move_ops.reserve(new_ents->size());
		    BOOST_FOREACH(auto &i, *new_ents)
		    {
		    	move_ops.push_back(async_io_op());
		      	move_funcs.push_back([this, &i](){return this->add_ent(i);});
		    }		      		
		    			    
		    auto remake_dict(dispatcher->call(move_ops, move_funcs)); 
		    //auto fut = &remake_dict.first;
		    when_all(remake_dict.second.begin(), remake_dict.second.end()).wait();
		    //std::cout << "dict has been remade\n";
		    //assert(dict.size() <= new_ents->size());
		    }catch(...){std::cout << "Monitor is throwing an error\n";}

		}

		bool Path::clean(directory_entry& ent)
		{
			//try{
			//std::cout << "Cleaning the dict... \n";
			// anything left in dict has been deleted
			dir_event event;
			event.eventNo=++(*eventcounter);
			event.setDeleted();
			event.path = ent.name();
			BOOST_FOREACH(auto i, handlers)
				dispatcher->call(async_io_op(), [i, event](){ i.second(event); });

			if(remove_ent(ent))
			{
				//std::cout << "successful removal of ent " << ent.name() << " from dict\n";
				return true;
			}
			else
			{
				std::cout << "Problem cleaning the dict\n";
				return false;
			}
			//}catch(...){std::cout << "the issue is in clean!!!\n"; throw; }

		}

		void Path::compare_entries(future<directory_entry>& fut, std::shared_ptr< async_io_handle> dirh)
		{
			//std::cout << "comparing entries... \n";
			//static std::atomic<int> eventcounter(0);
			std::shared_ptr<dir_event> ret;
			 auto entry = fut.get(); //}catch(std::exception &e){std::cout << "Getting this future cuased and error\n" << e.what() <<std::endl;}
			try
			{			
				// try to find the directory_entry
				// if it exists, then determine if anything has changed
				// if the hash is successful then it has the same inode and ctim
				auto temp = dict.at(entry);
				
				bool changed = false;
				bool renamed = false;
				bool modified = false;
				bool security = false;

				
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
		
				
				if(changed)
				{
					try{
					std::cout << "Found a changed entry!\n";
					ret = std::make_shared<dir_event>();
					ret->eventNo = ++(*eventcounter);
					ret->path = entry.name();
					ret->setRenamed(renamed);
					ret->setModified(modified);
					ret->setSecurity(security);
					}catch(std::exception &e){std::cout << "new event couldn't allocate memory after a change...\n" <<e.what() <<std::endl; throw;}
				}

				
				try{remove_ent(temp);}catch(std::exception &e){std::cout << "remove ent couldn't allocate memory...\n" <<e.what() <<std::endl; throw;}
			}
			catch(std::out_of_range &e)
			{
				//std::cout << "we have a new file: "  << entry.name() <<std::endl;
				//We've never seen this before
				try{
				ret = std::make_shared<dir_event>();
				ret->eventNo=++(*eventcounter);
				ret->path = entry.name();
				ret->setCreated();
				}catch(std::exception &e){std::cout << "new event couldn't allocate memory durring creation ...\n" <<e.what() <<std::endl; throw;}
			}
			catch(std::exception &e)
			{
				std::cout << "Durring comparison of " << entry.name() <<" another type of error occured that is ruining this\n" << e.what() <<std::endl;
				try{remove_ent(entry);}catch(std::exception &e){std::cout << "remove ent couldn't allocate memory...\n" <<e.what() <<std::endl; throw;}
				throw;
			}
			
			if(ret)
			{
				//std::cout << "Scheduling handlers...\n";
				auto event = *ret;
				BOOST_FOREACH(auto &i, handlers)
					dispatcher->call(async_io_op(), [i, event](){ i.second(event); });
			}			
		}

		bool Path::add_handler(Handler* h)
		{
			//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				std::cout << "The handler address is: " << h << std::endl;
				if(handlers.emplace(h, *h).second)
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

		bool Path::remove_handler(Handler* h)
		{
			//BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				std::cout << "The handler address is: " << h << std::endl;
				
				auto temp = handlers.size();
				std::cout << "The size of handlers is "<< temp << std::endl;
				if(handlers.erase(h) > 0)
					return true;
				else
				{
					std::cout << "The size of handlers is "<< temp << std::endl;
					if(handlers.size() < temp)
						return true;
					return false;
				}
			}
			catch(std::exception& e)
			{
				std::cout << "error removing a handler from the hash_table: "<< e.what() << std::endl;
				return false;
			}
		}
	}// namespace afio
}//namespace boost