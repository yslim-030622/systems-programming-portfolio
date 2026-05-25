#define FUSE_USE_VERSION 30
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stdlib.h>
#include <fuse.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <sys/xattr.h>
#include "wfs.h"

/* --------------------------- Globals / Mount ------------------------------ */
/* Global state for the mounted filesystem. The disk image is mmapped once
 * and all on-disk structures (superblock, bitmaps, inodes, data blocks) are
 * accessed by pointer arithmetic off this region. */
void *mregion; // mapped disk image
int   wfs_error; // last error code stored for helpers to return through FUSE

/* ---------------------- Superblock / bitmap helpers ----------------------- */
/* Return a pointer to the superblock at the start of the mapped region. */
static struct wfs_sb *wfs_super(void) {
    return (struct wfs_sb *)mregion;
}

/* Quick sanity check that an inode number is in range. */
static inline int valid_inum(int inum) {
    struct wfs_sb *sb = wfs_super();
    return (inum >= 0 && (size_t)inum < sb->num_inodes);
}

/* Bitmaps live right after the superblock; these helpers give typed pointers. */
static inline uint32_t *inode_bitmap(void) {
    struct wfs_sb *sb = wfs_super();
    return (uint32_t *)((char *)mregion + sb->i_bitmap_ptr);
}

static inline uint32_t *data_bitmap(void) {
    struct wfs_sb *sb = wfs_super();
    return (uint32_t *)((char *)mregion + sb->d_bitmap_ptr);
}

/* Number of 32-bit words used to represent each bitmap. */
static inline size_t inode_bitmap_len_words(void) {
    struct wfs_sb *sb = wfs_super();
    return (sb->num_inodes + 31) / 32;
}

static inline size_t data_bitmap_len_words(void) {
    struct wfs_sb *sb = wfs_super();
    return (sb->num_data_blocks + 31) / 32;
}

/* Check whether a particular bit in the bitmap is set (allocated). */
static int bitmap_test(uint32_t position, uint32_t *bitmap) {
    uint32_t idx = position / 32;
    uint32_t bit = position % 32;
    return (bitmap[idx] >> bit) & 0x1;
}

/* --------------------------- Time helpers --------------------------------- */
/* Update atime/mtime/ctime on an inode that is already mapped into memory.
 * Because inodes live in the mmapped region, updating these fields updates
 * the on-disk copy as well. */
static void wfs_update_times(struct wfs_inode *inode,
                             int update_atime,
                             int update_mtime,
                             int update_ctime)
{
    if (!inode) return;
    time_t now = time(NULL);
    if (update_atime) inode->atim = now;
    if (update_mtime) inode->mtim = now;
    if (update_ctime) inode->ctim = now;
}

/* --------------------------- Path helpers --------------------------------- */
/* Clean up a path: remove any ANSI color codes that might have been added
 * for pretty printing, and make sure we never end up with an empty string
 * (force "/" if the cleaned path would be empty). */
static void clean_path(const char *path, char *clean, size_t len) {
    if (!path || !clean || len == 0) return;
    extern void strip_ansi_codes(const char *in, char *out, size_t out_len);
    strip_ansi_codes(path, clean, len);
    if (clean[0] == '\0') {
        clean[0] = '/';
        clean[1] = '\0';
    }
}

/* Split an absolute path "/a/b/c" into parent "/a/b" and leaf name "c".
 * On success, *parent_out and *name_out are freshly allocated strings that
 * the caller must free. */
static int split_path(const char *path, char **parent_out, char **name_out) {
    if (!path || path[0] != '/')
        return -ENOENT;
    if (strcmp(path, "/") == 0)
        return -EINVAL;

    char *p1 = strdup(path);
    char *p2 = strdup(path);
    if (!p1 || !p2) {
        free(p1); free(p2);
        return -ENOMEM;
    }
    char *dir  = dirname(p1);
    char *base = basename(p2);

    *parent_out = strdup(dir);
    *name_out   = strdup(base);

    free(p1); free(p2);

    if (!*parent_out || !*name_out) {
        free(*parent_out); free(*name_out);
        return -ENOMEM;
    }
    return 0;
}

/* ---------------------- Color helpers (xattr + ls) ----------------------- */
/* Mapping between human-readable color names and the on-disk color codes. */
struct color_entry { const char *name; uint8_t code; };
static const struct color_entry color_table[] = {
    {"none",    WFS_COLOR_NONE},
    {"red",     WFS_COLOR_RED},
    {"green",   WFS_COLOR_GREEN},
    {"blue",    WFS_COLOR_BLUE},
    {"yellow",  WFS_COLOR_YELLOW},
    {"magenta", WFS_COLOR_MAGENTA},
    {"cyan",    WFS_COLOR_CYAN},
    {"white",   WFS_COLOR_WHITE},
    {"black",   WFS_COLOR_BLACK},
    {"orange",  WFS_COLOR_ORANGE},
    {"purple",  WFS_COLOR_PURPLE},
    {"gray",    WFS_COLOR_GRAY},
};

