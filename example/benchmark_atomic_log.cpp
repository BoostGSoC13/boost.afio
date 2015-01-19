#include "afio_pch.hpp"

/* On my Win8.1 x86 laptop Intel i5 540M @ 2.53Ghz on NTFS Samsung SSD:

Benchmarking traditional file locks with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 1720.7 attempts per second with a success rate of 1335.24 writes per second which is a 77.5984% success rate.

Benchmarking file locks via atomic append with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 2413.66 attempts per second with a success rate of 790.364 writes per second which is a 32.7455% success rate.
Traditional locks were 1.6894 times faster.

Without file_flags::TemporaryFile | file_flags::DeleteOnClose, traditional file locks were:

Benchmarking traditional file locks with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 1266.4 attempts per second with a success rate of 809.013 writes per second which is a 63.883% success rate.



On my Win8.1 x64 workstation Intel i7-3770k @ 3.9Ghz on NTFS magnetic HD:

Benchmarking traditional file locks with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 1923.97 attempts per second with a success rate of 1793.62 writes per second which is a 93.2249% success rate.

Benchmarking file locks via atomic append with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 4550.45 attempts per second with a success rate of 1094.96 writes per second which is a 24.0626% success rate.
Traditional locks were 1.63807 times faster.


On my Win8.1 x64 workstation Intel i7-3770k @ 3.9Ghz on NTFS Samsung SSD:

Benchmarking traditional file locks with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 2167.26 attempts per second with a success rate of 2032.99 writes per second which is a 93.8046% success rate.

Benchmarking file locks via atomic append with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 7304.17 attempts per second with a success rate of 1759.44 writes per second which is a 24.0882% success rate.
Traditional locks were 1.15548 times faster.


On my Linux x64 workstation Intel i7-3770k @ 3.9Ghz on ext4 Samsung SSD:

Benchmarking traditional file locks with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 3804.9 attempts per second with a success rate of 2972.49 writes per second which is a 78.1228% success rate.

Benchmarking file locks via atomic append with 2 concurrent writers ...
Waiting for threads to exit ...
For 2 concurrent writers, achieved 11458.2 attempts per second with a success rate of 1876.94 writes per second which is a 16.3808% success rate.
Traditional locks were 1.58369 times faster.

*/

//[benchmark_atomic_log
int main(int argc, const char *argv[])
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    using BOOST_AFIO_V1_NAMESPACE::off_t;
#if AFIO_STANDALONE
    using std::to_string;
#else
    using boost::to_string;
