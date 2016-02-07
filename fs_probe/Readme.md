This directory contains filing system behaviour testing and results.
Simply compile and run the fs_probe executable with the current directory
in the filing system you are testing. It will append the results into
the fs_probe_results.yaml file.

The results directory contains a YAML database of standard runs of
fs_probe for various OS, storage devices and filing systems. It may or
may not be representative of the same combination on other hardware.

Note that include contains the beginnings of the post-peer-review AFIO
v2 rewrite. It currently has Windows and POSIX AIO async file i/o
backends with file open, close, clone, scatter-gather read/writes and
truncate only.

Todo:
- [ ] Still need to decide if caching belongs in native_handle_type.
- [ ] Add native BSD kqueues to POSIX AIO backend as is vastly more efficient.
  - http://www.informit.com/articles/article.aspx?p=607373&seqNum=4 is a
very useful programming guide for POSIX AIO.
- [ ] Port to Linux KAIO
  - http://linux.die.net/man/2/io_getevents would be in the run() loop.
pthread_sigqueue() can be used by post() to cause aio_suspend() to break
early to run user supplied functions.
- [ ] Add to docs for every API the number of malloc + free performed.
  - Unit test op codes generated per set of i/o calls 
- [ ] Don't run the cpu and sys tests if cpu and sys ids already in fs_probe_results.yaml
  - Need to uniquely fingerprint a machine somehow?
- [ ] C bindings for all AFIO v2 APIs
  - Every .hpp should have a corresponding .h file
  - Can these be auto-generated?
  - Is SWIG worth it?
- [ ] Fatter afio::path. We probably need to allow relative paths
based on a handle and fragment in afio::path, therefore might as well encapsulate
NT kernel vs win32 paths in there too.
- [ ] Add monitoring of CPU usage to tests. See GetThreadTimes. Make sure
worker thread times are added into results.
- [ ] Configurable tracking of op latency and throughput (bytes) for all
handles on some storage i.e. storage needs to be kept in a global map.
  - Something which strongly resembles the memory bandwidth test
  - [ ] Should have decile bucketing e.g. percentage in bottom 10%, percentage
  in next 10% etc. Plus mean and stddev.
  - [ ] Should either be resettable or subtractable i.e. points can be diffed.
  - [ ] Add IOPS QD=1..N storage profile test
  - [ ] Add throughput storage profile test
- [ ] Output into YAML comparable hashes for OS + device + FS + flags
so we can merge partial results for some combo into the results database.
- [ ] Write YAML parsing tool which merges fs_probe_results.yaml into
the results directory where flags and OS get its own directory and each YAML file
is named FS + device e.g.
  - results/win64 direct=1 sync=0/NTFS + WDC WD30EFRX-68EUZN0
- [ ] virtual handle::path_type handle::path(bool refresh=false) should be added using
GetFinalPathNameByHandle(FILE_NAME_OPENED). VOLUME_NAME_DOS vs VOLUME_NAME_NT should
depend on the current afio::path setting.
- [ ] directory_handle
- [ ] symlink_handle
- [ ] Missing functions on handle/file_handle from AFIO v1
