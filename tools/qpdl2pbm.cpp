/*
 * qpdl2pbm - decode a QPDL print job back into page images
 *
 * Reads a job as it would be sent to the printer (for instance a file
 * captured with Haiku's "Print to file" transport), checks its structure and
 * checksums, and writes every page as a PBM image.  This is the way to see
 * what the driver produced without owning the printer.
 *
 * Only what this driver emits is understood: monochrome pages compressed
 * with algorithm 0x11.
 *
 * The decoder follows SpliX's tools/algo0x11.cpp, (C) 2006-2008 Aurelien
 * Croc (AP2C).  Distributed under the GNU General Public License, version 2.
 *
 * Usage: qpdl2pbm [-q | -b] job.prn [output-prefix]
 *   Pages are written as <output-prefix>-<n>.pbm (default prefix: "page").
 *   -q  only check the job, do not write images.
 *   -b  write BMP files instead of PBM (they open in any image viewer).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static uint32_t
BE32(const uint8_t* p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
		| ((uint32_t)p[2] << 8) | p[3];
}


/*!	Decodes one band into \a out (bandSize bytes, column-major, inverted -
	exactly what the encoder fed to the compressor).
*/
static void
PutLE32(uint8_t* p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}


/*!	Writes a 1 bit per pixel BMP.  \a bitmap may be NULL for a blank page. */
static bool
WriteBMP(const char* name, const uint8_t* bitmap, uint32_t bytesPerLine,
	uint32_t rows, uint32_t xResolution, uint32_t yResolution)
{
	FILE* out = fopen(name, "wb");
	if (out == NULL) {
		perror(name);
		return false;
	}

	const uint32_t rowSize = (bytesPerLine + 3) & ~(uint32_t)3;
	uint8_t header[62];
	memset(header, 0, sizeof(header));
	header[0] = 'B';
	header[1] = 'M';
	PutLE32(header + 2, 62 + rowSize * rows);		// file size
	PutLE32(header + 10, 62);						// offset of the pixels
	PutLE32(header + 14, 40);						// info header size
	PutLE32(header + 18, bytesPerLine * 8);			// width
	PutLE32(header + 22, rows);						// height, bottom-up
	header[26] = 1;									// planes
	header[28] = 1;									// bits per pixel
	PutLE32(header + 34, rowSize * rows);
	PutLE32(header + 38, xResolution * 10000 / 254);	// pixels per metre
	PutLE32(header + 42, yResolution * 10000 / 254);
	PutLE32(header + 46, 2);						// palette entries
	header[54] = header[55] = header[56] = 0xff;	// 0 = white, 1 = black
	fwrite(header, 1, sizeof(header), out);

	uint8_t* row = (uint8_t*)calloc(rowSize, 1);
	if (row == NULL) {
		fclose(out);
		return false;
	}
	for (uint32_t y = rows; y-- > 0;) {
		if (bitmap != NULL)
			memcpy(row, bitmap + (size_t)y * bytesPerLine, bytesPerLine);
		fwrite(row, 1, rowSize, out);
	}
	free(row);
	return fclose(out) == 0;
}


static bool
Decode0x11(const uint8_t* in, size_t inSize, bool bigEndian, uint8_t* out,
	size_t bandSize)
{
	if (inSize < 4 + 128)
		return false;

	uint32_t literals;
	uint32_t table[64];
	if (bigEndian) {
		literals = BE32(in);
		for (int i = 0; i < 64; i++)
			table[i] = ((uint32_t)in[4 + 2 * i] << 8) | in[5 + 2 * i];
	} else {
		literals = in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16)
			| ((uint32_t)in[3] << 24);
		for (int i = 0; i < 64; i++)
			table[i] = in[4 + 2 * i] | ((uint32_t)in[5 + 2 * i] << 8);
	}

	size_t r = 4 + 128;
	size_t w = 0;
	if (literals > bandSize || r + literals > inSize)
		return false;
	memcpy(out, in + r, literals);
	r += literals;
	w += literals;

	while (r < inSize) {
		const uint8_t control = in[r++];
		if ((control & 0x80) != 0) {
			if (r >= inSize)
				return false;
			const uint8_t second = in[r++];
			const size_t length = (((size_t)second & 0xc0) << 1)
				+ (control & 0x7f) + 3;
			const uint32_t distance = table[second & 0x3f];
			if (distance == 0 || distance > w || w + length > bandSize)
				return false;
			for (size_t i = 0; i < length; i++, w++)
				out[w] = out[w - distance];
		} else {
			const size_t length = (size_t)control + 1;
			if (r + length > inSize || w + length > bandSize)
				return false;
			memcpy(out + w, in + r, length);
			r += length;
			w += length;
		}
	}

	return w == bandSize;
}


