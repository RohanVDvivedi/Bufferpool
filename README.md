# bufferpoolman
This projects is a Heap File based Buffer Pool Manager implementation for a HDD/SSD oriented Database Storage Engine

* bufferpoolman is not itself a database storage engine.
* bufferpoolman only provides you with primitives to cache and manage a heap file, to build a database storage engine.
* remember heap file is a file of unordered pages, each page is identified using a 32 bit integer, but the integer does not reveal anything about the actual location of the file.
* bufferpoolman does not provide any restriction on the schema you use to store your data. The pages are your blank slate.
* bufferpoolman does not restrict the size of the page you wish to use for the given heap file. It is recommended to keep the page size same as the page size supported by your hardware.
* this primitives include :
  * creating a buffer pool manager for the heap file, with the maximum number of pages to keep cached, as its input parameter. 
  * creating a heap file, on the disk using the heap file manager for a given page size.
  * reading/(forced)writing a page from heap file.
  * creating a blank page in the heap file.
  * manage a directory of pages for the heap file.
  * manage a cache of a few frequently requested pages in memory.
  * also separately, manage a cache of the directory pages of the heap file.
