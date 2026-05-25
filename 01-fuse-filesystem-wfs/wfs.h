#ifndef WFS_H
#define WFS_H

#include <time.h>
#include <sys/stat.h>
#include <stdint.h>

#define BLOCK_SIZE (512)
#define MAX_NAME   (28)

#define D_BLOCK    (6)
#define IND_BLOCK  (D_BLOCK + 1)
#define N_BLOCKS   (IND_BLOCK + 1)

/*
 * Superblock describes how the whole filesystem is laid out on disk.
 * mkfs writes this struct at offset 0 of the disk image.
 *
 * Overall disk layout:
 *
 *         d_bitmap_ptr       d_blocks_ptr
 *              v                  v
 * +----+---------+---------+--------+--------------------------+
 * | SB | IBITMAP | DBITMAP | INODES |       DATA BLOCKS        |
 * +----+---------+---------+--------+--------------------------+
 * 0    ^                   ^
 * i_bitmap_ptr        i_blocks_ptr
 */

// Superblock (one copy at the very start of the disk)
struct wfs_sb {
    size_t num_inodes;       // how many inode slots exist
    size_t num_data_blocks;  // how many data blocks exist
    off_t  i_bitmap_ptr;     // byte offset of inode bitmap
    off_t  d_bitmap_ptr;     // byte offset of data bitmap
    off_t  i_blocks_ptr;     // byte offset where inode blocks start
    off_t  d_blocks_ptr;     // byte offset where data blocks start
};

// Color tag palette: stored on disk as a small enum value
typedef enum {
    WFS_COLOR_NONE = 0,
    WFS_COLOR_RED,
    WFS_COLOR_GREEN,
    WFS_COLOR_BLUE,
    WFS_COLOR_YELLOW,
    WFS_COLOR_MAGENTA,
    WFS_COLOR_CYAN,
    WFS_COLOR_WHITE,
    WFS_COLOR_BLACK,
    WFS_COLOR_ORANGE,
    WFS_COLOR_PURPLE,
    WFS_COLOR_GRAY,
    WFS_COLOR_MAX        // not a real color, just a limit marker
} wfs_color_t;

// On-disk inode format.
// Important: field order and types must match the prebuilt disk image layout.
struct wfs_inode {
    int     num;      /* Inode number (index into inode array) */
    mode_t  mode;     /* File type + permissions (st_mode) */
    uid_t   uid;      /* Owner user ID */
    gid_t   gid;      /* Owner group ID */
    off_t   size;     /* File size in bytes */
    int     nlinks;   /* How many directory entries point to this inode */

    /* Time metadata (Part 3) - stored as time_t for compatibility */
    time_t  atim;     /* Last read/access time */
    time_t  mtim;     /* Last content write time */
    time_t  ctim;     /* Last metadata change time (mode, xattr, etc.) */

    /* Color tag (Part 4) */
    wfs_color_t color;     /* Simple color code for this file/dir */

    /* Data block pointers (Part 1)
     * blocks[0..D_BLOCK-1]  : direct data blocks
     * blocks[IND_BLOCK]     : single indirect block (array of off_t) */
    off_t   blocks[N_BLOCKS];
};

// Directory entry stored in directory data blocks.
// name is not NUL-terminated if it fills the whole array.
struct wfs_dentry {
    char name[MAX_NAME];  // file or directory name
    int  num;             // inode number for this entry
};

/* Path / inode helpers */
int   get_inode_from_path(char *path, struct wfs_inode **inode);
char *data_offset(struct wfs_inode *inode, off_t offset, int alloc);
int   add_dentry(struct wfs_inode *parent, int num, char *name);
int   remove_dentry(struct wfs_inode *inode, int inum);
int   dentry_to_num(char *name, struct wfs_inode *inode);
void  free_block(off_t blk);
void  free_inode(struct wfs_inode *inode);
struct wfs_inode *retrieve_inode(int num);
off_t allocate_data_block(void);
struct wfs_inode *allocate_inode(void);
void  fillin_inode(struct wfs_inode *inode, mode_t mode);
void  create_root_dir(void);

#endif /* WFS_H */