#include "fs.h"
#include "terminal.h"
#include "types.h"

static fs_dir_t  dirs[FS_MAX_DIRS];
static fs_file_t files[FS_MAX_FILES];
static int       cwd = 0;   /* current dir index (0 = root) */

/* ── string helpers ─────────────────────────────────────────────── */
static void k_strcpy(char* dst, const char* src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}
static int k_strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static uint32_t k_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ── init ────────────────────────────────────────────────────────── */
void fs_init(void) {
    for (int i = 0; i < FS_MAX_DIRS;  i++) dirs[i].used  = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) files[i].used = 0;

    /* create root dir */
    dirs[0].used       = 1;
    dirs[0].parent_dir = -1;
    k_strcpy(dirs[0].name, "/", FS_NAME_LEN);
    cwd = 0;
}

/* ── directory ops ──────────────────────────────────────────────── */
int fs_mkdir(const char* name) {
    /* check duplicate */
    for (int i = 0; i < FS_MAX_DIRS; i++)
        if (dirs[i].used && dirs[i].parent_dir == cwd &&
            k_strcmp(dirs[i].name, name) == 0)
            return -1;

    for (int i = 1; i < FS_MAX_DIRS; i++) {
        if (!dirs[i].used) {
            dirs[i].used       = 1;
            dirs[i].parent_dir = cwd;
            k_strcpy(dirs[i].name, name, FS_NAME_LEN);
            return i;
        }
    }
    return -1;
}

int fs_find_dir(const char* name) {
    for (int i = 0; i < FS_MAX_DIRS; i++)
        if (dirs[i].used && dirs[i].parent_dir == cwd &&
            k_strcmp(dirs[i].name, name) == 0)
            return i;
    return -1;
}

void fs_ls(void) {
    int found = 0;
    /* list dirs */
    for (int i = 0; i < FS_MAX_DIRS; i++) {
        if (dirs[i].used && dirs[i].parent_dir == cwd) {
            terminal_write_color(dirs[i].name, VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            terminal_write("/  ");
            found = 1;
        }
    }
    /* list files */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && files[i].parent_dir == cwd) {
            terminal_write(files[i].name);
            terminal_write("  ");
            found = 1;
        }
    }
    if (found) terminal_putchar('\n');
    else terminal_writeln("(empty)");
}

/* ── file ops ───────────────────────────────────────────────────── */
int fs_create(const char* name) {
    /* check duplicate */
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && files[i].parent_dir == cwd &&
            k_strcmp(files[i].name, name) == 0)
            return i;  /* already exists, return it */

    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (!files[i].used) {
            files[i].used       = 1;
            files[i].parent_dir = cwd;
            files[i].content[0] = '\0';
            k_strcpy(files[i].name, name, FS_NAME_LEN);
            return i;
        }
    }
    return -1;
}

int fs_find_file(const char* name) {
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && files[i].parent_dir == cwd &&
            k_strcmp(files[i].name, name) == 0)
            return i;
    return -1;
}

fs_file_t* fs_get_file(int idx) {
    if (idx < 0 || idx >= FS_MAX_FILES) return (void*)0;
    return &files[idx];
}

void fs_delete(const char* name) {
    /* try file first */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used && files[i].parent_dir == cwd &&
            k_strcmp(files[i].name, name) == 0) {
            files[i].used = 0;
            return;
        }
    }
    /* try dir */
    for (int i = 1; i < FS_MAX_DIRS; i++) {
        if (dirs[i].used && dirs[i].parent_dir == cwd &&
            k_strcmp(dirs[i].name, name) == 0) {
            dirs[i].used = 0;
            return;
        }
    }
    terminal_write_color("rm: not found: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    terminal_writeln(name);
}

/* ── navigation ─────────────────────────────────────────────────── */
void fs_cd(const char* name) {
    if (k_strcmp(name, "..") == 0) {
        if (dirs[cwd].parent_dir >= 0)
            cwd = dirs[cwd].parent_dir;
        return;
    }
    if (k_strcmp(name, "/") == 0) { cwd = 0; return; }
    int idx = fs_find_dir(name);
    if (idx < 0) {
        terminal_write_color("cd: no such directory: ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        terminal_writeln(name);
        return;
    }
    cwd = idx;
}

void fs_pwd(void) {
    /* walk up, collect path */
    char parts[8][FS_NAME_LEN];
    int  depth = 0;
    int  cur   = cwd;
    while (cur != 0 && depth < 8) {
        int i = 0;
        while (dirs[cur].name[i]) { parts[depth][i] = dirs[cur].name[i]; i++; }
        parts[depth][i] = '\0';
        depth++;
        cur = dirs[cur].parent_dir;
        if (cur < 0) break;
    }
    terminal_putchar('/');
    for (int d = depth - 1; d >= 0; d--) {
        terminal_write(parts[d]);
        if (d > 0) terminal_putchar('/');
    }
    terminal_putchar('\n');
}

const char* fs_cwd_name(void) {
    return dirs[cwd].name;
}

uint32_t fs_used_files(void) {
    uint32_t n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) if (files[i].used) n++;
    return n;
}

uint32_t fs_used_dirs(void) {
    uint32_t n = 0;
    for (int i = 0; i < FS_MAX_DIRS; i++) if (dirs[i].used) n++;
    return n;
}

uint32_t fs_max_files(void) { return FS_MAX_FILES; }
uint32_t fs_max_dirs(void)  { return FS_MAX_DIRS; }

uint32_t fs_ram_used_bytes(void) {
    /* The filesystem tables are static in-memory arrays in BSS. */
    uint32_t used = (uint32_t)sizeof(dirs) + (uint32_t)sizeof(files);
    /* Add currently used payload bytes for a more intuitive "used". */
    for (int i = 0; i < FS_MAX_FILES; i++) {
        if (files[i].used) used += k_strlen(files[i].content);
    }
    return used;
}
