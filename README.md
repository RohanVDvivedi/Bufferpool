# Bufferpool
It is an implementation of a Buffer Pool Manager library in C, used for accessing (with Read/Write lcoks) and caching pages of secondary memory (SSD/HDD).

 * It primarily solves 2 problems
   * Caching disk pages in a maximum number of page frames, and evicting them as necessary using an LRU policy (Actual LRU not clock based LRU) for replacement.
   * Databases need page level latches (mutexes in database terminology) on pages, while modifying individual disk pages, **this library provides you an api to latch pages that have reader-writer lock like api and functionality**.
 * Additionally,
   * It provides plenty of flags with an extensive api to acquire read/write locks on pages, upgrading read lock to write lock on a page, downgrade write lock to read lock on a page and release read/write lock on page, gives wide flexibility to build your storage engine on top of this bufferpool.
   * This library allows you to lock (traditionally called latch in Database terminology) pages with reader and writer lock while accessing them, allowing higher concurrency to your database storage engine.
   * It also allows you to prefetch pages, both synchronously and asynchronously.
   * It supports periodic jobs that can be run in background to flush dirty pages to disk, keeping your bufferpool pages clean.
   * It does not perfrom I/O for you, but instead takes structure of call back functions that get called to perform I/O, allowing you to intercept read and write I/O calls.
   * This also allows you to switch the underlying storage from (conventionally) SSD/HDD to some network storage accessed over TCP/UDP, very easily.

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
 * add `-lbufferpool -lrwlock -lboompar -lpthread -lcutlery` linker flag, while compiling your application
 * do not forget to include appropriate public api headers as and when needed. this includes
   * `#include<bufferpool.h>` -> read this to get the gist of the API
   * `#include<page_io_ops.h>` -> defines the structure of read/write I/O callback function struct

## Instructions for uninstalling library

**Uninstall :**
 * `cd Bufferpool`
 * `sudo make uninstall`
