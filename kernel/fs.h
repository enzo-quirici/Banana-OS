#ifndef FS_H
#define FS_H

#include "types.h"

#define FS_MAX_FILES     32
#define FS_MAX_DIRS      16
#define FS_NAME_LEN      32
#define FS_CONTENT_LEN   2048
#define FS_PATH_LEN      128

typedef struct {
    char     name[FS_NAME_LEN];
    char     content[FS_CONTENT_LEN];
    int      used;
    int      parent_dir; /* index into dirs[] */
} fs_file_t;

typedef struct {
    char     name[FS_NAME_LEN];
    int      used;
    int      parent_dir; /* -1 = root */
} fs_dir_t;

void fs_init(void);

/* directory ops */
int  fs_mkdir(const char* name);           /* returns dir index or -1 */
int  fs_find_dir(const char* name);        /* in cwd */
void fs_ls(void);

/* file ops */
int  fs_create(const char* name);          /* returns file index or -1 */
int  fs_find_file(const char* name);       /* in cwd, returns index or -1 */
fs_file_t* fs_get_file(int idx);
void fs_delete(const char* name);

/* navigation */
void fs_cd(const char* name);
void fs_pwd(void);
const char* fs_cwd_name(void);

/* stats */
uint32_t fs_used_files(void);
uint32_t fs_used_dirs(void);
uint32_t fs_max_files(void);
uint32_t fs_max_dirs(void);
uint32_t fs_ram_used_bytes(void);

#endif