int
main(int argc, char** argv)
{
	bool quiet = false;
	bool bmp = false;
	int arg = 1;
	if (arg < argc && strcmp(argv[arg], "-q") == 0) {
		quiet = true;
		arg++;
	} else if (arg < argc && strcmp(argv[arg], "-b") == 0) {
		bmp = true;
		arg++;
	}
	if (arg >= argc) {
		fprintf(stderr, "usage: %s [-q | -b] job.prn [output-prefix]\n",
			argv[0]);
		return 1;
	}
	const char* path = argv[arg];
	const char* prefix = arg + 1 < argc ? argv[arg + 1] : "page";

	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	const long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint8_t* job = (uint8_t*)malloc(fileSize > 0 ? fileSize : 1);
	if (job == NULL || fread(job, 1, fileSize, file) != (size_t)fileSize) {
		fprintf(stderr, "%s: cannot read\n", path);
		return 1;
	}
	fclose(file);
	const size_t size = (size_t)fileSize;

	// PJL header
	static const char kEnter[] = "@PJL ENTER LANGUAGE = QPDL\n";
	const size_t enterLength = sizeof(kEnter) - 1;
	size_t pos = 0;
	bool found = false;
	for (; pos + enterLength <= size; pos++) {
		if (memcmp(job + pos, kEnter, enterLength) == 0) {
			found = true;
			break;
		}
	}
	if (!found || size < 9 || memcmp(job, "\033%-12345X", 9) != 0) {
		fprintf(stderr, "%s: not a QPDL job (PJL header missing)\n", path);
		return 1;
	}
	printf("--- PJL header ---\n");
	fwrite(job + 9, 1, pos + enterLength - 9, stdout);
	pos += enterLength;

	int pageCount = 0;
	bool inPage = false;
	uint8_t* bitmap = NULL;		// current page, row-major, 1 = black
	uint32_t bitmapBytes = 0;	// bytes per line
	uint32_t bitmapRows = 0;	// allocated rows
	uint32_t usedRows = 0;
	uint32_t bandCount = 0;
	uint32_t xResolution = 600;
	uint32_t yResolution = 600;
	size_t pageBytes = 0;
	int errors = 0;

	while (pos < size) {
		const uint8_t type = job[pos];

		if (type == 0x00 && pos + 17 <= size) {
			const uint8_t* h = job + pos;
			printf("--- page %d ---\n", pageCount + 1);
			printf("resolution %ux%u dpi, copies %u, paper code %u, "
				"size %ux%u (1/300 in), source %u\n", h[16] * 100, h[1] * 100,
				(h[2] << 8) | h[3], h[4], (h[5] << 8) | h[6],
				(h[7] << 8) | h[8], h[9]);
			printf("duplex %u, tumble %u, QPDL version %u, model bytes "
				"%u %u %u\n", h[11], h[12], h[14], h[10], h[13], h[15]);
			xResolution = h[16] * 100;
			yResolution = h[1] * 100;
			pos += 17;
			inPage = true;
			free(bitmap);
			bitmap = NULL;
			bitmapBytes = bitmapRows = usedRows = bandCount = 0;
			pageBytes = 0;
		} else if (type == 0x0c && inPage && pos + 15 <= size) {
			const uint8_t* h = job + pos;
			const uint32_t number = h[1];
			const uint32_t width = (h[2] << 8) | h[3];
			const uint32_t height = (h[4] << 8) | h[5];
			const uint8_t compression = h[6];
			const uint32_t payload = BE32(h + 7);
			if (compression != 0x11 || payload < 8 || width % 8 != 0
				|| height == 0 || pos + 11 + payload > size) {
				fprintf(stderr, "page %d: unsupported or damaged band record "
					"at offset %lu\n", pageCount + 1, (unsigned long)pos);
				return 1;
			}
			const uint8_t* signature = h + 11;
			bool bigEndian;
			if (signature[0] == 0xef && signature[1] == 0xcd
				&& signature[2] == 0xab && signature[3] == 0x09)
				bigEndian = false;
			else if (signature[0] == 0x09 && signature[1] == 0xab
				&& signature[2] == 0xcd && signature[3] == 0xef)
				bigEndian = true;
			else {
				fprintf(stderr, "page %d band %u: bad signature\n",
					pageCount + 1, number);
				return 1;
			}
			const uint8_t* data = h + 15;
			const size_t dataSize = payload - 8;

			uint32_t sum = 0;
			for (size_t i = 0; i < dataSize + 4; i++)
				sum += signature[i];
			if (sum != BE32(data + dataSize)) {
				fprintf(stderr, "page %d band %u: checksum mismatch\n",
					pageCount + 1, number);
				errors++;
			}

			const uint32_t bytes = width / 8;
			const size_t bandSize = (size_t)bytes * height;
			uint8_t* band = (uint8_t*)malloc(bandSize);
			if (band == NULL
				|| !Decode0x11(data, dataSize, bigEndian, band, bandSize)) {
				fprintf(stderr, "page %d band %u: cannot decode\n",
					pageCount + 1, number);
				return 1;
			}

			if (bitmapBytes == 0)
				bitmapBytes = bytes;
			if (bytes != bitmapBytes) {
				fprintf(stderr, "page %d: band width changes\n", pageCount + 1);
				return 1;
			}
			const uint32_t lastRow = (number + 1) * height;
			if (lastRow > bitmapRows) {
				const uint32_t rows = lastRow + 64 * height;
				bitmap = (uint8_t*)realloc(bitmap, (size_t)rows * bitmapBytes);
				if (bitmap == NULL)
					return 1;
				memset(bitmap + (size_t)bitmapRows * bitmapBytes, 0,
					(size_t)(rows - bitmapRows) * bitmapBytes);
				bitmapRows = rows;
			}
			if (lastRow > usedRows)
				usedRows = lastRow;

			// Band data is column-major and inverted.
			uint8_t* target = bitmap + (size_t)number * height * bitmapBytes;
			for (uint32_t x = 0; x < bytes; x++) {
				for (uint32_t y = 0; y < height; y++) {
					target[(size_t)y * bitmapBytes + x]
						= (uint8_t)~band[(size_t)x * height + y];
				}
			}
			free(band);

			bandCount++;
			pageBytes += dataSize;
			pos += 11 + payload;
		} else if (type == 0x01 && inPage && pos + 3 <= size) {
			pageCount++;
			printf("%u bands, %lu compressed bytes, image %ux%u\n", bandCount,
				(unsigned long)pageBytes, bitmapBytes * 8, usedRows);
			if (!quiet && bmp) {
				char name[1024];
				snprintf(name, sizeof(name), "%s-%d.bmp", prefix, pageCount);
				// A page without any band becomes a small white image.
				if (!WriteBMP(name, usedRows > 0 ? bitmap : NULL,
						bitmapBytes > 0 ? bitmapBytes : 4,
						usedRows > 0 ? usedRows : 1, xResolution,
						yResolution)) {
					return 1;
				}
			} else if (!quiet) {
				char name[1024];
				snprintf(name, sizeof(name), "%s-%d.pbm", prefix, pageCount);
				FILE* out = fopen(name, "wb");
				if (out == NULL) {
					perror(name);
					return 1;
				}
				// A page without any band is written as a 8x1 white image.
				const uint32_t outBytes = bitmapBytes > 0 ? bitmapBytes : 1;
				const uint32_t outRows = usedRows > 0 ? usedRows : 1;
				fprintf(out, "P4\n%u %u\n", outBytes * 8, outRows);
				if (bitmap != NULL)
					fwrite(bitmap, 1, (size_t)outBytes * outRows, out);
				else
					fputc(0, out);
				fclose(out);
			}
			pos += 3;
			inPage = false;
		} else if (type == 0x09 && !inPage && pos + 10 <= size
			&& memcmp(job + pos + 1, "\033%-12345X", 9) == 0) {
			pos += 10;
			if (pos != size)
				printf("note: %lu bytes follow the end of the job\n",
					(unsigned long)(size - pos));
			break;
		} else {
			fprintf(stderr, "unexpected byte 0x%02x at offset %lu\n", type,
				(unsigned long)pos);
			return 1;
		}
	}

	free(bitmap);
	free(job);
	printf("--- %d page(s), %d checksum error(s) ---\n", pageCount, errors);
	return errors == 0 ? 0 : 1;
}
