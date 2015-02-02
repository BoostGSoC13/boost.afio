#!/usr/bin/python3

inputs=[
  (("Android 5.0", "GCC 4.8", "libc++", "x86"), {"CPPSTD":"c++11", "CXX":"g++-4.8", "label":"android-ndk"})
]
variations={
  "CPPSTD":("c++11", "c++14"),
  "CXX":("g++-4.7", "g++-4.8", "g++-4.9"),
  "LINKTYPE":("static", "shared", "standalone")
}

# <a href='https://ci.nedprod.com/job/Boost.AFIO%20Build/CPPSTD=c++11,CXX=g++-4.8,LINKTYPE=static,label=android-ndk/'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build/CPPSTD=c++11,CXX=g++-4.8,LINKTYPE=static,label=android-ndk/badge/icon'></a>

for line, items in inputs:
  print('<tr align="center"><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td><div>' % line)
  
