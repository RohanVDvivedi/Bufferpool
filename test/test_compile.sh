cd ..
make clean all
cd test
FILE_NAME="./test.db"
gcc -o test_bpm.out test_bpm.c -I$BUFFER_POOL_MAN_PATH/inc -I$CUTLERY_PATH/inc -I$RWLOCK_PATH/inc -I$BOOMPAR_PATH/inc -L$BUFFER_POOL_MAN_PATH/bin -L$CUTLERY_PATH/bin -L$RWLOCK_PATH/bin -L$BOOMPAR_PATH/bin -lbufferpoolman -lboompar -lpthread -lcutlery -lrwlock
gcc -o test_io.out test_io.c -I$BUFFER_POOL_MAN_PATH/inc -I$CUTLERY_PATH/inc -I$RWLOCK_PATH/inc -I$BOOMPAR_PATH/inc -L$BUFFER_POOL_MAN_PATH/bin -L$CUTLERY_PATH/bin -L$RWLOCK_PATH/bin -L$BOOMPAR_PATH/bin -lbufferpoolman -lboompar -lpthread -lcutlery -lrwlock