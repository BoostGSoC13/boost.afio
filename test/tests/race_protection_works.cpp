#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(race_protection_works, "Tests that the race protection works", 600)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
#ifdef __FreeBSD__
    // ZFS really, really, really hates this test
    static BOOST_CONSTEXPR_OR_CONST size_t ITERATIONS=10;
    static BOOST_CONSTEXPR_OR_CONST size_t ITEMS=10;
#else
    static BOOST_CONSTEXPR_OR_CONST size_t ITERATIONS=1000;
    static BOOST_CONSTEXPR_OR_CONST size_t ITEMS=100;
#endif
    // Oh Windows, oh Windows, how strange you are ...
    for (size_t n = 0; n < 10; n++)
    {
      try
      {
        if (filesystem::exists("testdir"))
          filesystem::remove_all("testdir");
        break;
      }
      catch (...)
      {
        this_thread::sleep_for(chrono::milliseconds(10));
      }
    }
    
//[race_protection_example
    try
    {
      // HoldParentOpen is actually ineffectual as renames zap the parent container, but it tests more code.
      auto dispatcher = make_async_file_io_dispatcher(process_threadpool(), file_flags::HoldParentOpen);
      auto testdir = dispatcher->dir(async_path_op_req("testdir", file_flags::Create));
      async_io_op dirh;

      {
        // We can only reliably track directory renames on all platforms, so let's create 100 directories
        // which will be constantly renamed to something different by a worker thread
        std::vector<async_path_op_req> dirreqs;
        for(size_t n=0; n<ITEMS; n++)
          dirreqs.push_back(async_path_op_req::relative(testdir, to_string(n), file_flags::Create));
        auto dirs=dispatcher->dir(dirreqs);
        std::cout << "Creating " << ITEMS << " directories ..." << std::endl;
        when_all(dirs).get();
        dirh=dirs.front();
        atomic<bool> done(false);
        std::cout << "Creating worker thread to constantly rename those " << ITEMS << " directories ..." << std::endl;
        thread worker([&done, &testdir, &dirs]{
          try
          {
            for(size_t number=0; !done; number++)
            {
              for(size_t n=0; n<ITEMS; n++)
              {
                async_path_op_req::relative req(testdir, to_string(number)+"_"+to_string(n));
                //std::cout << "Renaming " << dirs[n].get()->path() << " ..." << std::endl;
                try
                {
                  dirs[n].get()->atomic_relink(req);
                }
                catch(...)
                {
                  // Windows does not permit renaming a directory containing open file handles
#ifndef WIN32
                  throw;
#endif
                }
              }
              std::cout << "Worker relinked all dirs to " << number << std::endl;
            }
          }
          catch(const system_error &e) { std::cerr << "ERROR: unit test exits via system_error code " << e.code().value() << "(" << e.what() << ")" << std::endl; abort(); }
          catch(const std::exception &e) { std::cerr << "ERROR: unit test exits via exception (" << e.what() << ")" << std::endl; abort(); }
          catch(...) { std::cerr << "ERROR: unit test exits via unknown exception" << std::endl; abort(); }
        });
        auto unworker=detail::Undoer([&done, &worker]{done=true; worker.join();});
        
        // Create some files inside the changing directories and rename them across changing directories
        std::vector<async_io_op> newfiles;
        for(size_t n=0; n<ITEMS; n++)
        {
          dirreqs[n].precondition=dirs[n];
          dirreqs[n].flags=file_flags::CreateOnlyIfNotExist|file_flags::Write;
        }
        for(size_t i=0; i<ITERATIONS; i++)
        {
          if(!newfiles.empty())
            std::cout << "Iteration " << i << ": Renaming " << ITEMS << " files and directories inside the " << ITEMS << " constantly changing directories ..." << std::endl;
          for(size_t n=0; n<ITEMS; n++)
          {
            if(!newfiles.empty())
            {
              // Relink previous new file into first directory
              //std::cout << "Renaming " << newfiles[n].get()->path() << std::endl;
              newfiles[n].get()->atomic_relink(async_path_op_req::relative(dirh, to_string(n)+"_"+to_string(i)));
              // Note that on FreeBSD if this is a file its path would be now be incorrect and moreover lost due to lack of
              // path enumeration support for files. As we throw away the handle, this doesn't show up here.

              // Have the file creation depend on the previous file creation
              dirreqs[n].precondition=dispatcher->depends(newfiles[n], dirs[n]);
            }
            dirreqs[n].path=to_string(i);
          }
          // Split into two
          std::vector<async_path_op_req> front(dirreqs.begin(), dirreqs.begin()+ITEMS/2), back(dirreqs.begin()+ITEMS/2, dirreqs.end());
          std::cout << "Iteration " << i << ": Creating " << ITEMS << " files and directories inside the " << ITEMS << " constantly changing directories ..." << std::endl;
          newfiles=dispatcher->file(front);
          auto newfiles2=dispatcher->dir(back);
          newfiles.insert(newfiles.end(), std::make_move_iterator(newfiles2.begin()), std::make_move_iterator(newfiles2.end()));
          
          // Pace the scheduling, else we slow things down a ton. Also retrieve and throw any errors.
          when_all(newfiles).get();
        }
        // Wait around for all that to process
        do
        {
          this_thread::sleep_for(chrono::seconds(1));
        } while(dispatcher->wait_queue_depth());
        // Close all handles opened during this context except for dirh
      }
      
      // Check that everything is as it ought to be
      auto _contents = dispatcher->enumerate(async_enumerate_op_req(dirh, metadata_flags::All, 10*ITEMS*ITERATIONS)).first.get().first;
      testdir=async_io_op();  // Kick out AFIO now so NTFS has itself cleaned up by the end of the checks
      dirh=async_io_op();
      dispatcher.reset();
      std::cout << "Checking that we successfully renamed " << (ITEMS*(ITERATIONS-1)+1) << " items into the same directory ..." << std::endl;
      BOOST_CHECK(_contents.size() == (ITEMS*(ITERATIONS-1)+1));
      std::set<BOOST_AFIO_V1_NAMESPACE::filesystem::path> contents;
      for(auto &i : _contents)
        contents.insert(i.name());
      BOOST_CHECK(contents.size() == (ITEMS*(ITERATIONS-1)+1));
      for(size_t i=1; i<ITERATIONS; i++)
      {
        for(size_t n=0; n<ITEMS; n++)
        {
          if(contents.count(to_string(n)+"_"+to_string(i))==0)
            std::cerr << to_string(n)+"_"+to_string(i) << std::endl;
          BOOST_CHECK(contents.count(to_string(n)+"_"+to_string(i))>0);
        }
      }
      filesystem::remove_all("testdir");
    }
    catch(...)
    {
      filesystem::remove_all("testdir");
      throw;
    }
//]
}
