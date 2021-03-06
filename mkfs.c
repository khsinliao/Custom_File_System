/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
	struct a1fs_superblock *sb = (struct a1fs_superblock *)(image);
	if (sb->magic != A1FS_MAGIC){
		return false;
	}
	return true;
}


int cal_num_block (int a , int b){
	int tmp = 0;
	if ((a / b)==0) tmp = a/b;
	else tmp = a/b + 1;
	return tmp;
}

/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */

static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO: initialize the superblock and create an empty root directory
	//NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	unsigned int num_inode = opts->n_inodes;
	unsigned int num_all_block = size / A1FS_BLOCK_SIZE;

	unsigned int inode_per_block = A1FS_BLOCK_SIZE / sizeof(a1fs_inode) ;
	unsigned int num_block_inode_table =  cal_num_block(num_inode , inode_per_block) ;
	unsigned int num_block_inode_bitmap = cal_num_block(num_inode , A1FS_BLOCK_SIZE);

	unsigned int left_blocks = num_all_block - 1 - num_block_inode_bitmap - num_block_inode_table;
	if (left_blocks < 2) return false;

	unsigned int num_block_blk_bitmap = cal_num_block(left_blocks , A1FS_BLOCK_SIZE) ;
    num_block_blk_bitmap -= num_block_blk_bitmap / A1FS_BLOCK_SIZE ;

	unsigned int num_reserve_block = 1 + num_block_inode_table + num_block_inode_bitmap + num_block_blk_bitmap ;
	unsigned int num_free_block = num_all_block - num_reserve_block;

	//a1fs_blk_t super_block = 0;
	a1fs_blk_t data_block_bitmap = 1;
	a1fs_blk_t inode_bitmap = data_block_bitmap + num_block_blk_bitmap;
	a1fs_blk_t inode_table = inode_bitmap + num_block_inode_bitmap;
	a1fs_blk_t data_block = inode_table + num_block_inode_table;

	a1fs_superblock *sb = (a1fs_superblock *)(image);
    
	sb -> magic = A1FS_MAGIC ; 
	sb -> size = size ;
	sb -> inode_count = num_inode;
	sb -> block_count = num_all_block;
	//sb -> reserve_inode_count = 0;
	sb -> free_inode_count = num_inode;
	//sb -> reserve_block_count = num_reserve_block;
	sb -> free_block_count = num_free_block;
	sb -> data_bitmap = data_block_bitmap;
	sb -> inode_bitmap = inode_bitmap;
	sb -> inode_table = inode_table;
	sb -> first_data = data_block;

	// Init directory
	unsigned char *inode_bitmap_str = image + (sb -> inode_bitmap * A1FS_BLOCK_SIZE);
	unsigned char *data_bitmap_str = image + (sb -> data_bitmap * A1FS_BLOCK_SIZE);
	memset(inode_bitmap_str , 0 , num_block_inode_bitmap *A1FS_BLOCK_SIZE);
	memset(data_bitmap_str , 0 , num_block_blk_bitmap *A1FS_BLOCK_SIZE);
	inode_bitmap_str[0] = 1 << 7; 

	a1fs_inode *first_inode = image + (inode_table * A1FS_BLOCK_SIZE);

	first_inode -> mode = S_IFDIR | 0777;
	first_inode -> size = 0;
	first_inode -> links = 2;
	clock_gettime(CLOCK_REALTIME, &(first_inode->mtime));
	first_inode -> extent_table = -1; //if file is empty point to -1
	first_inode -> extent_count = 0;

	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