/* Convert a color name string (case-insensitive) into a color code.
 * Returns 1 on success and fills out_code, 0 if the name is not recognized. */
int parse_color_name(const char *s, uint8_t *out_code) {
    if (!s || !out_code) return 0;
    char buf[32]; size_t n = 0;
    while (s[n] && n + 1 < sizeof(buf)) { buf[n] = (char)tolower((unsigned char)s[n]); n++; }
    buf[n] = '\0';
    for (size_t i = 0; i < sizeof(color_table)/sizeof(color_table[0]); i++) {
        if (strcmp(buf, color_table[i].name) == 0) { *out_code = color_table[i].code; return 1; }
    }
    return 0;
}

/* Information needed to print a colored name (ANSI code + human name). */
typedef struct { const char *ansi; const char *name; } wfs_color_info;

/* Look up ANSI escape sequence and label for a given color code. */
static inline const wfs_color_info* wfs_color_from_code(uint8_t code) {
    static const wfs_color_info table[] = {
        [WFS_COLOR_NONE]    = { "",               "none"    },
        [WFS_COLOR_RED]     = { "\033[31m",       "red"     },
        [WFS_COLOR_GREEN]   = { "\033[32m",       "green"   },
        [WFS_COLOR_BLUE]    = { "\033[34m",       "blue"    },
        [WFS_COLOR_YELLOW]  = { "\033[33m",       "yellow"  },
        [WFS_COLOR_MAGENTA] = { "\033[35m",       "magenta" },
        [WFS_COLOR_CYAN]    = { "\033[36m",       "cyan"    },
        [WFS_COLOR_WHITE]   = { "\033[37m",       "white"   },
        [WFS_COLOR_BLACK]   = { "\033[30m",       "black"   },
        [WFS_COLOR_ORANGE]  = { "\033[38;5;208m", "orange"  },
        [WFS_COLOR_PURPLE]  = { "\033[35m",       "purple"  },
        [WFS_COLOR_GRAY]    = { "\033[90m",       "gray"    },
    };
    if (code < WFS_COLOR_MAX) return &table[code];
    return &table[WFS_COLOR_NONE];
}

/* Strip ANSI escape sequences from a string. This is useful because ls output
 * might contain color codes, but FUSE paths should not. */
void strip_ansi_codes(const char *in, char *out, size_t out_len)
{
    if (!in || !out || out_len == 0) return;

    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 1 < out_len; ) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0x1B && in[i+1] == '[') {
            i += 2;
            while (in[i] && in[i] != 'm') i++;
            if (in[i] == 'm') i++;
        } else {
            out[o++] = in[i++];
        }
    }
    out[o] = '\0';
}

/* ------------------------ inode / block helpers --------------------------- */
/* Clear a bit in the bitmap, marking the corresponding inode or block free. */
void free_bitmap(uint32_t position, uint32_t* bitmap) {
    uint32_t idx = position / 32;
    uint32_t bit = position % 32;
    bitmap[idx] &= ~(1U << bit);
}

/* Look up an inode by number. Returns NULL if the number is invalid or if the
 * inode is not marked allocated in the inode bitmap. */
struct wfs_inode *retrieve_inode(int inum) {
    struct wfs_sb *sb = wfs_super();
    if (inum < 0 || (size_t)inum >= sb->num_inodes) return NULL;

    uint32_t *ibm = inode_bitmap();
    if (!bitmap_test((uint32_t)inum, ibm)) return NULL;

    /* Each inode takes up one full block on disk, so its offset is:
     *   i_blocks_ptr + inum * BLOCK_SIZE */
    off_t off = sb->i_blocks_ptr + (off_t)inum * BLOCK_SIZE;
    return (struct wfs_inode *)((char *)mregion + off);
}

/* Generic bitmap allocator: scan for a zero bit and set it, returning its
 * index, or -1 if no free entry exists. Used for both inode and data bitmaps. */
ssize_t allocate_block(uint32_t* bitmap, size_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint32_t bm_region = bitmap[i];
        if (bm_region == 0xFFFFFFFF) {
            continue;
        }
        for (uint32_t k = 0; k < 32; k++) {
            if (!((bm_region >> k) & 0x1)) { // it is free
                bitmap[i] = bitmap[i] | (0x1 << k);
                return 32*i + k;
            }
        }
    }
    return -1; // no free blocks found
}

/* Allocate a new inode, zero it, assign an inode number, and initialize its
 * metadata. Returns NULL and sets wfs_error on failure. */
struct wfs_inode *allocate_inode(void) {
    struct wfs_sb *sb = wfs_super();
    uint32_t *ibm = inode_bitmap();
    size_t len = inode_bitmap_len_words();

