# Bufferpool
It is an implementation of a Buffer Pool Manager library in C (like Linux page cache, but in user-space), used for accessing pages of a Heap File (or directly a partition on raw disk) from a HDD/SSD and caching it, by using well defined eviction policy for replacement.

**A Heap File** is a file of unordered fixed sized pages, where each page is identified using a 32 bit integer but the integer itself does not reveal anything about the actual location of the page in the file.

 * "Bufferpool" is not itself a database storage engine although it can be used to build a database storage engine.
 * A very simple linkedlist based actual LRU Policy (not a clock LRU algorithm) is implemented to evict the pages for replacement.
 * You may specifically use MRU policy for a particular access of a page, which can be helpfull, when you are performing a sequential scan.
 * The bufferpool man also provides a synchronous queue based access policy, which when used will result in piggy-backing page accesses, which can be helpful if you are performing multiple concurrent sequential scans (scan-sharing).
 * "Bufferpool" does not provide any restriction on the schema that you use to store your data. Its pages are your blank slate.
 * "Bufferpool" does not impose any restriction on the size of the page you wish to use for your heap file but the page size must be a multiple of the physical block size of the disk. It is recommended to keep the page size equal to file system block size to avoid any unsuspected issues.
 * To use this project on raw ext3/ext4 filesystems, you may need to turn off data journaling on the respective filesystem partition because (using O_DIRECT flag) direct I/O and syncing writes (immediately flushing pages) are used (and expected) by the project.

## Setup instructions
**Install dependencies :**
 * [Cutlery](https://github.com/RohanVDvivedi/Cutlery)
 * [BoomPar](https://github.com/RohanVDvivedi/BoomPar)
 * [ReaderWriterLock](https://github.com/RohanVDvivedi/ReaderWriterLock)

**Download source code :**
 * `git clone https://github.com/RohanVDvivedi/Bufferpool.git`

**Build from source :**
 * `cd Bufferpool`
 * `make clean all`

**Install from the build :**
 * `sudo make install`
 * ***Once you have installed from source, you may discard the build by*** `make clean`

## Using The library
 * add `-lbufferpool -lboompar -lrwlock -lpthread -lcutlery` linker flag, while compiling your application
 * do not forget to include appropriate public api headers as and when needed. this includes
   * `#include<bufferpool.h>`

## Instructions for uninstalling library

**Uninstall :**
 * `cd Bufferpool`
 * `sudo make uninstall`
