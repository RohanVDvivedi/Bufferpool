cd ..
make clean all
cd test
FILE_NAME="/media/rohan/5699ddb0-77df-47e2-8fd0-1152949f0981/test.db"
gcc -o test_bpm.out test_bpm.c -I$BUFFER_POOL_MAN_PATH/inc -I$CUTLERY_PATH/inc -I$RWLOCK_PATH/inc -L$BUFFER_POOL_MAN_PATH/bin -L$CUTLERY_PATH/bin -L$RWLOCK_PATH/bin -lbufferpoolman -lcutlery -lrwlock
sudo ./test_bpm.out $FILE_NAME
stat $FILE_NAME
stat -f $FILE_NAME
echo "Data written : "
cat $FILE_NAME