    ssize_t idx = allocate_block(ibm, len);
    if (idx < 0 || (size_t)idx >= sb->num_inodes) {
        wfs_error = -ENOSPC;
        return NULL;
    }

    off_t off = sb->i_blocks_ptr + (off_t)idx * BLOCK_SIZE;
    struct wfs_inode *inode = (struct wfs_inode *)((char *)mregion + off);
    memset(inode, 0, sizeof(struct wfs_inode));
    inode->num   = (int)idx;
    inode->color = WFS_COLOR_NONE;
    memset(inode->blocks, 0, sizeof(inode->blocks));

    wfs_update_times(inode, 1, 1, 1);  // initialize atime/mtime/ctime
    return inode;
}

/* Allocate a data block, zero its contents, and return its byte offset from
 * the start of the disk image. Returns 0 and sets wfs_error if allocation fails. */
off_t allocate_data_block(void) {
    struct wfs_sb *sb = wfs_super();
    uint32_t *dbm = data_bitmap();
    size_t len = data_bitmap_len_words();

    ssize_t idx = allocate_block(dbm, len);
    if (idx < 0 || (size_t)idx >= sb->num_data_blocks) {
        wfs_error = -ENOSPC;
        return 0;
    }

    off_t blk_off = sb->d_blocks_ptr + (off_t)idx * BLOCK_SIZE;
    memset((char *)mregion + blk_off, 0, BLOCK_SIZE);
    return blk_off;
}

/* Free an inode: clear its bit in the inode bitmap and zero out the struct.
 * The caller is responsible for freeing any data blocks before calling this. */
void free_inode(struct wfs_inode *inode) {
    if (!inode) return;
    int num = inode->num;
    uint32_t *ibm = inode_bitmap();
    free_bitmap((uint32_t)num, ibm);
    memset(inode, 0, sizeof(struct wfs_inode));
}

/* Free a data block given its byte offset into the disk image. This validates
 * that the offset is inside the data region and aligned to a block boundary
 * before clearing the corresponding bit in the data bitmap. */
void free_block(off_t blk_offset) {
    struct wfs_sb *sb = wfs_super();
    if (blk_offset < sb->d_blocks_ptr) return;

    off_t rel = blk_offset - sb->d_blocks_ptr;
    if (rel < 0 || (rel % BLOCK_SIZE) != 0) return;

    uint32_t idx = (uint32_t)(rel / BLOCK_SIZE);
    if ((size_t)idx >= sb->num_data_blocks) return;

    uint32_t *dbm = data_bitmap();
    free_bitmap(idx, dbm);
    memset((char *)mregion + blk_offset, 0, BLOCK_SIZE);
}

/* Return a pointer into the file's data for a given byte offset. If alloc is
 * non-zero, any missing data blocks (direct or via the single indirect block)
 * are allocated on demand. If alloc is zero, missing blocks yield NULL. */
char *data_offset(struct wfs_inode *inode, off_t offset, int alloc) {
    if (!inode || offset < 0) {
        wfs_error = -ENOENT;
        return NULL;
    }

    off_t block_index  = offset / BLOCK_SIZE;
    off_t block_offset = offset % BLOCK_SIZE;

    size_t indir_entries = BLOCK_SIZE / sizeof(off_t);
    off_t max_blocks = D_BLOCK + (off_t)indir_entries;
    if (block_index >= max_blocks) {
        wfs_error = -ENOSPC;
        return NULL;
    }

    off_t blk_off = 0;

    /* Direct blocks first. */
    if (block_index < D_BLOCK) {
        if (inode->blocks[block_index] == 0 && alloc) {
            off_t new_blk = allocate_data_block();
            if (!new_blk) return NULL;
            inode->blocks[block_index] = new_blk;
        }
        blk_off = inode->blocks[block_index];
    } else {
        /* Single indirect block. */
        off_t ind_off = inode->blocks[IND_BLOCK];
        if (ind_off == 0 && alloc) {
            off_t new_ind = allocate_data_block();
            if (!new_ind) return NULL;
            inode->blocks[IND_BLOCK] = new_ind;
            memset((char *)mregion + new_ind, 0, BLOCK_SIZE);
            ind_off = new_ind;
        }
        if (ind_off == 0) {
            if (alloc) wfs_error = -ENOSPC;
            return NULL;
        }

        off_t *ind_arr = (off_t *)((char *)mregion + ind_off);
        off_t idx = block_index - D_BLOCK;
        if (idx < 0 || (size_t)idx >= indir_entries) {
            wfs_error = -ENOSPC;
            return NULL;
        }

        if (ind_arr[idx] == 0 && alloc) {
            off_t new_blk = allocate_data_block();
            if (!new_blk) return NULL;
            ind_arr[idx] = new_blk;
        }
        blk_off = ind_arr[idx];
    }

    if (blk_off == 0) return NULL;

    return (char *)mregion + blk_off + block_offset;
}

