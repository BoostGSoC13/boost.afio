This directory contains filing system behaviour testing and results.
Simply compile and run the fs_probe executable with the current directory
in the filing system you are testing. It will append the results into
the fs_probe_results.yaml file.

The results directory contains a YAML database of standard runs of
fs_probe for various OS, storage devices and filing systems. It may or
may not be representative of the same combination on other hardware.

Todo:
- [ ] C bindings for all AFIO v2 APIs
  - Every .hpp should have a corresponding .h file
- [ ] Add monitoring of CPU usage to tests. See GetThreadTimes. Make sure
worker thread times are added into results.
- [ ] Output into YAML comparable hashes for OS + device + FS + flags
so we can merge partial results for some combo into the results database.
- [ ] Write YAML parsing tool which merges fs_probe_results.yaml into
the results directory where flags and OS get its own directory and each YAML file
is named FS + device e.g.
  - results/win64 direct=1 sync=0/NTFS + WDC WD30EFRX-68EUZN0
