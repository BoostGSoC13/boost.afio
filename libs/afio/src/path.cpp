#include "../../../boost/afio/path.hpp"
#include "../../../boost/afio/directory_monitor_v2.hpp"


size_t poll_rate = 100;
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
			// libstdc++ doesn't come with std::lock_guard
			BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				if(dict.insert(std::make_pair(ent, ent)).second)
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
			//BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
			auto t_source(dispatcher->threadsource().lock());
			
			//why don't the other timers work????
			//boost::asio::high_resolution_timer t(t_source->io_service(), milli_sec(500));

			auto timer = std::make_shared<boost::asio::deadline_timer>(t_source->io_service(), milli_sec(poll_rate));
			timers.push_back(timer);
			timer->async_wait(std::bind(&Path::monitor, this, timer.get()));
			//std::cout << "Setup the async callback\n";
			dispatcher->call(async_io_op(), [t_source](){t_source->io_service().run();});
			//std::cout <<"setup was successful\n";
		}

		//void Path::monitor(boost::asio::high_resolution_timer* t)
		void Path::monitor(boost::asio::deadline_timer* t)
		{
			// stop monitoring if the directory no longer exists
			if(std::filesystem::exists(name))
			{
				//t->expires_at(t->expires_at() + milli_sec(poll_rate) );
				t->expires_from_now( milli_sec(poll_rate) );
			    t->async_wait(std::bind(&Path::monitor, this, t));
				
				BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
				
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
			    //std::cout << "Dirctory size is: " << new_ents->size() <<std::endl;

			    auto handle_ptr = my_op.h->get();
			    std::vector<async_io_op> ops;
				ops.reserve(new_ents->size());
				std::vector<std::function<directory_entry()>> full_stat;
				full_stat.reserve(new_ents->size());

			    BOOST_FOREACH( auto &i, *new_ents)
			    {
			    	full_stat.push_back( [&i, &handle_ptr]()->directory_entry&{ i.fetch_lstat(handle_ptr); return i; } );
					ops.push_back(async_io_op());
			    }

			    auto stat_ents(dispatcher->call(ops, full_stat));
	//std::pair<std::vector<future<bool>>, std::vector<async_io_op>> clean_dict;
			    //when_all(stat_ents.first.begin(), stat_ents.first.end()).wait();
			    
			    std::vector<std::function<bool()>> comp_funcs;
		    	comp_funcs.reserve(new_ents->size());
		    	//std::pair<std::vector<future<bool>>, std::vector<async_io_op>> compare;
		    	
		    	BOOST_FOREACH(auto &i, stat_ents.first)
		    	{    //i.wait();
		    		auto ptr = &i;
		    		comp_funcs.push_back(std::bind(
		    			[this, &handle_ptr](future<directory_entry>* fut)->bool{
		    			return this->compare_entries(std::move(*fut), handle_ptr);		    			
		    		}, 
		    		ptr));
		    	}
		    
			    auto compare(dispatcher->call(stat_ents.second, comp_funcs));
			    when_all(compare.first.begin(), compare.first.end()).wait();

			   // auto comp_barrier(dispatcher->barrier(compare.second));
			   
			    // there is a better way to schedule this but I'm missing it. 
			    // I want dict to hold only the deleted files, but I need to schedule
			    // the next op in the lambda, and then I don't know how to 
			    // finish the rest
			    
			    std::vector<std::function<bool()>> clean_funcs;
				std::vector<async_io_op> clean_ops;
		
			    auto setup_removal(dispatcher->call(async_io_op(), [&](){

					clean_ops.reserve(dict.size());
			    	clean_funcs.reserve(dict.size());
			    	BOOST_FOREACH(auto &i, dict)
			    	{
			    		clean_ops.push_back(async_io_op());
			    		clean_funcs.push_back([this, &i](){
			    			 return this->clean(i.second);
			    		});
			    	}
			    }));
		    	
		    	
		    	setup_removal.first.wait();

			    auto clean_dict(dispatcher->call(clean_ops, clean_funcs)); 

			    //when_all(clean_dict.first.begin(), clean_dict.first.end()).wait();
			    
		
			    //auto barrier_move = (dispatcher->barrier(clean_dict.second));
	
			  	// when_all(barrier_move.begin(), barrier_move.end()).wait();
			    
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
			      	//add_ent(i);
			    }		      		
			    			    
			    auto remake_dict(dispatcher->call(move_ops, move_funcs)); 
			    //auto fut = &remake_dict.first;
			    when_all(remake_dict.first.begin(), remake_dict.first.end()).wait();
			    //std::cout << "dict has been remade\n";
			    //assert(dict.size() == new_ents->size());
			}
	
		}// end monitor()

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

		bool Path::compare_entries(future<directory_entry> fut, std::shared_ptr< async_io_handle> dirh)
		{
			//std::cout << "comparing entries... \n";
			//static std::atomic<int> eventcounter(0);
			std::shared_ptr<dir_event> ret;
			auto entry = fut.get(); //}catch(std::exception &e){std::cout << "Getting this future cuased and error\n" << e.what() <<std::endl;}
			
						
				// try to find the directory_entry
				// if it exists, then determine if anything has changed
				// if the hash is successful then it has the same inode and ctim
			bool new_file = false;
			directory_entry* temp = nullptr;

			{
				BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
				{
					auto it = dict.find(entry);
					if(it == dict.end())
						new_file = true;
					else
						temp = &(it->second);
				}
			}

			if(new_file)
			{
				//std::cout << "we have a new file: "  << entry.name() <<std::endl;
				//We've never seen this before
				ret = std::make_shared<dir_event>();
				ret->eventNo=++(*eventcounter);
				ret->path = entry.name();
				ret->setCreated();
			}
			else	
			{		
			// consider replacing this whole section with XORs and using a metadata_flags
			// to track the changes to the files, will require changing dir_event
				bool changed = false;
				bool renamed = false;
				bool modified = false;
				bool security = false;
				
				if(temp->name() != entry.name())
				{
					changed = true;
					renamed = true;
				}

				if(temp->st_mtim()!= entry.st_mtim()
						|| temp->st_ctim() != entry.st_ctim()
						|| temp->st_size() != entry.st_size() 
						|| temp->st_allocated() != entry.st_allocated())
				{
					modified = true;
					changed = true;
				}

				if(temp->st_type() != entry.st_type()
		#ifndef WIN32
						|| temp->st_mode() != entry.st_mode()
		#endif
						)
				{
					changed = true;
					security = true;
				}
		
				if(changed)
				{
					//std::cout << "Found a changed entry!\n";
					ret = std::make_shared<dir_event>();
					ret->eventNo = ++(*eventcounter);
					ret->path = entry.name();
					ret->setRenamed(renamed);
					ret->setModified(modified);
					ret->setSecurity(security);
				}

				
				remove_ent(*temp);
			}//end if
			
			if(ret)
			{
				//std::cout << "Scheduling handlers...\n";
				auto event = *ret;
				BOOST_FOREACH(auto &i, handlers)
					dispatcher->call(async_io_op(), [i, event](){ i.second(event); });
			}
			return true;			
		}// end compare_entries

		bool Path::add_handler(Handler* h)
		{
			BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try
			{
				//std::cout << "The handler address is: " << h << std::endl;
				auto temp = *h;
				if(handlers.insert(std::make_pair(h, std::move(temp))).second)
				{
					//std::cout << "The handler was addedd\n";
					return true;
				}
				else
				{
					//std::cout << "Error adding the Handler!!!!\n";
					return false;
				}
			}
			catch(std::exception& e)
			{
				//std::cout << "error inserting the handler into the hash_table: "<< e.what()<<std::endl;
				return false;
			}
		}

		bool Path::remove_handler(Handler* h)
		{
			//cant figure out how to do this better than with a try-catch...
			BOOST_AFIO_SPIN_LOCK_GUARD lk(sp_lock);
			try{
			
				//std::cout << "The handler address is: " << h << std::endl;
				
				auto temp = handlers.size();
				//std::cout << "The size of handlers is "<< temp << std::endl;
				if(handlers.erase(h) > 0)
					return true;
				else
				{
					//std::cout << "The size of handlers is "<< temp << std::endl;
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