/* --------------------------- Directory helpers ---------------------------- */
/* Initialize a freshly allocated inode with basic metadata and no data
 * blocks. Mode is supplied by the caller (file vs directory, permissions). */
void fillin_inode(struct wfs_inode* inode, mode_t mode)
{
    inode->mode   = mode;
    inode->uid    = getuid();
    inode->gid    = getgid();
    inode->size   = 0;
    inode->nlinks = 1;
    inode->color  = WFS_COLOR_NONE;
    memset(inode->blocks, 0, sizeof(inode->blocks));

    wfs_update_times(inode, 1, 1, 1);
}

/* Look up a child entry by name inside a directory inode and return its
 * inode number. Returns a negative errno on error. */
int dentry_to_num(char* name, struct wfs_inode* inode) {
    if (!name || !inode) return -ENOENT;
    if (!S_ISDIR(inode->mode)) return -ENOTDIR;

    size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    size_t max_entries       = inode->size / sizeof(struct wfs_dentry);

    for (size_t idx = 0; idx < max_entries; idx++) {
        size_t blk_idx = idx / entries_per_block;
        size_t ent_idx = idx % entries_per_block;
        if (blk_idx >= D_BLOCK) break;

        off_t blk_off = inode->blocks[blk_idx];
        if (blk_off == 0) continue;

        struct wfs_dentry *d =
            (struct wfs_dentry *)((char *)mregion + blk_off +
                                  ent_idx * sizeof(struct wfs_dentry));

        if (d->num == 0 || d->name[0] == '\0') continue;

        if (strncmp(d->name, name, MAX_NAME) == 0) {
            return d->num;
        }
    }
    return -ENOENT;
}

/* Insert a new directory entry into a parent directory. If there is a free
 * slot in an existing directory block it is reused; otherwise a new data
 * block is allocated for the directory. */
int add_dentry(struct wfs_inode* parent, int num, char* name)
{
    if (!parent || !name) return -ENOENT;
    if (!S_ISDIR(parent->mode)) return -ENOTDIR;

    /* Do not allow duplicate names in the same directory. */
    if (dentry_to_num(name, parent) >= 0) return -EEXIST;

    size_t nlen = strnlen(name, MAX_NAME);
    if (nlen == 0 || nlen >= MAX_NAME) return -ENAMETOOLONG;

    size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    size_t max_entries       = D_BLOCK * entries_per_block;

    for (size_t idx = 0; idx < max_entries; idx++) {
        size_t blk_idx = idx / entries_per_block;
        size_t ent_idx = idx % entries_per_block;
        if (blk_idx >= D_BLOCK) break;

        /* Allocate a directory data block lazily if needed. */
        if (parent->blocks[blk_idx] == 0) {
            off_t new_blk = allocate_data_block();
            if (!new_blk) return wfs_error ? wfs_error : -ENOSPC;
            parent->blocks[blk_idx] = new_blk;
            memset((char *)mregion + new_blk, 0, BLOCK_SIZE);
        }

        off_t blk_off = parent->blocks[blk_idx];
        struct wfs_dentry *d =
            (struct wfs_dentry *)((char *)mregion + blk_off +
                                  ent_idx * sizeof(struct wfs_dentry));

        if (d->num == 0 || d->name[0] == '\0') {
            memset(d->name, 0, MAX_NAME);
            strncpy(d->name, name, MAX_NAME - 1);
            d->num = num;

            size_t needed_size = (idx + 1) * sizeof(struct wfs_dentry);
            if ((off_t)needed_size > parent->size)
                parent->size = (off_t)needed_size;

            wfs_update_times(parent, 0, 1, 1); // update mtime/ctime
            return 0;
        }
    }
    return -ENOSPC;
}

/* Remove a directory entry that points to inum, if present. This only clears
 * the dentry slot; freeing the inode and its blocks is done by the caller. */
int remove_dentry(struct wfs_inode *dir, int inum)
{
    if (!dir) return -ENOENT;
    if (!S_ISDIR(dir->mode)) return -ENOTDIR;

    size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    size_t max_entries       = dir->size / sizeof(struct wfs_dentry);

    for (size_t idx = 0; idx < max_entries; idx++) {
        size_t blk_idx = idx / entries_per_block;
        size_t ent_idx = idx % entries_per_block;
        if (blk_idx >= D_BLOCK) break;

        off_t blk_off = dir->blocks[blk_idx];
        if (blk_off == 0) continue;

        struct wfs_dentry *d =
            (struct wfs_dentry *)((char *)mregion + blk_off +
                                  ent_idx * sizeof(struct wfs_dentry));
        if (d->num == inum && d->num != 0) {
            d->num = 0;
            if (d->name[0] != '\0') d->name[0] = '\0';

            wfs_update_times(dir, 0, 1, 1); // update mtime/ctime
            return 0;
        }
    }
    return -ENOENT;
}

