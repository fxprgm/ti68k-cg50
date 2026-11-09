/* Storage memory access through BFile, each call in the OS world. */
#include "files.h"

#include <gint/bfile.h>
#include <gint/gint.h>
#include <string.h>

enum { F_OPEN, F_CREATE, F_READ, F_WRITE, F_SIZE, F_CLOSE, F_REMOVE, F_RENAME, F_EXISTS, F_READ_AT, F_LIST };

static struct {
	int op, h, size, pos;
	void *buf;
	const void *cbuf;
	uint16_t path[FILES_NAME_MAX + 16], path2[FILES_NAME_MAX + 16];
	/* listing */
	const char *dir;
	files_entry_t *out;
	int max, n;
} req;

static void to_fc(uint16_t *dst, const char *name, int star)
{
	const char *pre = "\\\\fls0\\";
	int n = 0;
	while (*pre)
		dst[n++] = (uint8_t)*pre++;
	while (*name && n < FILES_NAME_MAX + 12)
		dst[n++] = (uint8_t)*name++;
	if (star)
		dst[n++] = '*';
	dst[n] = 0;
}

/* the last path component of a found name, as ASCII; 0 if it isn't plain ASCII or too long */
static int from_fc(const uint16_t *fc, char *out, int max)
{
	const uint16_t *base = fc;
	for (const uint16_t *p = fc; *p; p++)
		if (*p == '\\')
			base = p + 1;
	int n = 0;
	for (const uint16_t *p = base; *p; p++) {
		if (*p < 0x20 || *p >= 0x7f || n >= max - 1)
			return 0;
		out[n++] = (char)*p;
	}
	out[n] = 0;
	return n > 0;
}

static int list_files(void)
{
	uint16_t found[0x110];      /* BFile writes up to 0x10A characters */
	struct BFile_FileInfo fi;
	int sh;
	int rc = BFile_FindFirst(req.path, &sh, found, &fi);
	if (rc < 0)
		return 0;
	int dl = strlen(req.dir);
	while (rc >= 0 && req.n < req.max) {
		if (fi.type != BFile_Type_Dot && fi.type != BFile_Type_DotDot && fi.type != BFile_Type_Volume
		    && fi.type != BFile_Type_MainMem) {
			char name[FILES_NAME_MAX];
			if (from_fc(found, name, sizeof name) && dl + (int)strlen(name) + 1 < FILES_NAME_MAX) {
				files_entry_t *e = &req.out[req.n++];
				strcpy(e->name, req.dir);
				strcpy(e->name + dl, name);
				e->dir = fi.type == BFile_Type_Directory;
				e->size = e->dir ? 0 : fi.file_size;
			}
		}
		rc = BFile_FindNext(sh, found, &fi);
	}
	BFile_FindClose(sh);
	return 0;
}

static int worker(void)
{
	switch (req.op) {
	case F_OPEN:
		return BFile_Open(req.path, BFile_ReadOnly);
	case F_CREATE: {
		BFile_Remove(req.path);
		int size = 0;
		int rc = BFile_Create(req.path, BFile_File, &size);
		return rc < 0 ? rc : BFile_Open(req.path, BFile_WriteOnly);
	}
	case F_READ:
		return BFile_Read(req.h, req.buf, req.size, req.pos);
	case F_WRITE:
		return BFile_Write(req.h, req.cbuf, req.size);
	case F_SIZE:
		return BFile_Size(req.h);
	case F_CLOSE:
		return BFile_Close(req.h);
	case F_REMOVE:
		return BFile_Remove(req.path);
	case F_RENAME:
		return BFile_Rename(req.path, req.path2);
	case F_EXISTS:
	case F_READ_AT: {
		int h = BFile_Open(req.path, BFile_ReadOnly);
		if (h < 0)
			return h;
		int rc = req.op == F_EXISTS ? BFile_Size(h) : BFile_Read(h, req.buf, req.size, req.pos);
		BFile_Close(h);
		return rc;
	}
	case F_LIST:
		return list_files();
	}
	return -1;
}

static int run(int op)
{
	req.op = op;
	return gint_world_switch(GINT_CALL(worker));
}

int files_open_read(const char *name) { to_fc(req.path, name, 0); return run(F_OPEN); }
int files_create(const char *name) { to_fc(req.path, name, 0); return run(F_CREATE); }
int files_remove(const char *name) { to_fc(req.path, name, 0); return run(F_REMOVE); }
int files_exists(const char *name) { to_fc(req.path, name, 0); return run(F_EXISTS); }
int files_size(int h) { req.h = h; return run(F_SIZE); }
int files_close(int h) { req.h = h; return run(F_CLOSE); }

int files_rename(const char *from, const char *to)
{
	to_fc(req.path, from, 0);
	to_fc(req.path2, to, 0);
	return run(F_RENAME);
}

int files_read(int h, void *buf, int size, int pos)
{
	req.h = h, req.buf = buf, req.size = size, req.pos = pos;
	return run(F_READ);
}

int files_read_at(const char *name, void *buf, int size, int pos)
{
	to_fc(req.path, name, 0);
	req.buf = buf, req.size = size, req.pos = pos;
	return run(F_READ_AT);
}

int files_write(int h, const void *buf, int size)
{
	req.h = h, req.cbuf = buf, req.size = size;
	return run(F_WRITE);
}

int files_list(const char *dir, files_entry_t *out, int max)
{
	to_fc(req.path, dir, 1);
	req.dir = dir, req.out = out, req.max = max, req.n = 0;
	run(F_LIST);
	return req.n;
}
