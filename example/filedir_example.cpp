#include "afio_pch.hpp"

int main(void)
{
    //[filedir_example
    std::shared_ptr<boost::afio::async_file_io_dispatcher_base> dispatcher=
        boost::afio::make_async_file_io_dispatcher();
        
    try
    {
        // Schedule creating a directory called testdir
        auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir",
            boost::afio::file_flags::create)));
        // Schedule creating a file called testfile in testdir only when testdir has been created
        auto mkfile(dispatcher->file(boost::afio::async_path_op_req::relative(mkdir,
            "testfile", boost::afio::file_flags::create)));
        // Schedule creating a symbolic link called linktodir to the item referred to by the precondition
        // i.e. testdir. Note that on Windows you can only symbolic link directories. Note that creating
        // symlinks must *always* be as an absolute path, as that is how they are stored.
        auto mklink(dispatcher->symlink(boost::afio::async_path_op_req::absolute(mkdir,
            "testdir/linktodir", boost::afio::file_flags::create)));

        // Schedule deleting the symbolic link only after when it has been created
        auto rmlink(dispatcher->close(dispatcher->rmsymlink(mklink)));
        // Schedule deleting the file only after when it has been created
        auto rmfile(dispatcher->close(dispatcher->rmfile(mkfile)));
        // Schedule waiting until both the preceding operations have finished
        auto barrier(dispatcher->barrier({rmlink, rmfile}));
        // Schedule deleting the directory only after the barrier completes
        auto rmdir(dispatcher->rmdir(dispatcher->depends(barrier.front(), mkdir)));
        // Check ops for errors
        boost::afio::when_all({mkdir, mkfile, mklink, rmlink, rmfile, rmdir}).wait();
    }
    catch(...)
    {
        std::cerr << boost::current_exception_diagnostic_information(true) << std::endl;
        throw;
    }
    //]
}