/* --------------------------- Path resolution ------------------------------ */
/* Resolve an absolute path like "/a/b/c" to an inode pointer by walking from
 * the root inode (inum 0) through directory entries. On success, *inode is
 * set to the final inode. */
int get_inode_from_path(char *path, struct wfs_inode **inode)
{
    if (!path || !inode) return -ENOENT;

    struct wfs_inode *cur = retrieve_inode(0);
    if (!cur) return -ENOENT;

    if (strcmp(path, "/") == 0) {
        *inode = cur;
        return 0;
    }

    char *tmp = strdup(path);
    if (!tmp) return -ENOMEM;

    char *p = tmp;
    if (p[0] == '/') p++;

    int ret = 0;
    char *saveptr = NULL;
    char *comp = strtok_r(p, "/", &saveptr);
    while (comp) {
        int child_inum = dentry_to_num(comp, cur);
        if (child_inum < 0) {
            ret = child_inum;
            goto out;
        }
        struct wfs_inode *next = retrieve_inode(child_inum);
        if (!next) {
            ret = -ENOENT;
            goto out;
        }
        cur = next;
        comp = strtok_r(NULL, "/", &saveptr);
    }

    *inode = cur;

out:
    free(tmp);
    return ret;
}

/* --------------------------- FUSE Operations ------------------------------ */
/* getattr: fill in struct stat based on our inode fields. */
int wfs_getattr(const char *path, struct stat *st)
{
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0) return rc;

    memset(st, 0, sizeof(*st));
    st->st_mode   = inode->mode;
    st->st_nlink  = inode->nlinks;
    st->st_uid    = inode->uid;
    st->st_gid    = inode->gid;
    st->st_size   = inode->size;
    st->st_blksize= BLOCK_SIZE;
    st->st_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    /* Times are stored as time_t already, so copy them straight over. */
    st->st_atime = inode->atim;
    st->st_mtime = inode->mtim;
    st->st_ctime = inode->ctim;
    return 0;
}

/* mknod: create a regular file (or FIFO) at the given path. */
int wfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    (void)dev;
    if (!S_ISREG(mode) && !S_ISFIFO(mode))
        mode = S_IFREG | (mode & 0777);

    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    char *parent_path = NULL;
    char *name        = NULL;
    int rc = split_path(clean, &parent_path, &name);
    if (rc < 0) return rc;

    struct wfs_inode *parent = NULL;
    rc = get_inode_from_path(parent_path, &parent);
    if (rc < 0) { free(parent_path); free(name); return rc; }

    if (!S_ISDIR(parent->mode)) {
        free(parent_path); free(name);
        return -ENOTDIR;
    }

    if (dentry_to_num(name, parent) >= 0) {
        free(parent_path); free(name);
        return -EEXIST;
    }

    struct wfs_inode *inode = allocate_inode();
    if (!inode) {
        free(parent_path); free(name);
        return wfs_error ? wfs_error : -ENOSPC;
    }

    fillin_inode(inode, S_IFREG | (mode & 0777));

    rc = add_dentry(parent, inode->num, name);
    if (rc < 0) {
        free_inode(inode);
        free(parent_path); free(name);
        return rc;
    }

    free(parent_path); free(name);
    return 0;
}

/* mkdir: create a new directory and add it to its parent. */
int wfs_mkdir(const char *path, mode_t mode)
{ 
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    char *parent_path = NULL;
    char *name        = NULL;
    int rc = split_path(clean, &parent_path, &name);
    if (rc < 0) return rc;

    struct wfs_inode *parent = NULL;
    rc = get_inode_from_path(parent_path, &parent);
    if (rc < 0) { free(parent_path); free(name); return rc; }

    if (!S_ISDIR(parent->mode)) {
        free(parent_path); free(name);
        return -ENOTDIR;
    }

    if (dentry_to_num(name, parent) >= 0) {
        free(parent_path); free(name);
        return -EEXIST;
    }

    struct wfs_inode *inode = allocate_inode();
    if (!inode) {
        free(parent_path); free(name);
        return wfs_error ? wfs_error : -ENOSPC;
    }

    fillin_inode(inode, S_IFDIR | (mode & 0777));
    inode->nlinks = 2;   // "." and the entry in the parent
    parent->nlinks++;

    rc = add_dentry(parent, inode->num, name);
    if (rc < 0) {
        free_inode(inode);
        parent->nlinks--;
        free(parent_path); free(name);
        return rc;
    }

    free(parent_path); free(name);
    return 0;
}

