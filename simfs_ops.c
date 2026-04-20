/* simfs_ops.c
 * Implements the four file system operations:
 * createfile, deletefile, readfile, writefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simfs.h"

/* ═══════════════════════════════════════════════════════════
   HELPER FUNCTIONS
   ═══════════════════════════════════════════════════════════ */

FILE *
openfs(char *filename, char *mode)
{
    FILE *fp;
    if ((fp = fopen(filename, mode)) == NULL) {
        perror("openfs");
        exit(1);
    }
    return fp;
}

void
closefs(FILE *fp)
{
    if (fclose(fp) != 0) {
        perror("closefs");
        exit(1);
    }
}

/* Load fentries and fnodes from the fs file into our arrays.
 * We do this at the start of every operation so we know the current state. */
void
load_metadata(char *fsfile, fentry *files, fnode *fnodes)
{
    FILE *fp = openfs(fsfile, "r");
    if (fread(files, sizeof(fentry), MAXFILES, fp) != MAXFILES) {
        fprintf(stderr, "Error: could not read file entries\n");
        closefs(fp); exit(1);
    }
    if (fread(fnodes, sizeof(fnode), MAXBLOCKS, fp) != MAXBLOCKS) {
        fprintf(stderr, "Error: could not read fnodes\n");
        closefs(fp); exit(1);
    }
    closefs(fp);
}

/* Write fentries and fnodes back to the fs file.
 * "r+" opens for read+write WITHOUT truncating the file. */
void
save_metadata(char *fsfile, fentry *files, fnode *fnodes)
{
    FILE *fp = openfs(fsfile, "r+");
    if (fwrite(files, sizeof(fentry), MAXFILES, fp) != MAXFILES) {
        fprintf(stderr, "Error: could not write file entries\n");
        closefs(fp); exit(1);
    }
    if (fwrite(fnodes, sizeof(fnode), MAXBLOCKS, fp) != MAXBLOCKS) {
        fprintf(stderr, "Error: could not write fnodes\n");
        closefs(fp); exit(1);
    }
    closefs(fp);
}

/* Find a file by name. Returns fentry index or -1 if not found.
 * A free slot has name[0] == '\0', so we skip those. */
int
find_file(fentry *files, char *name)
{
    for (int i = 0; i < MAXFILES; i++) {
        if (files[i].name[0] != '\0' && strncmp(files[i].name, name, 12) == 0)
            return i;
    }
    return -1;
}

/* Count available (free) fnode slots.
 * A fnode is free if blockindex < 0. */
int
count_free_fnodes(fnode *fnodes)
{
    int count = 0;
    for (int i = 0; i < MAXBLOCKS; i++) {
        if (fnodes[i].blockindex < 0)
            count++;
    }
    return count;
}

/* Find index of the first free fnode, or -1 if none available. */
int
find_free_fnode(fnode *fnodes)
{
    for (int i = 0; i < MAXBLOCKS; i++) {
        if (fnodes[i].blockindex < 0)
            return i;
    }
    return -1;
}

