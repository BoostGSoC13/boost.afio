#include "afio_pch.hpp"

int main(void)
{
    //[statfs_example
    boost::afio::current_dispatcher_guard h(boost::afio::make_dispatcher());
    
    // Schedule an opening of the root directory
    boost::afio::future<> rootdir(boost::afio::async_dir("/")());

    // Ask the filing system of the root directory how much free space there is
    boost::afio::statfs_t statfs(rootdir.then(async_statfs(
        boost::afio::fs_metadata_flags::bsize|boost::afio::fs_metadata_flags::bfree)).get());
    
    std::cout << "Your root filing system has "
        << (statfs.f_bfree*statfs.f_bsize/1024.0/1024.0/1024.0) << " Gb free." << std::endl;
    //]
}
