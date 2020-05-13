#include<dbfile.h>

#include<time.h>

double diff_timespec(struct timespec tstart, struct timespec tend)
{
	return ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);
}

int main(int argc, char **argv)
{
	printf("\n\ntest started\n\n");

	char filename[512] = "./test.db";
	uint32_t block_count = 8;
	uint32_t blocks_per_page = 8;
	if(argc == 4)
	{
		strcpy(filename, argv[1]);
		sscanf(argv[2], "%u", &block_count);
		sscanf(argv[3], "%u", &blocks_per_page);
	}
	else
	{
		printf("test FAILED please enter file name, block count for testing io and number of blocks per page\n\n\n");
		return -1;
	}

	if(block_count == 0 || blocks_per_page == 0)
	{
		printf("test FAILED please enter positive integers for block_count and blocks_per_page\n\n");
		return -1;
	}

	dbfile* dbfilep = open_dbfile(filename);
	if(dbfilep == NULL)
	{
		dbfilep = create_dbfile(filename);
		if(dbfilep == NULL)
		{
			printf("test FAILED could not open/create database file at the given path\n\n\n");
			return -1;
		}
	}

	printf("Test Conditions\n \t filename : %s\n \t block_count : %u\n \t blocks_per_page : %u\n\n", filename, block_count, blocks_per_page);

	int64_t bytes_op = 0;
	void* alloc_memory = malloc(get_block_size(dbfilep) * (block_count + 1));
	void* blocks_in_main_memory = (void*)(((((uintptr_t)alloc_memory) / get_block_size(dbfilep)) + 1) * get_block_size(dbfilep));
	struct timespec start_time, end_time;

	printf("\n\n");

	printf("Test 1 : Complete sequential write from massive in-memory buffer\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	bytes_op = write_blocks_to_disk(dbfilep, blocks_in_main_memory, 0, block_count);
	clock_gettime(CLOCK_MONOTONIC, &end_time);;
	printf("Result 1 : written %ld bytes in %.10lf time\n\n", bytes_op, diff_timespec(start_time, end_time));

	// this is effectively random io
	printf("Test 2 : Complete sequential page by page write using multiple write io calls from massive in-memory buffer -> effectively random\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	bytes_op = 0;
	for(uint32_t start_block = 0, end_block = blocks_per_page - 1; end_block < block_count; start_block += blocks_per_page, end_block += blocks_per_page)
	{
		bytes_op += write_blocks_to_disk(dbfilep, blocks_in_main_memory, start_block, blocks_per_page);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);;
	printf("Result 2 : written %ld bytes in %.10lf time\n\n", bytes_op, diff_timespec(start_time, end_time));

	printf("Test 3 : Complete sequential read to massive in-memory buffer\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	bytes_op = read_blocks_from_disk(dbfilep, blocks_in_main_memory, 0, block_count);
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("Result 3 : read %ld bytes in %.10lf seconds time\n\n", bytes_op, diff_timespec(start_time, end_time));

	// This is what effective random io
	printf("Test 4 : Complete sequential page by page read using multiple read io calls to massive in-memory buffer -> effectively random\n");
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	bytes_op = 0;
	for(uint32_t start_block = 0, end_block = blocks_per_page - 1; end_block < block_count; start_block += blocks_per_page, end_block += blocks_per_page)
	{
		bytes_op += read_blocks_from_disk(dbfilep, blocks_in_main_memory, start_block, blocks_per_page);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);
	printf("Result 4 : read %ld bytes in %.10lf seconds time\n\n", bytes_op, diff_timespec(start_time, end_time));

	close_dbfile(dbfilep);
	free(alloc_memory);

	printf("test completed\n\n\n");
	return 0;
}