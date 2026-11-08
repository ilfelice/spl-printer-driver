/*
 * mkraster - wrap PBM (P4) pages into a CUPS raster stream.
 *
 * Test-only helper: it produces the input that SpliX's rastertoqpdl filter
 * expects, so the reference driver and this project's encoder can be fed
 * exactly the same pixels.
 *
 * Usage: mkraster xres yres pageWpt pageHpt marginLpt marginBpt compression
 *                 copies page1.pbm [page2.pbm ...] > out.ras
 */
#include <cups/raster.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char *read_pbm(const char *path, unsigned *w, unsigned *h)
{
    FILE *f = fopen(path, "rb");
    char magic[3] = {0, 0, 0};
    unsigned char *data;
    size_t size;
    int c;

    if (!f) { perror(path); exit(1); }
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P4")) {
        fprintf(stderr, "%s: not a P4 PBM\n", path); exit(1);
    }
    /* skip whitespace and comments */
    for (;;) {
        c = fgetc(f);
        if (c == '#') { while ((c = fgetc(f)) != '\n' && c != EOF) {} }
        else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') { ungetc(c, f); break; }
    }
    if (fscanf(f, "%u %u", w, h) != 2) { fprintf(stderr, "bad PBM header\n"); exit(1); }
    fgetc(f); /* single whitespace */
    size = (size_t)((*w + 7) / 8) * *h;
    data = malloc(size);
    if (fread(data, 1, size, f) != size) { fprintf(stderr, "short PBM\n"); exit(1); }
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    cups_raster_t *ras;
    int i;

    if (argc < 10) {
        fprintf(stderr, "usage: %s xres yres pageW pageH marginL marginB "
            "compression copies page.pbm...\n", argv[0]);
        return 1;
    }
    ras = cupsRasterOpen(STDOUT_FILENO, CUPS_RASTER_WRITE);
    for (i = 9; i < argc; i++) {
        cups_page_header2_t h;
        unsigned w, hgt, y, bpl;
        unsigned char *data = read_pbm(argv[i], &w, &hgt);

        memset(&h, 0, sizeof(h));
        h.HWResolution[0] = atoi(argv[1]);
        h.HWResolution[1] = atoi(argv[2]);
        h.PageSize[0] = atoi(argv[3]);
        h.PageSize[1] = atoi(argv[4]);
        h.cupsPageSize[0] = atof(argv[3]);
        h.cupsPageSize[1] = atof(argv[4]);
        h.Margins[0] = atoi(argv[5]);
        h.Margins[1] = atoi(argv[6]);
        h.cupsCompression = atoi(argv[7]);
        h.NumCopies = atoi(argv[8]);
        h.cupsWidth = w;
        h.cupsHeight = hgt;
        h.cupsBitsPerColor = 1;
        h.cupsBitsPerPixel = 1;
        h.cupsNumColors = 1;
        h.cupsColorSpace = CUPS_CSPACE_K;
        h.cupsColorOrder = CUPS_ORDER_CHUNKED;
        bpl = (w + 7) / 8;
        h.cupsBytesPerLine = bpl;
        if (!cupsRasterWriteHeader2(ras, &h)) { fprintf(stderr, "header write failed\n"); return 1; }
        for (y = 0; y < hgt; y++)
            if (cupsRasterWritePixels(ras, data + (size_t)y * bpl, bpl) != bpl) {
                fprintf(stderr, "pixel write failed\n"); return 1;
            }
        free(data);
    }
    cupsRasterClose(ras);
    return 0;
}
