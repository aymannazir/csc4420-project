#include <stdio.h>
#include "simfstypes.h"

/* File system setup */
void printfs(char *);
void initfs(char *);

/* Low-level file helpers */
FILE *openfs(char *filename, char *mode);
void  closefs(FILE *fp);

/* Metadata helpers */
void load_metadata(char *fsfile, fentry *files, fnode *fnodes);
void save_metadata(char *fsfile, fentry *files, fnode *fnodes);
int  find_file(fentry *files, char *name);
int  count_free_fnodes(fnode *fnodes);
int  find_free_fnode(fnode *fnodes);
int  count_file_blocks(fnode *fnodes, int firstblock);

/* The four file system operations */
void createfile(char *fsfile, char *name);
void deletefile(char *fsfile, char *name);
void readfile(char *fsfile, char *name, int start, int length);
void writefile(char *fsfile, char *name, int start, int length);
