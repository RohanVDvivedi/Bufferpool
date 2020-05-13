echo "TEST_TYP = $1"
echo "FILENAME = $2"
TEST_TYP=$1
FILENAME=$2
if [ $TEST_TYP = "bufferpool" ]
then
	sudo time -v ./test_bpm.out $FILENAME
	sudo stat $FILENAME
	sudo stat -f $FILENAME
	sudo head -c `expr 512 \* 8 \* 2` $FILENAME
elif [ $TEST_TYP = "io_speed" ]
then
	sudo time -v ./test_io.out $FILENAME 160 8
	sudo time -v ./test_io.out $FILENAME 640 8
	echo "1. Even on SSD sequential io is slower"
	echo "2. With larger amount of data for io, the difference in sequential and random io increases"
	echo "3. Always try to make fewer and bigger read/write calls for disk and execution effeciency"
fi