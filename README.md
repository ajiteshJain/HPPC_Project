HPPC_Project
============
PIM Project
Contains PIN Tools and USIM

To Run the PIN tool the following steps are required:
1) Download relevant source code for running the tool from https://software.intel.com/en-us/articles/pin-a-dynamic-binary-instrumentation-tool

2) Copy buffer_filter.cpp to ./pintools/pin-2.14-67254-gcc.4.4.7-linux/source/tools/MyPinTool directory.
   Add the file in makefile.rules and do make.
   This should generate buffer_filter.so in obj-intel64 folder.
3) To run the tool for collecting the traces run the following command in the obj-intel64 directory:
../../../../pin.sh -t buffer_filter.so -filter_rtn <mangled function names> -- <binary name>
The function names mentioned in the command are the functions for which the traces need to be collected.

After running this command, the PIN tool will create buffer_filter.out.[1-9]+ files in the same directory.

Running USIMM

1) To run the traces in USIM, rename the files collected from PIN as t0,t1 etc corresponding to different threads.
2) Add these files in usim/input/threads folder.
3) Make configuration changes in input/hmc.cfg files adjusting the channels, banks, rows etc for desired memory.
4) Goto to simscript directory and run ./runall.sh. This should generate output in output directory with name 'thread'.
