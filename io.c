#include <arpa/inet.h>
#include <assert.h>
#include "io.h"
#include "common.h"

int read_byte(FILE *stream, uint8_t *byte)
{
	if (fread(byte, sizeof(uint8_t), 1, stream) != 1) {
		return RET_FAILURE_FILE_IO;
	}

	return RET_SUCCESS;
}

int read_word(FILE *stream, uint16_t *word)
{
	uint16_t w;

	if (fread(&w, sizeof(uint16_t), 1, stream) != 1) {
		return RET_FAILURE_FILE_IO;
	}

	w = ntohs(w);

	assert(word != NULL);

	*word = w;

	return RET_SUCCESS;
}

int read_length(FILE *stream, uint16_t *len)
{
	int err;

	err = read_word(stream, len);
	RETURN_IF(err);

	return RET_SUCCESS;
}

int read_nibbles(FILE *stream, uint8_t *first, uint8_t *second)
{
	int err;
	uint8_t byte;

	assert(first != NULL);
	assert(second != NULL);

	err = read_byte(stream, &byte);
	RETURN_IF(err);

	/* The first 4-bit parameter of the pair shall occupy the most significant 4 bits of the byte.  */
	*first = (byte >> 4) & 15;
	*second = (byte >> 0) & 15;

	return RET_SUCCESS;
}

/* B.1.1.2 Markers
 * All markers are assigned two-byte codes */
int read_marker(FILE *stream, uint16_t *marker)
{
	int err;
	uint8_t byte;

	/* Any marker may optionally be preceded by any
	 * number of fill bytes, which are bytes assigned code X’FF’. */

	long start = ftell(stream), end;

	seek: do {
		err = read_byte(stream, &byte);
		RETURN_IF(err);
	} while (byte != 0xff);

	do {
		err = read_byte(stream, &byte);
		RETURN_IF(err);

		switch (byte) {
			case 0xff:
				continue;
			/* not a marker */
			case 0x00:
				goto seek;
			default:
				end = ftell(stream);
				if (end - start != 2) {
					printf("*** %li bytes skipped ***\n", end - start - 2);
				}
				*marker = UINT16_C(0xff00) | byte;
				return RET_SUCCESS;
		}
	} while (1);
}

int skip_segment(FILE *stream, uint16_t len)
{
	if (fseek(stream, (long)len - 2, SEEK_CUR) != 0) {
		return RET_FAILURE_FILE_SEEK;
	}

	return RET_SUCCESS;
}