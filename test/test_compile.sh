cd ..
make clean all
cd test
stat -f "block_count=%b,  block_size=%k, size=%z" test.db 
gcc -o test_page_entry.out test_page_entry.c -I$BUFFER_POOL_MAN_PATH/inc -I$CUTLERY_PATH/inc -I$RWLOCK_PATH/inc -L$BUFFER_POOL_MAN_PATH/bin -lbufferpoolman -L$RWLOCK_PATH/bin -lrwlock
./test_page_entry.out
stat -f "block_count=%b,  block_size=%k, size=%z" test.db 