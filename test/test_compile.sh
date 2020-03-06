cd ..
make clean all
cd test
stat -f "block_count=%b,  block_size=%k, size=%z" test.db 
gcc -o test_bpm.out test_bpm.c -I$BUFFER_POOL_MAN_PATH/inc -I$CUTLERY_PATH/inc -I$RWLOCK_PATH/inc -L$BUFFER_POOL_MAN_PATH/bin -L$CUTLERY_PATH/bin -L$RWLOCK_PATH/bin -lbufferpoolman -lcutlery -lrwlock
./test_bpm.out
stat -f "block_count=%b,  block_size=%k, size=%z" test.db 
echo "Data written : "
cat ./test.db