/* read: copy data out of file into user buffer, possibly spanning blocks. */
int wfs_read(const char *path, char *buf, size_t len, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0) return rc;

    if (S_ISDIR(inode->mode)) return -EISDIR;
    if (off >= inode->size) return 0;

    size_t to_read = len;
    if (off + (off_t)len > inode->size)
        to_read = inode->size - off;

    size_t done = 0;
    while (done < to_read) {
        off_t cur_off = off + (off_t)done;
        char *ptr = data_offset(inode, cur_off, 0);

        size_t block_rem = BLOCK_SIZE - (cur_off % BLOCK_SIZE);
        size_t chunk = to_read - done;
        if (chunk > block_rem) chunk = block_rem;

        if (!ptr) {
            memset(buf + done, 0, chunk);
        } else {
            memcpy(buf + done, ptr, chunk);
        }
        done += chunk;
    }

    if (done > 0) {
        wfs_update_times(inode, 1, 0, 0); // update atime only
    }
    return (int)done;
}

/* write: grow/modify file contents, allocating blocks as needed. */
int wfs_write(const char *path, const char *buf, size_t len, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0) return rc;

    if (S_ISDIR(inode->mode)) return -EISDIR;

    size_t done = 0;
    while (done < len) {
        off_t cur_off = off + (off_t)done;
        char *ptr = data_offset(inode, cur_off, 1);
        if (!ptr) return wfs_error ? wfs_error : -ENOSPC;

        size_t block_rem = BLOCK_SIZE - (cur_off % BLOCK_SIZE);
        size_t chunk = len - done;
        if (chunk > block_rem) chunk = block_rem;

        memcpy(ptr, buf + done, chunk);
        done += chunk;
    }

    off_t new_end = off + (off_t)done;
    if (new_end > inode->size)
        inode->size = new_end;

    if (done > 0) {
        wfs_update_times(inode, 0, 1, 1); // update mtime/ctime
    }
    return (int)done;
}

/* readdir: list directory entries, optionally decorating names with color
 * when the calling process is "ls". */
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t off, struct fuse_file_info *fi)
{
    (void)off;
    (void)fi;

    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *dir = NULL;
    int rc = get_inode_from_path(clean, &dir);
    if (rc < 0) return rc;

    if (!S_ISDIR(dir->mode)) return -ENOTDIR;

    int is_ls = 0;
    struct fuse_context *ctx = fuse_get_context();
    if (ctx) {
        char comm_path[64];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", ctx->pid);
        FILE *f = fopen(comm_path, "r");
        if (f) {
            char cmd[64];
            if (fgets(cmd, sizeof(cmd), f)) {
                size_t n = strcspn(cmd, "\n");
                cmd[n] = '\0';
                if (strcmp(cmd, "ls") == 0) is_ls = 1;
            }
            fclose(f);
        }
    }

    /* "." and ".." are always present. */
    filler(buf, ".",  NULL, 0);
    filler(buf, "..", NULL, 0);

    size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    size_t max_entries       = dir->size / sizeof(struct wfs_dentry);

    for (size_t idx = 0; idx < max_entries; idx++) {
        size_t blk_idx = idx / entries_per_block;
        size_t ent_idx = idx % entries_per_block;
        if (blk_idx >= D_BLOCK) break;

        off_t blk_off = dir->blocks[blk_idx];
        if (blk_off == 0) continue;

        struct wfs_dentry *d =
            (struct wfs_dentry *)((char *)mregion + blk_off +
                                  ent_idx * sizeof(struct wfs_dentry));

        if (d->num == 0 || d->name[0] == '\0') continue;

        struct wfs_inode *child = retrieve_inode(d->num);
        const char *name_to_send = d->name;
        char decorated[MAX_NAME + 64];

        /* Only colorize names when the caller is ls and the inode has a
         * non-default color. */
        if (is_ls && child && child->color != WFS_COLOR_NONE) {
            const wfs_color_info *info = wfs_color_from_code(child->color);
            snprintf(decorated, sizeof(decorated), "%s%s\033[0m",
                     info->ansi, d->name);
            name_to_send = decorated;
        }

        filler(buf, name_to_send, NULL, 0);
    }

    wfs_update_times(dir, 1, 0, 0); // update atime
    return 0;
}

/* unlink: remove a regular file and free its inode and data blocks when
 * the last link is gone. */
