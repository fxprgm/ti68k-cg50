/* A minimal PNG writer (8-bit gray, stored deflate blocks) for the desktop tools. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- PNG (stored deflate blocks) ----------------------------------------------------------- */

static uint32_t crc_table[256];
static uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t k = i;
			for (int j = 0; j < 8; j++) k = k & 1 ? 0xedb88320u ^ (k >> 1) : k >> 1;
			crc_table[i] = k;
		}
	c = ~c;
	while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n)
{
	uint8_t h[8];
	put32(h, n);
	memcpy(h + 4, type, 4);
	fwrite(h, 1, 8, f);
	if (n) fwrite(data, 1, n, f);
	uint32_t c = crc32_update(0, (const uint8_t *)type, 4);
	c = crc32_update(c, data, n);
	put32(h, c);
	fwrite(h, 1, 4, f);
}

/* gray 8-bit image, scaled by `s` */
void ti_write_png_gray(const char *path, const uint8_t *pix, int w, int h, int s)
{
	int W = w * s, H = h * s;
	uint32_t raw_n = (uint32_t)(H * (W + 1));
	uint8_t *raw = malloc(raw_n);
	for (int y = 0; y < H; y++) {
		raw[y * (W + 1)] = 0;
		for (int x = 0; x < W; x++) raw[y * (W + 1) + 1 + x] = pix[(y / s) * w + x / s];
	}
	uint32_t nblk = (raw_n + 65534) / 65535;
	uint8_t *z = malloc(raw_n + nblk * 5 + 6);
	uint32_t zn = 0, a = 1, b = 0;
	z[zn++] = 0x78; z[zn++] = 0x01;
	for (uint32_t off = 0; off < raw_n; off += 65535) {
		uint32_t n = raw_n - off < 65535 ? raw_n - off : 65535;
		z[zn++] = off + n == raw_n;
		z[zn++] = n & 0xff; z[zn++] = n >> 8; z[zn++] = ~n & 0xff; z[zn++] = (~n >> 8) & 0xff;
		memcpy(z + zn, raw + off, n);
		zn += n;
	}
	for (uint32_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	put32(z + zn, b << 16 | a);
	zn += 4;
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); exit(1); }
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	uint8_t ihdr[13];
	put32(ihdr, (uint32_t)W); put32(ihdr + 4, (uint32_t)H);
	ihdr[8] = 8; ihdr[9] = 0; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
	png_chunk(f, "IHDR", ihdr, 13);
	png_chunk(f, "IDAT", z, zn);
	png_chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
}

