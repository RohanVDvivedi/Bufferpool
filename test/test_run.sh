TEST_TYP=$1
FILENAME=$2
echo "TEST_TYP = $TEST_TYP"
echo "FILENAME = $FILENAME"
if [ $TEST_TYP = "bufferpool" ]
then
	sudo time -v ./test_bpm.out $FILENAME
	sudo stat $FILENAME
	sudo stat -f $FILENAME
	sudo cat -c `expr 512 \* 8 \* 20` $FILENAME
fi