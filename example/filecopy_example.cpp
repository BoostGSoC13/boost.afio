#include "afio_pch.hpp"

//[filecopy_example
namespace {
    using namespace boost::afio;
    using boost::afio::off_t;

    // Parallel copy files in sources into dest, concatenating
    future<std::vector<std::shared_ptr<async_io_handle>>> async_concatenate_files(
        atomic<off_t> &written, off_t &totalbytes,
        std::shared_ptr<async_file_io_dispatcher_base> dispatcher,
        boost::afio::filesystem::path dest, std::vector<boost::afio::filesystem::path> sources,
        size_t chunk_size=1024*1024 /* 1Mb */)
    {
        // A special allocator of highly efficient file i/o memory
        typedef std::vector<char, utils::file_buffer_allocator<char>> file_buffer_type;
        // Schedule the opening of the output file for writing
        auto oh=dispatcher->file(async_path_op_req(dest, file_flags::Create|file_flags::Write));
        // Schedule the opening of all the input files for reading
        std::vector<async_path_op_req> ihs_reqs; ihs_reqs.reserve(sources.size());
        for(auto &&source : sources)
            ihs_reqs.push_back(async_path_op_req(source, file_flags::Read
            |file_flags::WillBeSequentiallyAccessed));
        auto ihs=dispatcher->file(ihs_reqs);
        // Retrieve any error from opening the output
        when_all(oh).wait();
        // Wait for the input file handles to open so we can get their sizes
        // (plus any failures to open)
        when_all(ihs).wait();

        // Need to figure out the sizes of the sources so we can resize output
        // correctly. We also need to allocate scratch buffers for each source.
        std::vector<std::tuple<off_t, off_t, std::unique_ptr<file_buffer_type>>> offsets;
        offsets.reserve(ihs.size());
        off_t offset=0;
        for(auto &ih : ihs)
        {
            // Get the file's size in bytes
            off_t bytes=ih.get()->direntry(metadata_flags::size).st_size();
            // Push the offset to write at, amount to write, and a scratch buffer
            offsets.push_back(std::make_tuple(offset, bytes,
                detail::make_unique<file_buffer_type>(chunk_size)));
            offset+=bytes;
        }
        // Schedule resizing output to correct size, retrieving errors
        totalbytes=offset;
        auto ohresize=dispatcher->truncate(oh, offset);
        when_all(ohresize).wait();

        // Schedule the parallel processing of all input files
        std::vector<async_io_op> writes(ihs.size());
//#pragma omp parallel for // optional, actually makes little difference
        for(int idx=0; idx<(int) ihs_reqs.size(); idx++)
        {
            async_io_op last=ihs[idx];
            auto offset=std::get<0>(offsets[idx]), bytes=std::get<1>(offsets[idx]);
            auto &buffer=std::get<2>(offsets[idx]);
            for(off_t o=0; o<bytes; o+=chunk_size)
            {
                size_t thischunk=(size_t)(bytes-o);
                if(thischunk>chunk_size) thischunk=chunk_size;
                // Schedule a filling of buffer from offset o after last has completed
                auto readchunk=dispatcher->read(make_async_data_op_req(last, buffer->data(),
                    thischunk, o));
                // Schedule a writing of buffer to offset offset+o after readchunk is ready
                auto writechunk=dispatcher->write(make_async_data_op_req(readchunk,
                    buffer->data(), thischunk, offset+o));
                // Schedule incrementing written after write has completed
                auto incwritten=dispatcher->call(writechunk, [&written, thischunk]{
                    written+=thischunk;
                });
                // Don't do next read until written is incremented
                last=incwritten.second;
            }
            // Send write completion to writes for later synchronisation
            writes[idx]=last;
        }
        // Having scheduled all the reads and write, return a future which returns when
        // they're done
        return when_all(writes);
    }
}

int main(int argc, const char *argv[])
{
    using namespace boost::afio;
    using boost::afio::off_t;
    typedef chrono::duration<double, ratio<1, 1>> secs_type;
    if(argc<3)
    {
        std::cerr << "ERROR: Need to specify destination path and source paths"
            << std::endl;
        return 1;
    }        
    try
    {
        atomic<off_t> written(0);
        off_t totalbytes=0;
        std::shared_ptr<boost::afio::async_file_io_dispatcher_base> dispatcher=
            boost::afio::make_async_file_io_dispatcher();

        boost::afio::filesystem::path dest=argv[1];
        std::vector<boost::afio::filesystem::path> sources;
        std::cout << "Concatenating into " << dest << " the files ";
        for(int n=2; n<argc; argc++)
        {
            sources.push_back(argv[n]);
            std::cout << sources.back();
            if(n<argc-1) std::cout << ", ";
        }
        std::cout << " ..." << std::endl;

        auto begin=chrono::steady_clock::now();
        auto h=async_concatenate_files(written, totalbytes, dispatcher, dest, sources);
        // Print progress once a second until it's done
        while(future_status::timeout==h.wait_for(boost::afio::chrono::seconds(1)))
        {
            std::cout << "\r" << (100*written)/totalbytes << "% complete (" << written
                << " out of " << totalbytes << " @ " << (written/chrono::duration_cast<secs_type>(
                    chrono::steady_clock::now()-begin).count()/1024/1024) << "Mb/sec) ...";
        }
        std::cout << std::endl;
    }
    catch(...)
    {
        std::cerr << "ERROR: " << boost::current_exception_diagnostic_information(true)
            << std::endl;
        return 1;
    }
    return 0;
}
//]
