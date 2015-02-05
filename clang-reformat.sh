clang-format-3.6 -i include/boost/afio/afio.hpp
#clang-format-3.6 -i include/boost/afio/config.hpp
find include/boost/afio/detail -name *.hpp | xargs clang-format-3.6 -i
find include/boost/afio/detail -name *.ipp | xargs clang-format-3.6 -i