#endif
    typedef chrono::duration<double, ratio<1, 1>> secs_type;
    double traditional_locks=0, atomic_log_locks=0;
    try { filesystem::remove_all("testdir"); } catch(...) {}

    size_t totalwriters=2, writers=totalwriters;
    if(argc>1)
      writers=totalwriters=atoi(argv[1]);
    {
      auto dispatcher = make_async_file_io_dispatcher();
      auto mkdir(dispatcher->dir(async_path_op_req("testdir", file_flags::Create)));
      auto statfs_(dispatcher->statfs(mkdir, fs_metadata_flags::All));
      auto statfs(statfs_.first.get());
      std::cout << "The filing system holding our test directory is " << statfs.f_fstypename << " and has features:" << std::endl;
#define PRINT_FIELD(field, ...) \
      std::cout << "  f_flags." #field ": "; std::cout << statfs.f_flags.field __VA_ARGS__ << std::endl
      PRINT_FIELD(rdonly);
      PRINT_FIELD(noexec);
      PRINT_FIELD(nosuid);
      PRINT_FIELD(acls);
      PRINT_FIELD(xattr);
      PRINT_FIELD(compression);
      PRINT_FIELD(extents);
      PRINT_FIELD(filecompression);
#undef PRINT_FIELD
#define PRINT_FIELD(field, ...) \
      std::cout << "  f_" #field ": "; std::cout << statfs.f_##field __VA_ARGS__ << std::endl
      PRINT_FIELD(bsize);
      PRINT_FIELD(iosize);
      PRINT_FIELD(blocks, << " (" << (statfs.f_blocks*statfs.f_bsize / 1024.0 / 1024.0 / 1024.0) << " Gb)");
      PRINT_FIELD(bfree, << " (" << (statfs.f_bfree*statfs.f_bsize / 1024.0 / 1024.0 / 1024.0) << " Gb)");
      PRINT_FIELD(bavail, << " (" << (statfs.f_bavail*statfs.f_bsize / 1024.0 / 1024.0 / 1024.0) << " Gb)");
#undef PRINT_FIELD
    }
    if(1)
    {
      std::cout << "\nBenchmarking traditional file locks with " << writers << " concurrent writers ...\n";
      std::vector<thread> threads;
      atomic<bool> done(true);
      atomic<size_t> attempts(0), successes(0);
      for(size_t n=0; n<writers; n++)
      {
        threads.push_back(thread([&done, &attempts, &successes, n]{
          try
          {
            // Create a dispatcher
            auto dispatcher = make_async_file_io_dispatcher();
            // Schedule opening the log file for writing log entries
            auto logfile(dispatcher->file(async_path_op_req("testdir/log",
                file_flags::Create | file_flags::ReadWrite)));
            // Retrieve any errors which occurred
            logfile.get();
            // Wait until all threads are ready
            while(done) { this_thread::yield(); }
            while(!done)
            {
              // Traditional file locks are very simple: try to exclusively create the lock file.
              // If you succeed, you have the lock.
              auto lockfile(dispatcher->file(async_path_op_req("testdir/log.lock",
                file_flags::CreateOnlyIfNotExist | file_flags::Write | file_flags::TemporaryFile | file_flags::DeleteOnClose)));
              attempts.fetch_add(1, memory_order_relaxed);
              // v1.4 of the AFIO engine will return error_code instead of exceptions for this
              try { lockfile.get(); } catch(const system_error &e) { this_thread::sleep_for(chrono::milliseconds(1)); continue; }
              std::string logentry("I am log writer "), mythreadid(to_string(n)), logentryend("!\n");
              // Fetch the size
              off_t where=logfile->lstat().st_size, entrysize=logentry.size()+mythreadid.size()+logentryend.size();
              // Schedule extending the log file
              auto extendlog(dispatcher->truncate(logfile, where+entrysize));
              // Schedule writing the new entry
              auto writetolog(dispatcher->write(make_async_data_op_req(extendlog, {logentry, mythreadid, logentryend}, where)));
              // Schedule releasing the lock
              auto closelockfile(dispatcher->close(lockfile));
              // Fetch errors from the last operation first to avoid sleep-wake cycling
              closelockfile.get();
              extendlog.get();
              writetolog.get();
              successes.fetch_add(1, memory_order_relaxed);
            }
          }
          catch(const system_error &e) { std::cerr << "ERROR: test exits via system_error code " << e.code().value() << "(" << e.what() << ")" << std::endl; abort(); }
          catch(const std::exception &e) { std::cerr << "ERROR: test exits via exception (" << e.what() << ")" << std::endl; abort(); }
          catch(...) { std::cerr << "ERROR: test exits via unknown exception" << std::endl; abort(); }
        }));
      }
      auto begin=chrono::high_resolution_clock::now();
      done=false;
      std::this_thread::sleep_for(std::chrono::seconds(20));
      done=true;
      std::cout << "Waiting for threads to exit ..." << std::endl;
      for(auto &i : threads)
        i.join();
      auto end=chrono::high_resolution_clock::now();
      auto diff=chrono::duration_cast<secs_type>(end-begin);
      std::cout << "For " << writers << " concurrent writers, achieved " << (attempts/diff.count()) << " attempts per second with a "
      "success rate of " << (successes/diff.count()) << " writes per second which is a " << (100.0*successes/attempts) << "% success rate." << std::endl;
      traditional_locks=successes/diff.count();
    }
    
    /* Some notes on this mutual exclusion via atomic append to a file algorithm ...
    
    How this algorithm works is interesting, as it sort of resembles distributed mutual exclusion by
    message passing of which the algorithms by Suzuki and Kasami, Maekawa and Ricart and Agrawala are the
    most famous. However, we aren't passing messages, we're appending messages to a file, and we are therefore
    guaranteed that appends are atomic, and that means that unlike message passing where messages can arrive
    in any order, everyone in this algorithm sees an identical ordering of messages which is effectively a
    free Lamport clock. This, unfortunately, is where the good differences end.

    The big unhelpful difference is that locking actors aren't around to respond to messages unless they are
    trying to claim the lock. This creates a mix of rapidly oscillating messaging nodes and a potentially very
    high rate of nodes failing to respond, and this makes the traditional algorithms mentioned above problematic.
    I have therefore come up with something of my own:

    1. First thing someone who wants the lock does is to append an "I am interested in this lock" message.
    This registers the sequential priority of this claim relative to other claims based on the relative
    offset the message ends up landing at.

    2. You now scan the lock file from its end backwards to find your claim message. Its offset is now your
    unique claim id. You also find the preceding lock interest message (if any), and the subsequent lock
    interest message (if any). You now append an "This is how my claim relates to this preceding and
    subsequent claims" message. You now scan the lock file from its end backwards again to find your
    newly posted claim relation message.

    3. By reordering the claim relation messages based on their unique claim id, you have a priority queue.
    Let us imagine the combinations that two writers 1 happens before 2 could append:
      a. Interest(1)          Interest(1)          Interest(1)
      b. Relation(1, 0, 0)    Interest(2)          Interest(2)
      c. Interest(2)          Relation(1, 0, 2)    Relation(2, 1, 0)
      d. Relation(2, 1, 0)    Relation(2, 1, 0)    Relation(1, 0, 2)
    Clearly, if your claim relation message shows no preceding claim, you now have the mutex. You indicate
    this by posting a "I have the mutex" message.
    
    4. If your claim relation message does show a preceding claim, you now spin on scanning the lock file from
    the unique claim id with the mutex to the end of the lock file. When the holder of the mutex is finished,
    they unlock by scanning the lock file from its unique claim id to the end of the lock file looking for the
    next unique claim id without a rescind message, and they post an unlock message with the unique claim id
    of the next claim. After posting the unlock message, they scan the lock file from its end backwards to find
    the unlock message just posted and if the writer they just handed the mutex to has rescinded their interest,
    they nominate the next writer in the priority queue and so on until a nominated writer issues a "I have the
    mutex" message. At this point, the newly released writer may examine the lock file extents to see if it's
    worth zeroing the lock file up to the point of the just released unique claim id.

    5. For efficiency, all those interested in the lock can rescind their interest with a rescind message.
    If a process with interest dies without rescinding it, one is allowed to ignore interest messages once
    their most recent claim relation message becomes older than twenty seconds. For this reason, those interested
    in the lock, or those holding the mutex, should reissue their claim relation or mutex ownership message
    every fifteen seconds with an updated timestamp.

    */
    if(1)
    {
      std::cout << "\nBenchmarking file locks via atomic append with " << writers << " concurrent writers ...\n";
      std::vector<thread> threads;
      atomic<bool> done(true);
      atomic<size_t> attempts(0), successes(0);
      for(size_t thread=0; thread<writers; thread++)
      {
        threads.push_back(std::thread([&done, &attempts, &successes, thread]{
          try
          {
            // Create a dispatcher
            auto dispatcher = make_async_file_io_dispatcher();
            // Schedule opening the log file for writing log entries
            auto logfile(dispatcher->file(async_path_op_req("testdir/log",
                file_flags::Create | file_flags::ReadWrite)));
            // Schedule opening the lock file for scanning and hole punching
            auto lockfilez(dispatcher->file(async_path_op_req("testdir/log.lock",
                file_flags::Create | file_flags::ReadWrite)));
            // Schedule opening the lock file for atomic appending
            auto lockfilea(dispatcher->file(async_path_op_req("testdir/log.lock",
                file_flags::Create | file_flags::Write | file_flags::Append)));
            // Retrieve any errors which occurred
            lockfilea.get(); lockfilez.get(); logfile.get();
            while(!done)
            {
              // Each lock log entry is 16 bytes in length.
              enum class message_code_t : uint8_t
              {
                unlock=0,
                havelock=1,
                rescind=2,
                interest=3,
                nominate=5
              };
#pragma pack(push, 1)
              union message_t
              {
                char bytes[16];
                struct
                {
                  message_code_t code;
                  char __padding1[3];
                  uint32_t timestamp; // time_t
                  uint64_t uniqueid;
                };
              };
#pragma pack(pop)
              static_assert(sizeof(message_t)==16, "message_t is not 16 bytes long!");
              auto gettime=[]{ return (uint32_t)(std::time(nullptr)-1420070400UL/* 1st Jan 2015*/); };
              message_t temp, buffers[256];
              off_t buffersoffset;
              uint32_t nowtimestamp=gettime();
              // TODO FIXME: If multiple machines are all accessing the lock file, nowtimestamp
              //             ought to be corrected for drift

              // Step 1: Register my interest
              memset(temp.bytes, 0, sizeof(temp));
              temp.code=message_code_t::interest;
              temp.timestamp=nowtimestamp;
              temp.uniqueid=thread; // TODO FIXME: Needs to be a VERY random number to prevent collision.
              dispatcher->write(make_async_data_op_req(lockfilea, temp.bytes, sizeof(temp), 0)).get();

              // Step 2: Wait until my interest message appears, also figure out what interests precede
              //         mine and where my start of interest begins, and if someone currently has the lock
              off_t startofinterest=dispatcher->extents(lockfilez).first.get().front().first;
              off_t myuniqueid=(off_t)-1;
              bool findPreceding=true;
              std::vector<std::pair<bool, off_t>> preceding;
              std::pair<bool, off_t> lockid;
              auto iterate=[&]{
                size_t validPrecedingCount=0;
                off_t lockfilesize=lockfilez->lstat(metadata_flags::size).st_size;
                buffersoffset=lockfilesize>sizeof(buffers) ? lockfilesize-sizeof(buffers) : 0;
                //buffersoffset-=buffersoffset % sizeof(buffers[0]);
                for(; !validPrecedingCount && buffersoffset>=startofinterest && buffersoffset<lockfilesize; buffersoffset-=sizeof(buffers))
                {
                  size_t amount=(size_t)(lockfilesize-buffersoffset);
                  if(amount>sizeof(buffers))
                    amount=sizeof(buffers);
                  dispatcher->read(make_async_data_op_req(lockfilez, (void *) buffers, amount, buffersoffset)).get();
                  for(size_t n=amount/sizeof(buffers[0])-1; !validPrecedingCount && n<amount/sizeof(buffers[0]); n--)
                  {
                    // Early exit if messages have become stale
                    if(!buffers[n].timestamp || (buffers[n].timestamp<nowtimestamp && nowtimestamp-buffers[n].timestamp>20))
                    {
                      startofinterest=buffersoffset+n*sizeof(buffers[0]);
                      break;
                    }
                    // Find if he is locked or unlocked
                    if(lockid.second==(off_t) -1)
                      if(buffers[n].code==message_code_t::unlock)
                        lockid=std::make_pair(false, buffers[n].uniqueid);
                      else if(buffers[n].code==message_code_t::havelock)
                        lockid=std::make_pair(true, buffers[n].uniqueid);
                    // Am I searching for my interest?
                    if(myuniqueid==(off_t)-1)
                    {
                      if(!memcmp(buffers+n, &temp, sizeof(temp)))
                        myuniqueid=buffersoffset+n*sizeof(buffers[0]);
                    }
                    else if(findPreceding && (buffers[n].uniqueid<myuniqueid || buffersoffset+n*sizeof(buffers[0])<myuniqueid))
                    {
                      // We are searching for preceding claims now
                      if(buffers[n].code==message_code_t::rescind || buffers[n].code==message_code_t::unlock)
                        preceding.push_back(std::make_pair(false, buffers[n].uniqueid));
                      else if(buffers[n].code==message_code_t::nominate || buffers[n].code==message_code_t::havelock)
                      {
                        if(buffers[n].uniqueid<myuniqueid && preceding.end()==std::find(preceding.begin(), preceding.end(), std::make_pair(false, (off_t) buffers[n].uniqueid)))
                        {
                          preceding.push_back(std::make_pair(true, buffers[n].uniqueid));
                          validPrecedingCount++;
                        }
                      }
                      else if(buffers[n].code==message_code_t::interest)
                      {
                        if(buffersoffset+n*sizeof(buffers[0])<myuniqueid && preceding.end()==std::find(preceding.begin(), preceding.end(), std::make_pair(false, buffersoffset+n*sizeof(buffers[0]))))
                        {
                          preceding.push_back(std::make_pair(true, buffersoffset+n*sizeof(buffers[0])));
                          validPrecedingCount++;
                        }
                      }
                    }
                  }
                }
#if 0
                std::cout << thread << ": myuniqueid=" << myuniqueid << " startofinterest=" << startofinterest << " size=" << lockfilez->lstat(metadata_flags::size).st_size << " lockid=" << lockid.first << "," << lockid.second << " preceding=";
                for(auto &i : preceding)
                  std::cout << i.first << "," << i.second << ";";
                std::cout << std::endl;
#endif
                if(findPreceding)
                {
                  // Remove all rescinded interests preceding ours
                  preceding.erase(std::remove_if(preceding.begin(), preceding.end(), [](const std::pair<bool, off_t> &i){ return !i.first; }), preceding.end());
                  std::sort(preceding.begin(), preceding.end());
                  findPreceding=false;
                }
              };
              do
              {
                lockid=std::make_pair(false, (off_t)-1);
                iterate();
                // Didn't find it, so sleep and retry, maybe st_size will have updated by then
                if(myuniqueid==(off_t)-1) this_thread::sleep_for(chrono::milliseconds(1));
              } while(myuniqueid==(off_t)-1);

              // Step 3: If there is no lock and no interest precedes mine, claim the mutex. Else issue a nominate for myself,
              //         once per ten seconds.
              {
                nowtimestamp=0;
                mutex m;
                condition_variable c;
                unique_lock<decltype(m)> l(m);
                atomic<bool> fileChanged(false);
                for(;;)
                {
                  attempts.fetch_add(1, memory_order_relaxed);
                  temp.timestamp=gettime();
                  temp.uniqueid=myuniqueid;
                  if(preceding.empty())
                  {
                    temp.code=message_code_t::havelock;
                    dispatcher->write(make_async_data_op_req(lockfilea, temp.bytes, sizeof(temp), 0)).get();
                    // Zero the range between startofinterest and myuniqueid
                    if(startofinterest<myuniqueid)
                    {
                      std::vector<std::pair<off_t, off_t>> range={{startofinterest, myuniqueid-startofinterest}};
                      dispatcher->zero(lockfilez, range).get();
  //                    std::cout << thread << ": lock taken for myuniqueid=" << myuniqueid << ", zeroing " << range.front().first << ", " << range.front().second << std::endl;
                    }
                    break;
                  }
                  else
                  {
                    auto lockfilechanged=[&]{
                      fileChanged=true;
                      c.notify_all();
                    };
                    // TODO FIXME: Put a modify watch on the lockfile instead of spinning
                    if(temp.timestamp-nowtimestamp>=10)
                    {
                      temp.code=message_code_t::nominate;
                      dispatcher->write(make_async_data_op_req(lockfilea, temp.bytes, sizeof(temp), 0)).get();
                      nowtimestamp=temp.timestamp;
                    }
                    //c.wait_for(l, chrono::milliseconds(1), [&fileChanged]{ return fileChanged==true; });
                    fileChanged=false;
                    preceding.clear();
                    findPreceding=true;
                    lockid=std::make_pair(false, (off_t)-1);
                    iterate();
                  }
                }
              }

              // Step 4: I now have the lock, so do my thing
              std::string logentry("I am log writer "), mythreadid(to_string(thread)), logentryend("!\n");
              // Fetch the size
              off_t where=logfile->lstat().st_size, entrysize=logentry.size()+mythreadid.size()+logentryend.size();
              // Schedule extending the log file
              auto extendlog(dispatcher->truncate(logfile, where+entrysize));
              // Schedule writing the new entry
              auto writetolog(dispatcher->write(make_async_data_op_req(extendlog, {logentry, mythreadid, logentryend}, where)));
              // Fetch errors from the last operation first to avoid sleep-wake cycling
              writetolog.get();
              extendlog.get();
              successes.fetch_add(1, memory_order_relaxed);
//              std::cout << thread << ": doing work for myuniqueid=" << myuniqueid << std::endl;
//              this_thread::sleep_for(chrono::milliseconds(250));

              // Step 5: Release the lock
              temp.code=message_code_t::unlock;
              temp.timestamp=gettime();
              temp.uniqueid=myuniqueid;
//              std::cout << thread << ": lock released for myuniqueid=" << myuniqueid << std::endl;
              dispatcher->write(make_async_data_op_req(lockfilea, temp.bytes, sizeof(temp), 0)).get();
            }
          }
          catch(const system_error &e) { std::cerr << "ERROR: test exits via system_error code " << e.code().value() << "(" << e.what() << ")" << std::endl; abort(); }
          catch(const std::exception &e) { std::cerr << "ERROR: test exits via exception (" << e.what() << ")" << std::endl; abort(); }
          catch(...) { std::cerr << "ERROR: test exits via unknown exception" << std::endl; abort(); }
        }));
      }
      auto begin=chrono::high_resolution_clock::now();
      done=false;
      std::this_thread::sleep_for(std::chrono::seconds(20));
      done=true;
      std::cout << "Waiting for threads to exit ..." << std::endl;
      for(auto &i : threads)
        i.join();
      auto end=chrono::high_resolution_clock::now();
      auto diff=chrono::duration_cast<secs_type>(end-begin);
      std::cout << "For " << writers << " concurrent writers, achieved " << (attempts/diff.count()) << " attempts per second with a "
      "success rate of " << (successes/diff.count()) << " writes per second which is a " << (100.0*successes/attempts) << "% success rate." << std::endl;
      atomic_log_locks=successes/diff.count();
    }
    filesystem::remove_all("testdir");
    std::cout << "Traditional locks were " << (traditional_locks/atomic_log_locks) << " times faster." << std::endl;
    return 0;
}
//]
