/* The storage memory (\\fls0\). Every BFile call runs in the OS world (gint_world_switch).
 * Names are plain ASCII relative to \\fls0\, with a folder if any ("TI68K\\TI89.rom"). */
#ifndef TI68K_FILES_H
#define TI68K_FILES_H

#include <stdint.h>

#define FILES_NAME_MAX 48

int files_open_read(const char *name);       /* handle or < 0 */
int files_create(const char *name);          /* removes an old file; handle open for writing */
int files_read(int h, void *buf, int size, int pos);
int files_write(int h, const void *buf, int size);  /* size must be even */
int files_size(int h);
int files_close(int h);
int files_remove(const char *name);
int files_rename(const char *from, const char *to);
int files_exists(const char *name);          /* its size, or < 0 */
int files_read_at(const char *name, void *buf, int size, int pos); /* open, read, close */

typedef struct {
	char name[FILES_NAME_MAX];   /* with the folder */
	uint32_t size;
} files_entry_t;

/* the files (not folders) in dir ("" = the root, "TI68K\\" = a folder); returns how many */
int files_list(const char *dir, files_entry_t *out, int max);

#endif
