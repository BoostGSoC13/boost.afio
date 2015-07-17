#include "afio_pch.hpp"

int main(void)
{        
    try
    {
        //[readwrite_example
        namespace afio = BOOST_AFIO_V1_NAMESPACE;
        namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;

        std::shared_ptr<afio::async_file_io_dispatcher_base> dispatcher=
            afio::make_async_file_io_dispatcher();
            
        // Schedule an opening of a file called example_file.txt
        afio::async_path_op_req req("example_file.txt",
            afio::file_flags::create|afio::file_flags::read_write);
        afio::async_io_op openfile(dispatcher->file(req)); /*< schedules open file as soon as possible >*/
        
        // Something a bit surprising for many people is that writing off
        // the end of a file in AFIO does NOT extend the file and writes
        // which go past the end will simply fail instead. Why not?
        // Simple: that's the convention with async file i/o, because
        // synchronising multiple processes concurrently adjusting a
        // file's length has significant overhead which is wasted if you
        // don't need that functionality. Luckily, there is an easy
        // workaround: either open a file for append-only access, in which
        // case all writes extend the file for you, or else you explicitly
        // extend files before writing, like this:
        afio::async_io_op resizedfile(dispatcher->truncate(openfile, 12)); /*< schedules resize file ready for writing after open file completes >*/
    
        // Config a write gather. You could do this of course as a batch
        // of writes, but a write gather has optimised host OS support in most
        // cases, so it's one syscall instead of many.
        std::vector<asio::const_buffer> buffers;
        buffers.push_back(asio::const_buffer("He", 2));
        buffers.push_back(asio::const_buffer("ll", 2));
        buffers.push_back(asio::const_buffer("o ", 2));
        buffers.push_back(asio::const_buffer("Wo", 2));
        buffers.push_back(asio::const_buffer("rl", 2));
        buffers.push_back(asio::const_buffer("d\n", 2));
        // Schedule the write gather to offset zero
        afio::async_io_op written(dispatcher->write(
            afio::make_async_data_op_req(resizedfile, buffers, 0))); /*< schedules write after resize file completes >*/
        
        // Have the compiler config the exact same write gather as earlier for you
        // The compiler assembles an identical sequence of ASIO write gather
        // buffers for you
        std::vector<std::string> buffers2={ "He", "ll", "o ", "Wo", "rl", "d\n" };
        afio::async_io_op written2(dispatcher->write(
            afio::make_async_data_op_req(written, buffers2, 0))); /*< schedules write after previous write completes >*/
        
        // Schedule making sure the previous batch has definitely reached physical storage
        // This won't complete until the write is on disc
        afio::async_io_op stored(dispatcher->sync(written2)); /*< schedules sync after write completes >*/
                
        // Schedule filling this array from the file. Note how convenient std::array
        // is and completely replaces C style char buffer[bytes]
        std::array<char, 12> buffer;
        afio::async_io_op read(dispatcher->read(
            afio::make_async_data_op_req(stored, buffer, 0))); /*< schedules read after sync completes >*/
            
        // Schedule the closing and deleting of example_file.txt after the contents read
        req.precondition=dispatcher->close(read); /*< schedules close file after read completes >*/
        afio::async_io_op deletedfile(dispatcher->rmfile(req)); /*< schedules delete file after close completes >*/
        
        // Wait until the buffer has been filled, checking all steps for errors
        afio::when_all({openfile, resizedfile, written, written2, stored, read}).get(); /*< waits for file open, resize, write, sync and read to complete, throwing any exceptions encountered >*/
        
        // There is actually a async_data_op_req<std::string> specialisation you
        // can use to skip this bit by reading directly into a string ...
        std::string contents(buffer.begin(), buffer.end());
        std::cout << "Contents of file is '" << contents << "'" << std::endl;

        // Check remaining ops for errors
        afio::when_all({req.precondition /*close*/, deletedfile}).get();        /*< waits for file close and delete to complete, throwing any exceptions encountered >*/
        //]
    }
    catch(const BOOST_AFIO_V1_NAMESPACE::system_error &e) { std::cerr << "ERROR: program exits via system_error code " << e.code().value() << " (" << e.what() << ")" << std::endl; return 1; }
    catch(const std::exception &e) { std::cerr << "ERROR: program exits via exception (" << e.what() << ")" << std::endl; return 1; }
    catch(...) { std::cerr << "ERROR: program exits via " << boost::current_exception_diagnostic_information(true) << std::endl; return 1; }
    return 0;
}
