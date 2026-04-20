#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simfs.h"

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

/* reads fentries and fnodes from the fs file into our arrays */
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

/* writes fentries and fnodes back to the fs file
 * "r+" so we don't truncate/wipe the whole file */
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

/* searches fentries by name, returns index or -1 if not found
 * skips empty slots (name[0] == '\0') */
int
find_file(fentry *files, char *name)
{
    for (int i = 0; i < MAXFILES; i++) {
        if (files[i].name[0] != '\0' && strncmp(files[i].name, name, 12) == 0)
            return i;
    }
    return -1;
}

/* counts how many fnodes are free (blockindex < 0 means free) */
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

/* returns index of first free fnode, or -1 if none */
int
find_free_fnode(fnode *fnodes)
{
    for (int i = 0; i < MAXBLOCKS; i++) {
        if (fnodes[i].blockindex < 0)
            return i;
    }
    return -1;
}

/* walks the fnode chain and counts how many blocks a file uses */
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

/* createfile: creates an empty file entry in the filesystem */
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

    /* find a free slot — name[0] == '\0' means slot is empty */
    int slot = -1;
    for (int i = 0; i < MAXFILES; i++) {
        if (files[i].name[0] == '\0') { slot = i; break; }
    }
    if (slot == -1) {
        fprintf(stderr, "createfile error: filesystem full (MAXFILES=%d reached)\n", MAXFILES);
        exit(1);
    }

    strncpy(files[slot].name, name, 12);
    files[slot].size       = 0;
    files[slot].firstblock = -1;  /* no blocks yet */

    save_metadata(fsfile, files, fnodes);
}

/* deletefile: removes file, zeros its data blocks, frees its fnodes */
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

    int cur = files[slot].firstblock;
    while (cur != -1) {
        int next = fnodes[cur].nextblock;  /* save next before we overwrite this fnode */

        /* fnode[i]'s data lives at byte offset i*BLOCKSIZE in the file */
        if (fseek(fp, (long)cur * BLOCKSIZE, SEEK_SET) != 0) {
            fprintf(stderr, "deletefile error: seek failed\n");
            closefs(fp); exit(1);
        }
        if (fwrite(zerobuf, BLOCKSIZE, 1, fp) != 1) {
            fprintf(stderr, "deletefile error: write failed\n");
            closefs(fp); exit(1);
        }

        fnodes[cur].blockindex = -cur;  /* negative = free */
        fnodes[cur].nextblock  = -1;

        cur = next;
    }
    closefs(fp);

    memset(&files[slot], 0, sizeof(fentry));
    files[slot].firstblock = -1;

    save_metadata(fsfile, files, fnodes);
}

/* readfile: reads 'length' bytes from 'start' and prints to stdout */
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

    /* skip to the block containing byte 'start'
     * start/BLOCKSIZE = how many blocks to skip in the chain */
    int cur = files[slot].firstblock;
    int blocks_to_skip = start / BLOCKSIZE;
    for (int i = 0; i < blocks_to_skip; i++) {
        cur = fnodes[cur].nextblock;
    }

    int bytes_left = length;
    int file_pos   = start;

    while (bytes_left > 0 && cur != -1) {
        int offset_in_block = file_pos % BLOCKSIZE;  /* where in this block we start */
        int space_in_block  = BLOCKSIZE - offset_in_block;
        int to_read         = (bytes_left < space_in_block) ? bytes_left : space_in_block;

        if (fseek(fp, (long)cur * BLOCKSIZE + offset_in_block, SEEK_SET) != 0) {
            fprintf(stderr, "readfile error: seek failed\n");
            closefs(fp); exit(1);
        }

        char buf[BLOCKSIZE];
        if ((int)fread(buf, 1, to_read, fp) != to_read) {
            fprintf(stderr, "readfile error: read failed\n");
            closefs(fp); exit(1);
        }

        fwrite(buf, 1, to_read, stdout);

        file_pos  += to_read;
        bytes_left -= to_read;
        cur = fnodes[cur].nextblock;
    }

    closefs(fp);
}

/* writefile: writes 'length' bytes from stdin into file at 'start'
 * atomic — checks everything first, writes nothing if it can't complete */
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

    if (start > files[slot].size) {
        fprintf(stderr, "writefile error: start=%d beyond file size=%d\n",
                start, files[slot].size);
        exit(1);
    }

    /* read all stdin data first — before touching the filesystem */
    char *data = malloc(length);
    if (!data) {
        fprintf(stderr, "writefile error: malloc failed\n");
        exit(1);
    }
    if ((int)fread(data, 1, length, stdin) != length) {
        fprintf(stderr, "writefile error: could not read %d bytes from stdin\n", length);
        free(data); exit(1);
    }

    /* how many blocks do we need after this write?
     * blocks_needed = ceil(new_size / BLOCKSIZE) */
    int new_size        = (start + length > files[slot].size) ? start + length : files[slot].size;
    int blocks_needed   = (new_size + BLOCKSIZE - 1) / BLOCKSIZE;
    int blocks_have     = count_file_blocks(fnodes, files[slot].firstblock);
    int blocks_to_alloc = blocks_needed - blocks_have;

    /* atomic check — do we have enough free blocks? if not, exit now */
    if (blocks_to_alloc > 0 && count_free_fnodes(fnodes) < blocks_to_alloc) {
        fprintf(stderr, "writefile error: not enough free blocks\n");
        free(data); exit(1);
    }

    /* find the tail of the chain so we can attach new blocks to it */
    int last = -1;
    int cur  = files[slot].firstblock;
    while (cur != -1 && fnodes[cur].nextblock != -1) {
        cur = fnodes[cur].nextblock;
    }
    last = cur;

    int new_fnode_indices[MAXBLOCKS];
    int new_fnode_count = 0;

    for (int i = 0; i < blocks_to_alloc; i++) {
        int nf = find_free_fnode(fnodes);

        fnodes[nf].blockindex = nf;
        fnodes[nf].nextblock  = -1;

        if (last == -1)
            files[slot].firstblock = nf;
        else
            fnodes[last].nextblock = nf;

        last = nf;
        new_fnode_indices[new_fnode_count++] = nf;
    }

    FILE *fp = openfs(fsfile, "r+");
    char zerobuf[BLOCKSIZE] = {0};

    /* zero out newly allocated blocks as required by spec */
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

    /* same navigation as readfile — skip to block containing byte 'start' */
    cur = files[slot].firstblock;
    int blocks_to_skip = start / BLOCKSIZE;
    for (int i = 0; i < blocks_to_skip; i++) {
        cur = fnodes[cur].nextblock;
    }

    int bytes_left = length;
    int file_pos   = start;
    int data_off   = 0;

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

    if (start + length > (int)files[slot].size)
        files[slot].size = start + length;

    save_metadata(fsfile, files, fnodes);
    free(data);
}