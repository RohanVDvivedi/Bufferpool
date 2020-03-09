# bufferpoolman
This project is an implementation of a Buffer Pool Manager used for managing access to a Heap File from a HDD/SSD and caching it as needed using Least Recently Used cache-page eviction policy.

* bufferpoolman is not itself a database storage engine although it can be used to build a database storage engine.
* A very simple linkedlist based Policy is implemented to evict the Least Recently Used pages first.
* The Heap File, a file of unordered pages, where each page is identified using a 32 bit integer but the integer does not reveal anything about the actual location of the page.
* bufferpoolman does not provide any restriction on the schema you use to store your data. The pages are your blank slate.
* bufferpoolman does not impose any restriction on the size of the page you wish to use for your heap file but the page size must be a multiple of the physical block size of the disk. It is recommended to keep the page size equal to file system block size to avoid any unsuspected issues.
* To use this project on ext2/ext3/ext4 filesystems, turn off data journaling on the respective filesystem/partition (because Direct I/O and syncing writes are used by the project).