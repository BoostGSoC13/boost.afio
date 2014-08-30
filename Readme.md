<p align="center">
<a href="http://boostgsoc13.github.io/boost.afio/">Documentation can be found here</a>
</p>
<h3 align="center">
Boost.AFIO Jenkins CI status:
</h3>
<p align="center">Unit test code coverage is: <a href='https://coveralls.io/r/BoostGSoC13/boost.afio'><img src='https://coveralls.io/repos/BoostGSoC13/boost.afio/badge.png' alt='Coverage Status' /></a></p>
<p align="center">Current master branch build and unit tests status on Travis CI: <a href="https://travis-ci.org/BoostGSoC13/boost.afio"><img valign="middle" src="https://travis-ci.org/BoostGSoC13/boost.afio.png?branch=master"/></a></p>

<center>
<table border="1" cellpadding="2">
<tr><th>OS</th><th>Compiler</th><th>STL</th><th>CPU</th><th>Build</th><th>Unit tests</th></tr>

<!-- static analysis clang -->
<tr align="center"><td rowspan="2">Static analysis</td><td>clang 3.4</td><td></td><td></td><td>
<div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Static%20Analysis%20clang/'><img src='https://ci.nedprod.com/buildStatus/icon?job=Boost.AFIO%20Static%20Analysis%20clang' style="margin-left:-58px;" /></a></div></td><td></td>
</tr>

<!-- static analysis MSVC -->
<tr align="center"><td>VS2013</td><td></td><td></td><td>
<div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Static%20Analysis%20MSVC/'><img src='https://ci.nedprod.com/buildStatus/icon?job=Boost.AFIO%20Static%20Analysis%20MSVC' style="margin-left:-58px;" /></a></div></td><td></td>
</tr>

<!-- sanitiser -->
<tr align="center"><td>Thread Sanitiser</td><td>clang 3.4</td><td>libstdc++ 4.9</td><td>x64</td><td></td><td>
<div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Sanitise%20Linux%20clang%203.4/'><img src='https://ci.nedprod.com/buildStatus/icon?job=Boost.AFIO%20Sanitise%20Linux%20clang%203.4' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- valgrind -->
<tr align="center"><td>Valgrind</td><td>GCC 4.8</td><td>libstdc++ 4.8</td><td>x64</td><td></td><td>
<div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Valgrind%20Linux%20GCC%204.8/'><img src='https://ci.nedprod.com/buildStatus/icon?job=Boost.AFIO%20Valgrind%20Linux%20GCC%204.8' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- sep -->
<tr><td></td></tr>

<!-- clang 3.3 x86 -->
<tr align="center"><td>FreeBSD 10 on ZFS</td><td>clang 3.3</td><td>libc++</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_FreeBSD_clang%203.3'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_FreeBSD_clang%203.3/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_FreeBSD_clang%203.3'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_FreeBSD_clang%203.3/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- clang 3.2 x86 -->
<tr align="center"><td rowspan="4">Ubuntu Linux 12.04 LTS</td><td>clang 3.2</td><td>libstdc++ 4.8</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_clang%203.2'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_clang%203.2/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_clang%203.2'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_clang%203.2/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- clang 3.3 x86 -->
<tr align="center"><td>clang 3.3</td><td>libstdc++ 4.8</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_clang%203.3'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_clang%203.3/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_clang%203.3'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_clang%203.3/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- GCC 4.6 x86 -->
<tr align="center"><td>GCC 4.6</td><td>libstdc++ 4.6</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_GCC%204.6'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_GCC%204.6/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_GCC%204.6'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_GCC%204.6/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- GCC 4.7 x86 -->
<tr align="center"><td>GCC 4.7</td><td>libstdc++ 4.7</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_GCC%204.7'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux32_GCC%204.7/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_GCC%204.7'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux32_GCC%204.7/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>



<!-- clang 3.5 ARMv7a -->
<tr align="center"><td rowspan="5">Ubuntu Linux 14.04 LTS</td><td>clang 3.5</td><td>libstdc++ 4.8</td><td>ARMv7a</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_ARM_clang%203.4'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_ARM_clang%203.4/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_ARM_clang%203.4'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_ARM_clang%203.4/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- clang 3.4 x64 -->
<tr align="center"><td>clang 3.4</td><td>libstdc++ 4.9</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_clang%203.4'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_clang%203.4/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_clang%203.4'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_clang%203.4/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- GCC 4.8 x64 -->
<tr align="center"><td>GCC 4.8</td><td>libstdc++ 4.8</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_GCC%204.8'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_GCC%204.8/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_GCC%204.8'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_GCC%204.8/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- GCC 4.8 x64 -->
<tr align="center"><td>GCC 4.8</td><td>libstdc++ 4.8</td><td>ARMv7a</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_ARM_GCC%204.8'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_ARM_GCC%204.8/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_ARM_GCC%204.8'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_ARM_GCC%204.8/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- GCC 4.9 x64 -->
<tr align="center"><td>GCC 4.9</td><td>libstdc++ 4.9</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_GCC%204.9'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20POSIX_Linux64_GCC%204.9/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_GCC%204.9'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20POSIX_Linux64_GCC%204.9/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>



<!-- VS2010 -->
<tr align="center"><td rowspan="5">Microsoft Windows 8.1</td><td colspan="2">VS2010</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2010'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2010/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2010'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2010/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- VS2012 -->
<tr align="center"><td colspan="2">VS2012</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2012'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2012/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2012'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2012/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- VS2013 -->
<tr align="center"><td colspan="2">VS2013</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2013'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_VS2013/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2013'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_VS2013/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- MinGW -->
<tr align="center"><td>MinGW GCC 4.8</td><td>libstdc++ 4.8</td><td>x86</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_Mingw32'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_Mingw32/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_Mingw32'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_Mingw32/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>

<!-- MinGW64 -->
<tr align="center"><td>MinGW-w64 GCC 4.9</td><td>libstdc++ 4.9</td><td>x64</td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_Mingw64'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Build%20NT_Win64_Mingw64/badge/icon' style="margin-left:-58px;" /></a></div></td><td><div style="position:relative; width:42px; overflow:hidden;"><a href='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_Mingw64'><img src='https://ci.nedprod.com/job/Boost.AFIO%20Test%20NT_Win64_Mingw64/badge/icon' style="margin-left:-58px;" /></a></div></td>
</tr>
</table>

</center>
