#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "wfs.h"

// Round `num` up to the nearest multiple of `factor`
int roundup(int num, int factor) {
    return num % factor == 0 ? num : num + (factor - (num % factor));
}

// Fill in the superblock and check that the requested layout fits in the image
int setup_sb(struct wfs_sb* sb, int inodes, int blocks, size_t sz) {
    // Make inode and block counts multiples of 32 so the bitmaps stay word-aligned
    inodes = roundup(inodes, 32);
    blocks = roundup(blocks, 32);
    
    sb->num_inodes      = inodes;
    sb->num_data_blocks = blocks;

    // Layout:
    // [ superblock ][ inode bitmap ][ data bitmap ][ inodes ][ data blocks ]
    sb->i_bitmap_ptr = sizeof(struct wfs_sb);
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + (inodes / 8);     // inode bitmap size in bytes
    sb->i_blocks_ptr = sb->d_bitmap_ptr + (blocks / 8);     // data bitmap size in bytes
    sb->d_blocks_ptr = sb->i_blocks_ptr + (inodes * BLOCK_SIZE);

    printf("trying to create with %d inodes, %d blocks, size is %ld, block start at %ld\n",
           inodes, blocks, (long)sz, (long)sb->i_blocks_ptr);

    // Make sure the whole filesystem layout fits inside the disk image
    size_t need = (size_t)sb->i_blocks_ptr +
                  (size_t)inodes * BLOCK_SIZE +
                  (size_t)blocks * BLOCK_SIZE;
    return need < sz;
}

// Build a fresh filesystem on top of the given disk image
int wfs_mkfs(char* path, int inodes, int blocks) {
    int fd;
    struct stat statb;
    struct wfs_sb sb;

    // Open the disk image for read/write
    if ((fd = open(path, O_RDWR, S_IRWXU)) < 0) {
        perror("open failed create metadata\n");
        return -1;
    }

    // Find out how big the disk image is
    if (fstat(fd, &statb) < 0) {
        perror("stat-ing diskimg\n");
        close(fd);
        return -1;
    }

    // Initialize the superblock and verify that it fits
    if (setup_sb(&sb, inodes, blocks, statb.st_size) == 0) {
        printf("too many blocks requested, failed to write superblock\n");
        close(fd);
        return -1;
    }

    // Superblock goes at offset 0
    if (lseek(fd, 0, SEEK_SET) < 0) {
        perror("lseek superblock\n");
        close(fd);
        return -1;
    }

    if (write(fd, &sb, sizeof(struct wfs_sb)) < 0) {
        perror("writing superblock\n");
        close(fd);
        return -1;
    }

    // Set up the root inode (inode 0) in memory before writing it out
    struct wfs_inode inode;
    memset(&inode, 0, sizeof(struct wfs_inode));

    inode.num    = 0;
    inode.mode   = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;  // root is a directory, user rwx
    inode.uid    = getuid();
    inode.gid    = getgid();
    inode.size   = 0;
    inode.nlinks = 1;                                      // one link from "/"
    inode.color  = WFS_COLOR_NONE;
    memset(inode.blocks, 0, sizeof(inode.blocks));

    // Initialize timestamps for the root directory
    time_t now = time(NULL);
    inode.atim = now;
    inode.mtim = now;
    inode.ctim = now;

    // Mark inode 0 as allocated in the inode bitmap
    uint32_t bit = 0x1;
    if (lseek(fd, sb.i_bitmap_ptr, SEEK_SET) < 0) {
        perror("lseek inode bitmap\n");
        close(fd);
        return -1;
    }
    if (write(fd, &bit, sizeof(uint32_t)) < 0) {
        perror("writing inode bitmap\n");
        close(fd);
        return -1;
    }

    // The rest of the inode bitmap, data bitmap, and blocks are assumed to be zeroed
    // by the script that created the disk image (create_disk.sh).

    // Write the root inode into the first inode slot on disk
    if (lseek(fd, sb.i_blocks_ptr, SEEK_SET) < 0) {
        perror("lseek root inode\n");
        close(fd);
        return -1;
    }
    if (write(fd, &inode, sizeof(struct wfs_inode)) < 0) {
        perror("writing root inode\n");
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

int main(int argc, char* argv[]) {
    char* diskimg = NULL;
    int inodes = 0, blocks = 0;
    int opt;
    
    // Parse command-line flags: -d (image), -i (num inodes), -b (num data blocks)
    while ((opt = getopt(argc, argv, "d:i:b:")) != -1) {
        switch (opt) {
        case 'd':
            diskimg = optarg;
            break;
        case 'i':
            inodes = atoi(optarg);
            break;
        case 'b':
            blocks = atoi(optarg);
            break;
        default:
            printf("usage: ./mkfs -d <disk img> -i <num inodes> -b <num data blocks>\n");
            exit(1);
        }
    }

    // Basic sanity check for the arguments
    if (!diskimg || inodes <= 0 || blocks <= 0) {
        printf("usage: ./mkfs -d <disk img> -i <num inodes> -b <num data blocks>\n");
        exit(1);
    }
    
    return wfs_mkfs(diskimg, inodes, blocks);
}