/* Count how many blocks a file has by walking its fnode chain. */
int
count_file_blocks(fnode *fnodes, int firstblock)
{
    int count = 0;
    int cur = firstblock;
    while (cur != -1) {
        count++;
        cur = fnodes[cur].nextblock;
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════
   CREATEFILE
   ═══════════════════════════════════════════════════════════ */
void
createfile(char *fsfile, char *name)
{
    if (strlen(name) > 11) {
        fprintf(stderr, "createfile error: name '%s' is too long (max 11 chars)\n", name);
        exit(1);
    }

    fentry files[MAXFILES];
    fnode  fnodes[MAXBLOCKS];
    load_metadata(fsfile, files, fnodes);

    if (find_file(files, name) != -1) {
        fprintf(stderr, "createfile error: file '%s' already exists\n", name);
        exit(1);
    }

    /* Find a free fentry slot — empty name[0] means free */
    int slot = -1;
    for (int i = 0; i < MAXFILES; i++) {
        if (files[i].name[0] == '\0') { slot = i; break; }
    }
    if (slot == -1) {
        fprintf(stderr, "createfile error: filesystem full (MAXFILES=%d reached)\n", MAXFILES);
        exit(1);
    }

    strncpy(files[slot].name, name, 12);  /* 12 = max 11 chars + null terminator */
    files[slot].size       = 0;
    files[slot].firstblock = -1;          /* no blocks allocated yet */

    save_metadata(fsfile, files, fnodes);
}

/* ═══════════════════════════════════════════════════════════
   DELETEFILE
   ═══════════════════════════════════════════════════════════ */
void
deletefile(char *fsfile, char *name)
{
    fentry files[MAXFILES];
    fnode  fnodes[MAXBLOCKS];
    load_metadata(fsfile, files, fnodes);

    int slot = find_file(files, name);
    if (slot == -1) {
        fprintf(stderr, "deletefile error: file '%s' not found\n", name);
        exit(1);
    }

    FILE *fp = openfs(fsfile, "r+");
    char zerobuf[BLOCKSIZE] = {0};

    /* Walk the fnode chain: zero each block, then mark the fnode as free */
    int cur = files[slot].firstblock;
    while (cur != -1) {
        int next = fnodes[cur].nextblock;  /* save next BEFORE we modify this fnode */

        /* Zero this data block. fnode[i]'s block is at byte offset i*BLOCKSIZE */
        if (fseek(fp, (long)cur * BLOCKSIZE, SEEK_SET) != 0) {
            fprintf(stderr, "deletefile error: seek failed\n");
            closefs(fp); exit(1);
        }
        if (fwrite(zerobuf, BLOCKSIZE, 1, fp) != 1) {
            fprintf(stderr, "deletefile error: write failed\n");
            closefs(fp); exit(1);
        }

        /* Mark this fnode as free: negative blockindex = free */
        fnodes[cur].blockindex = -cur;
        fnodes[cur].nextblock  = -1;

        cur = next;
    }
    closefs(fp);

    /* Clear the fentry (sets name[0]='\0' which marks it as a free slot) */
    memset(&files[slot], 0, sizeof(fentry));
    files[slot].firstblock = -1;

    save_metadata(fsfile, files, fnodes);
}

/* ═══════════════════════════════════════════════════════════
   READFILE
   ═══════════════════════════════════════════════════════════ */
void
readfile(char *fsfile, char *name, int start, int length)
{
    fentry files[MAXFILES];
    fnode  fnodes[MAXBLOCKS];
    load_metadata(fsfile, files, fnodes);

    int slot = find_file(files, name);
    if (slot == -1) {
        fprintf(stderr, "readfile error: file '%s' not found\n", name);
        exit(1);
    }

    if (start > files[slot].size || start + length > files[slot].size) {
        fprintf(stderr, "readfile error: range [%d, %d) out of bounds (file size=%d)\n",
                start, start + length, files[slot].size);
        exit(1);
    }

    FILE *fp = openfs(fsfile, "r");

    /* Walk the chain to the block that contains byte 'start'.
     * start / BLOCKSIZE tells us how many blocks to skip in the chain. */
    int cur = files[slot].firstblock;
    int blocks_to_skip = start / BLOCKSIZE;
    for (int i = 0; i < blocks_to_skip; i++) {
        cur = fnodes[cur].nextblock;
    }

    int bytes_left = length;
    int file_pos   = start;

    while (bytes_left > 0 && cur != -1) {
        int offset_in_block = file_pos % BLOCKSIZE;
        int space_in_block  = BLOCKSIZE - offset_in_block;
        int to_read         = (bytes_left < space_in_block) ? bytes_left : space_in_block;

        /* Seek: block starts at cur*BLOCKSIZE, then add offset within the block */
        if (fseek(fp, (long)cur * BLOCKSIZE + offset_in_block, SEEK_SET) != 0) {
            fprintf(stderr, "readfile error: seek failed\n");
            closefs(fp); exit(1);
        }

        char buf[BLOCKSIZE];
        if ((int)fread(buf, 1, to_read, fp) != to_read) {
            fprintf(stderr, "readfile error: read failed\n");
            closefs(fp); exit(1);
        }

        fwrite(buf, 1, to_read, stdout);  /* print to terminal */

        file_pos  += to_read;
        bytes_left -= to_read;
        cur = fnodes[cur].nextblock;
    }

    closefs(fp);
}

/* ═══════════════════════════════════════════════════════════
   WRITEFILE  (atomic: all or nothing)
   ═══════════════════════════════════════════════════════════ */
void
writefile(char *fsfile, char *name, int start, int length)
{
    fentry files[MAXFILES];
    fnode  fnodes[MAXBLOCKS];
    load_metadata(fsfile, files, fnodes);

    int slot = find_file(files, name);
    if (slot == -1) {
        fprintf(stderr, "writefile error: file '%s' not found\n", name);
        exit(1);
    }

    /* No gaps: can only write from 0 up to current file size */
    if (start > files[slot].size) {
        fprintf(stderr, "writefile error: start=%d beyond file size=%d\n",
                start, files[slot].size);
        exit(1);
    }

    /* ATOMIC STEP 1: Read ALL data from stdin before touching the filesystem.
     * If we can't get the data, we exit cleanly with nothing changed. */
    char *data = malloc(length);
    if (!data) {
        fprintf(stderr, "writefile error: malloc failed\n");
        exit(1);
    }
    if ((int)fread(data, 1, length, stdin) != length) {
        fprintf(stderr, "writefile error: could not read %d bytes from stdin\n", length);
        free(data); exit(1);
    }

    /* How many blocks do we need after this write?
     * new_size = the file's size after this write completes.
     * blocks_needed = ceiling(new_size / BLOCKSIZE)
     * blocks_to_alloc = how many NEW blocks we need on top of what we have */
    int new_size        = (start + length > files[slot].size) ? start + length : files[slot].size;
    int blocks_needed   = (new_size + BLOCKSIZE - 1) / BLOCKSIZE;
    int blocks_have     = count_file_blocks(fnodes, files[slot].firstblock);
    int blocks_to_alloc = blocks_needed - blocks_have;

    /* ATOMIC STEP 2: Check we have enough free blocks BEFORE writing anything.
     * If not, exit with error. Nothing in the filesystem has changed yet. */
    if (blocks_to_alloc > 0 && count_free_fnodes(fnodes) < blocks_to_alloc) {
        fprintf(stderr, "writefile error: not enough free blocks\n");
        free(data); exit(1);
    }

    /* Allocate new fnodes and attach them to the end of the chain.
     * Find the current tail of the chain first. */
    int last = -1;
    int cur  = files[slot].firstblock;
    while (cur != -1 && fnodes[cur].nextblock != -1) {
        cur = fnodes[cur].nextblock;
    }
    last = cur;  /* -1 if the file has no blocks yet */

    int new_fnode_indices[MAXBLOCKS];  /* track new ones so we can zero them */
    int new_fnode_count = 0;

    for (int i = 0; i < blocks_to_alloc; i++) {
        int nf = find_free_fnode(fnodes);

        fnodes[nf].blockindex = nf;   /* positive = in use */
        fnodes[nf].nextblock  = -1;   /* end of chain */

        if (last == -1)
            files[slot].firstblock = nf;  /* first block this file has ever had */
        else
            fnodes[last].nextblock = nf;  /* link to end of chain */

        last = nf;
        new_fnode_indices[new_fnode_count++] = nf;
    }

    /* Open the real file for writing */
    FILE *fp = openfs(fsfile, "r+");
    char zerobuf[BLOCKSIZE] = {0};

    /* Zero out every newly allocated block (spec requirement) */
    for (int i = 0; i < new_fnode_count; i++) {
        int idx = new_fnode_indices[i];
        if (fseek(fp, (long)idx * BLOCKSIZE, SEEK_SET) != 0) {
            fprintf(stderr, "writefile error: seek failed during zero\n");
            closefs(fp); free(data); exit(1);
        }
        if (fwrite(zerobuf, BLOCKSIZE, 1, fp) != 1) {
            fprintf(stderr, "writefile error: write failed during zero\n");
            closefs(fp); free(data); exit(1);
        }
    }

    /* Walk to the block containing byte 'start', then write chunk by chunk */
    cur = files[slot].firstblock;
    int blocks_to_skip = start / BLOCKSIZE;
    for (int i = 0; i < blocks_to_skip; i++) {
        cur = fnodes[cur].nextblock;
    }

    int bytes_left = length;
    int file_pos   = start;
    int data_off   = 0;   /* index into our data buffer */

    while (bytes_left > 0 && cur != -1) {
        int offset_in_block = file_pos % BLOCKSIZE;
        int space_in_block  = BLOCKSIZE - offset_in_block;
        int to_write        = (bytes_left < space_in_block) ? bytes_left : space_in_block;

        if (fseek(fp, (long)cur * BLOCKSIZE + offset_in_block, SEEK_SET) != 0) {
            fprintf(stderr, "writefile error: seek failed\n");
            closefs(fp); free(data); exit(1);
        }
        if ((int)fwrite(data + data_off, 1, to_write, fp) != to_write) {
            fprintf(stderr, "writefile error: write failed\n");
            closefs(fp); free(data); exit(1);
        }

        file_pos  += to_write;
        data_off  += to_write;
        bytes_left -= to_write;
        cur = fnodes[cur].nextblock;
    }

    closefs(fp);

    /* Update size if we extended the file */
    if (start + length > (int)files[slot].size)
        files[slot].size = start + length;

    save_metadata(fsfile, files, fnodes);
    free(data);
}
