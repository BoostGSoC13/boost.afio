#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(atomic_log_append, "Tests that atomic append to a shared log file works as expected", 60)
{
    std::cout << "\n\nTesting atomic append to a shared log file\n";
    using namespace BOOST_AFIO_V1_NAMESPACE;
    using BOOST_AFIO_V1_NAMESPACE::off_t;
    filesystem::create_directory("testdir");
    std::vector<std::thread> threads;
    std::atomic<bool> done;
    for(size_t n=0; n<4; n++)
    {
      threads.push_back(std::thread([&done, n]{
//[extents_example
        // Create a dispatcher
        auto dispatcher = make_async_file_io_dispatcher();
        // Schedule opening the log file for hole punching
        auto logfilez(dispatcher->file(async_path_op_req("testdir/log",
            file_flags::Create | file_flags::ReadWrite)));
        // Schedule opening the log file for atomic appending of log entries
        auto logfilea(dispatcher->file(async_path_op_req("testdir/log",
            file_flags::Create | file_flags::Write | file_flags::Append)));
        // Retrieve any errors which occurred
        logfilez.get(); logfilea.get();
        // Initialise a random number generator
        ranctx ctx; raninit(&ctx, (u4) n);
        while(!done)
        {
          // Each log entry is 32 bytes in length
          union
          {
            char bytes[32];
            struct
            {
              uint64 id;     // The id of the writer
              uint64 r;      // A random number
              uint64 h1, h2; // A hash of the previous two items
            };
          } buffer;
          buffer.id=n;
          buffer.r=ranval(&ctx);
          buffer.h1=buffer.h2=1;
          SpookyHash::Hash128(buffer.bytes, 16, &buffer.h1, &buffer.h2);
          // Atomically append the log entry to the log file and wait for it
          // to complete, then fetch the new size of the log file.
          stat_t s=dispatcher->write(make_async_data_op_req(logfilea,
            buffer.bytes, 32, 0))->lstat();
          off_t allocated=s.st_allocated*s.st_blksize;
          if(allocated>1024)
          {
            // Allocated space exceeds 1Kb. The actual file size reported may be
            // many times larger if the filing system supports hole punching.
            
            // Get the list of allocated extents
            std::vector<std::pair<off_t, off_t>> extents=
                dispatcher->extents(logfilea).first.get();
            extents.resize(1);
            extents.front().second-=1024;
            // Fire and forget chopping off the front of the log file
            dispatcher->zero(logfilez, extents);
          }
        }
//]
      }));
    }
    std::this_thread::sleep_for(std::chrono::seconds(10));
    done=true;
    std::cout << "Waiting for threads to exit ..." << std::endl;
    for(auto &i : threads)
      i.join();
    std::cout << "Examining the file ..." << std::endl;
    // Examine the file for consistency
    {
      std::ifstream is("testdir/log");
      uint64 hash1, hash2;
      union
      {
        char bytes[32];
        struct { uint64 r1, r2, h1, h2; };
      } buffer;
      bool isZero=true;
      size_t entries=0;
      while(is.good())
      {
        is.read(buffer.bytes, 32);
        for(size_t n=0; n<32; n++)
          if(buffer.bytes[n]) goto startprinting;
      }
      while(is.good())
      {
        is.read(buffer.bytes, 32);
        isZero=true;
        for(size_t n=0; n<32; n++)
          if(buffer.bytes[n]) isZero=false;
        BOOST_CHECK(!isZero);
        if(isZero)
        {
          std::cout << "(zero)" << std::endl;
        }
        else
        {
startprinting:
          // First 16 bytes is random, second 16 bytes is hash
          hash1=hash2=1;
          SpookyHash::Hash128(buffer.bytes, 16, &hash1, &hash2);
          BOOST_REQUIRE((buffer.h1==hash1 && buffer.h2==hash2));
          entries++;
        }
      }
      std::cout << "There were " << entries << " valid entries." << std::endl;
    }
    //filesystem::remove_all("testdir");
}