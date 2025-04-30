# Trace Emulator

The trace emulator is composed of three customizable components, that together, enable the reading of any trace format, and its emulation on any storage system.

* Trace reader: the trace reader reads a trace file and for each row, returns a request object outlining the requirements of a file access, notably, the timestamp of the request, the type of access, name of the file accessed, and the number of bytes manipulated by the operation.
* Emulation core: the emulation core is responsible for submitting requests to emulation nodes following their respective timestamps. For each timestamp in the trace, the emulation core spawns a thread pool of emulation nodes depending on how many concurrent requests share the same time stamp. With this design, we guarantee that requests are first, executed sequentially based on their timestamps, and second, that requests with the same timestamps are executed concurrently.
Furthermore, the emulation core records the durations returned by emulation nodes in a csv file with each row denoting the filename, operation name, bytes modified and duration of the operation.
* Emulation node: the emulation node is a customizable interface that specifies both a read and a write operation. Emulation nodes are also responsible for returning the duration in microseconds taken to execute the operation. Lastly, since emulation nodes are implemented as threads, we might face issues when there are too concurrent requests spawning too many threads.

The current implemented backends are:
  -  Ceph with directory splitting 
  -  Ceph O_direct
  -  SLSFS

For help on which arguments, please run compile and run trace-emulator.cpp with the no arguments