int wfs_unlink(const char *path)
{ 
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    char *parent_path = NULL;
    char *name        = NULL;
    int rc = split_path(clean, &parent_path, &name);
    if (rc < 0) return rc;

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        free(parent_path); free(name);
        return -EPERM;
    }

    struct wfs_inode *parent = NULL;
    rc = get_inode_from_path(parent_path, &parent);
    if (rc < 0) { free(parent_path); free(name); return rc; }

    if (!S_ISDIR(parent->mode)) {
        free(parent_path); free(name);
        return -ENOTDIR;
    }

    int inum = dentry_to_num(name, parent);
    if (inum < 0) {
        free(parent_path); free(name);
        return inum;
    }

    struct wfs_inode *inode = retrieve_inode(inum);
    if (!inode) {
        free(parent_path); free(name);
        return -ENOENT;
    }
    if (S_ISDIR(inode->mode)) {
        free(parent_path); free(name);
        return -EISDIR;
    }

    rc = remove_dentry(parent, inum);
    if (rc < 0) {
        free(parent_path); free(name);
        return rc;
    }

    inode->nlinks--;
    if (inode->nlinks <= 0) {
        /* Free all direct data blocks. */
        for (int i = 0; i < D_BLOCK; i++) {
            if (inode->blocks[i] != 0) {
                free_block(inode->blocks[i]);
                inode->blocks[i] = 0;
            }
        }

        /* Free any blocks pointed to by the indirect block, then the
         * indirect block itself. */
        if (inode->blocks[IND_BLOCK] != 0) {
            off_t ind_off = inode->blocks[IND_BLOCK];
            off_t *ind_arr = (off_t *)((char *)mregion + ind_off);
            size_t entries = BLOCK_SIZE / sizeof(off_t);
            for (size_t j = 0; j < entries; j++) {
                if (ind_arr[j] != 0) {
                    free_block(ind_arr[j]);
                    ind_arr[j] = 0;
                }
            }
            free_block(inode->blocks[IND_BLOCK]);
            inode->blocks[IND_BLOCK] = 0;
        }

        free_inode(inode);
    }

    wfs_update_times(parent, 0, 1, 1);

    free(parent_path); free(name);
    return 0;
}

/* rmdir: remove an empty directory (other than "/") and free its blocks. */
int wfs_rmdir(const char *path)
{ 
    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    if (strcmp(clean, "/") == 0) return -EBUSY;

    char *parent_path = NULL;
    char *name        = NULL;
    int rc = split_path(clean, &parent_path, &name);
    if (rc < 0) return rc;

    struct wfs_inode *parent = NULL;
    rc = get_inode_from_path(parent_path, &parent);
    if (rc < 0) { free(parent_path); free(name); return rc; }

    if (!S_ISDIR(parent->mode)) {
        free(parent_path); free(name);
        return -ENOTDIR;
    }

    int inum = dentry_to_num(name, parent);
    if (inum < 0) {
        free(parent_path); free(name);
        return inum;
    }

    struct wfs_inode *dir = retrieve_inode(inum);
    if (!dir) {
        free(parent_path); free(name);
        return -ENOENT;
    }
    if (!S_ISDIR(dir->mode)) {
        free(parent_path); free(name);
        return -ENOTDIR;
    }

    /* Make sure the directory is empty (ignoring "." and ".."). */
    size_t entries_per_block = BLOCK_SIZE / sizeof(struct wfs_dentry);
    size_t max_entries       = dir->size / sizeof(struct wfs_dentry);

    for (size_t idx = 0; idx < max_entries; idx++) {
        size_t blk_idx = idx / entries_per_block;
        size_t ent_idx = idx % entries_per_block;
        if (blk_idx >= D_BLOCK) break;

        off_t blk_off = dir->blocks[blk_idx];
        if (blk_off == 0) continue;

        struct wfs_dentry *d =
            (struct wfs_dentry *)((char *)mregion + blk_off +
                                  ent_idx * sizeof(struct wfs_dentry));

        if (d->num == 0 || d->name[0] == '\0') continue;
        if (strncmp(d->name, ".", MAX_NAME) == 0 ||
            strncmp(d->name, "..", MAX_NAME) == 0) continue;

        free(parent_path); free(name);
        return -ENOTEMPTY;
    }

    rc = remove_dentry(parent, inum);
    if (rc < 0) {
        free(parent_path); free(name);
        return rc;
    }

    parent->nlinks--;

    /* Free any directory data blocks (direct and indirect). */
    for (int i = 0; i < D_BLOCK; i++) {
        if (dir->blocks[i] != 0) {
            free_block(dir->blocks[i]);
            dir->blocks[i] = 0;
        }
    }

    if (dir->blocks[IND_BLOCK] != 0) {
        off_t ind_off = dir->blocks[IND_BLOCK];
        off_t *ind_arr = (off_t *)((char *)mregion + ind_off);
        size_t entries = BLOCK_SIZE / sizeof(off_t);
        for (size_t j = 0; j < entries; j++) {
            if (ind_arr[j] != 0) {
                free_block(ind_arr[j]);
                ind_arr[j] = 0;
            }
        }
        free_block(dir->blocks[IND_BLOCK]);
        dir->blocks[IND_BLOCK] = 0;
    }

    free_inode(dir);

    wfs_update_times(parent, 0, 1, 1);

    free(parent_path); free(name);
    return 0;
}

/* ------------------------- PART 2: statfs --------------------------------- */
/* Report filesystem statistics: block sizes, total counts, and how many
 * inodes and data blocks remain free. */
