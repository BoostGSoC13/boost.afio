#include "afio_pch.hpp"

int main(void)
{
  //[statfs_example
  std::shared_ptr<boost::afio::async_file_io_dispatcher_base> dispatcher =
  boost::afio::make_async_file_io_dispatcher();

  // Schedule an opening of the root directory
  boost::afio::async_io_op rootdir(
  dispatcher->dir(boost::afio::async_path_op_req("/")));

  // Ask the filing system of the root directory how much free space there is
  boost::afio::statfs_t statfs(
  dispatcher->statfs(rootdir, boost::afio::fs_metadata_flags::bsize |
                              boost::afio::fs_metadata_flags::bfree).first.get());

  std::cout << "Your root filing system has "
            << (statfs.f_bfree * statfs.f_bsize / 1024.0 / 1024.0 / 1024.0)
            << " Gb free." << std::endl;
  //]
}
