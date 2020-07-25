# bufferpoolman
It is an implementation of a Buffer Pool Manager library in C (like Linux page cache, but in user-space), used for accessing pages of a Heap File from a HDD/SSD and caching it, by using well defined eviction policy for replacement.

**A Heap File** is a file of unordered fixed sized pages, where each page is identified using a 32 bit integer but the integer itself does not reveal anything about the actual location of the page in the file.

 * bufferpoolman is not itself a database storage engine although it can be used to build a database storage engine.
 * A very simple linkedlist based actual LRU Policy is implemented to evict the pages for replacement.
 * You may specifically use MRU policy for a particular set of pages, which can be helpfull, when you are performing a sequential scan.
 * The bufferpool man also provides a synchronous queue based access policy, which when used will result in piggy-backing page accesses, which can be helpfull if you want synchronous sequential scans.
 * bufferpoolman does not provide any restriction on the schema that you use to store your data. The pages are your blank slate.
 * bufferpoolman does not impose any restriction on the size of the page you wish to use for your heap file but the page size must be a multiple of the physical block size of the disk. It is recommended to keep the page size equal to file system block size to avoid any unsuspected issues.
 * To use this project on ext2/ext3/ext4 filesystems, turn off data journaling on the respective filesystem/partition (because Direct I/O and syncing writes are used by the project).

setup instructions
 * git clone https://github.com/RohanVDvivedi/bufferpoolman.git
 * cd bufferpoolman
 * sudo make clean install
 * add "-lbufferpoolman -lboompar -lrwlock -lpthread -lcutlery" linker flag, while compiling your application