int wfs_statfs(const char *path, struct statvfs *st)
{
    (void)path;
    struct wfs_sb *sb = wfs_super();
    uint32_t *ibm = inode_bitmap();
    uint32_t *dbm = data_bitmap();

    size_t free_inodes = 0;
    size_t free_blocks = 0;

    for (size_t i = 0; i < sb->num_inodes; i++) {
        if (!bitmap_test((uint32_t)i, ibm)) free_inodes++;
    }
    for (size_t i = 0; i < sb->num_data_blocks; i++) {
        if (!bitmap_test((uint32_t)i, dbm)) free_blocks++;
    }

    memset(st, 0, sizeof(*st));
    st->f_bsize   = BLOCK_SIZE;
    st->f_frsize  = BLOCK_SIZE;
    st->f_blocks  = sb->num_data_blocks;
    st->f_bfree   = free_blocks;
    st->f_bavail  = free_blocks;
    st->f_files   = sb->num_inodes;
    st->f_ffree   = free_inodes;
    st->f_favail  = free_inodes;
    st->f_namemax = MAX_NAME;

    return 0;
}

/* ------------------------- PART 4: xattr ---------------------------------- */
/* Set the "user.color" extended attribute on an inode. The value is a small
 * string like "red" or "green", which we map to an enum code stored in the
 * inode. */
int wfs_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    (void)flags;

    if (strcmp(name, "user.color") != 0)
        return -ENOTSUP;

    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0)
        return rc;

    if (!valid_inum(inode->num))
        return -EINVAL;

    if (!value || size == 0)
        return -EINVAL;
    if (size >= 32)
        return -ERANGE;

    char buf[32];
    memcpy(buf, value, size);
    buf[size] = '\0';

    uint8_t code;
    if (!parse_color_name(buf, &code))
        return -EINVAL;

    inode->color = code;
    wfs_update_times(inode, 0, 0, 1);   // metadata change -> bump ctime

    return 0;
}

/* Get the "user.color" extended attribute as a string. If size == 0, we only
 * return the length of the value; otherwise we copy into the caller's buffer. */
int wfs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    if (strcmp(name, "user.color") != 0)
        return -ENOTSUP;

    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0)
        return rc;

    if (!valid_inum(inode->num))
        return -EINVAL;

    const wfs_color_info *info = wfs_color_from_code(inode->color);
    const char *cname = info->name;  // NONE -> "none"
    size_t needed = strlen(cname);

    if (size == 0)
        return (int)needed;

    if (size < needed)
        return -ERANGE;

    memcpy(value, cname, needed);
    if (size > needed)
        value[needed] = '\0';

    return (int)needed;
}

/* Remove the "user.color" attribute by resetting the color to NONE. */
int wfs_removexattr(const char *path, const char *name)
{
    if (strcmp(name, "user.color") != 0)
        return -ENOTSUP;

    char clean[PATH_MAX];
    clean_path(path, clean, sizeof(clean));

    struct wfs_inode *inode = NULL;
    int rc = get_inode_from_path(clean, &inode);
    if (rc < 0)
        return rc;

    if (!valid_inum(inode->num))
        return -EINVAL;

    inode->color = WFS_COLOR_NONE;
    wfs_update_times(inode, 0, 0, 1);   // metadata change -> ctime

    return 0;
}

/* ------------------------------ Mount Entry ------------------------------- */
/* FUSE operation table: hook our implementation into libfuse. */
static struct fuse_operations wfs_ops = {
    .getattr    = wfs_getattr,
    .mknod      = wfs_mknod,
    .mkdir      = wfs_mkdir,
    .read       = wfs_read,
    .write      = wfs_write,
    .readdir    = wfs_readdir,
    .unlink     = wfs_unlink,
    .rmdir      = wfs_rmdir,
    .statfs     = wfs_statfs,
    .setxattr   = wfs_setxattr,
    .getxattr   = wfs_getxattr,
    .removexattr= wfs_removexattr,
};

/* Program entry: open and mmap the disk image, then hand control to FUSE.
 * When FUSE exits, clean up the mapping and file descriptor. */
int main(int argc, char *argv[])
{
    int fuse_stat;
    struct stat sb;
    int fd;
    if (argc < 2) {
        fprintf(stderr, "usage: ./wfs <diskimage> <fuse-opts...>\n");
        return 1;
    }

    char* diskimage = strdup(argv[1]);

    // shift args down by one for fuse
    for (int i = 2; i < argc; i++) {
        argv[i-1] = argv[i];
    }
    argc -= 1;

    // open the file
    if ((fd = open(diskimage, O_RDWR, 0666)) < 0) {
        perror("open failed main\n");
        return 1;
    }

    // stat so we know how large the mmap needs to be
    if (fstat(fd, &sb) < 0) {
        perror("stat");
        close(fd);
        return 1;
    }

    // setup mmap
    mregion = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mregion == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    assert(retrieve_inode(0) != NULL);
    fuse_stat = fuse_main(argc, argv, &wfs_ops, NULL);

    munmap(mregion, sb.st_size);
    close(fd);
    free(diskimage);
    return fuse_stat;
}