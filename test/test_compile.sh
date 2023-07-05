gcc -o test_bpm.out test_bpm.c blockfile_page_io_ops_test_util.c -lblockio -lbufferpool -lrwlock -lboompar -lpthread -lcutlery

gcc -o test_bpm_rnu.out test_bpm_rnu.c blockfile_page_io_ops_test_util.c -lblockio -lbufferpool -lrwlock -lboompar -lpthread -lcutlery