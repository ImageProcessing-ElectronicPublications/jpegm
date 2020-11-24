#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "common.h"
#include "io.h"
#include "huffman.h"
#include "bits.h"

/* zig-zag scan to raster scan */
const uint8_t zigzag[64] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

const char *Pq_to_str[] = {
	[0] = "8-bit",
	[1] = "16-bit"
};

/* B.2.4.1 Quantization table-specification syntax */
int parse_qtable(FILE *stream, struct context *context)
{
	int err;
	uint8_t Pq, Tq;
	struct qtable *qtable;

	assert(context != NULL);

	err = read_nibbles(stream, &Pq, &Tq);
	RETURN_IF(err);

	printf("Pq = %" PRIu8 " (%s), Tq = %" PRIu8 " (QT identifier)\n", Pq, Pq_to_str[Pq], Tq);

	assert(Tq < 4);
	assert(Pq < 2);

	qtable = &context->qtable[Tq];

	qtable->precision = Pq;

	for (int i = 0; i < 64; ++i) {
		if (Pq == 0) {
			uint8_t byte;
			err = read_byte(stream, &byte);
			RETURN_IF(err);
			qtable->element[zigzag[i]] = (uint16_t)byte;
		} else {
			uint16_t word;
			err = read_word(stream, &word);
			RETURN_IF(err);
			qtable->element[zigzag[i]] = word;
		}
	}

	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			printf("%3" PRIu16 " ", qtable->element[y * 8 + x]);
		}
		printf("\n");
	}

	return RET_SUCCESS;
}

int parse_frame_header(FILE *stream, struct context *context)
{
	int err;
	/* Sample precision */
	uint8_t P;
	/* Number of lines, Number of samples per line */
	uint16_t Y, X;
	/* Number of image components in frame */
	uint8_t Nf;

	assert(context != NULL);

	err = read_byte(stream, &P);
	RETURN_IF(err);
	err = read_word(stream, &Y);
	RETURN_IF(err);
	err = read_word(stream, &X);
	RETURN_IF(err);
	err = read_byte(stream, &Nf);
	RETURN_IF(err);

	assert(P == 8);
	assert(X > 0);
	assert(Nf > 0);

	printf("P = %" PRIu8 " (Sample precision), Y = %" PRIu16 ", X = %" PRIu16 ", Nf = %" PRIu8 " (Number of image components)\n", P, Y, X, Nf);

	context->precision = P;
	context->Y = Y;
	context->X = X;
	context->components = Nf;

	for (int i = 0; i < Nf; ++i) {
		uint8_t C;
		uint8_t H, V;
		uint8_t Tq;

		err = read_byte(stream, &C);
		RETURN_IF(err);
		err = read_nibbles(stream, &H, &V);
		RETURN_IF(err);
		err = read_byte(stream, &Tq);
		RETURN_IF(err);

		printf("C = %" PRIu8 " (Component identifier), H = %" PRIu8 ", V = %" PRIu8 ", Tq = %" PRIu8 " (QT identifier)\n", C, H, V, Tq);

		context->component[C].H = H;
		context->component[C].V = V;
		context->component[C].Tq = Tq;
	}

	return RET_SUCCESS;
}

const char *Tc_to_str[] = {
	[0] = "DC table",
	[1] = "AC table"
};

int parse_huffman_tables(FILE *stream, struct context *context)
{
	int err;
	uint8_t Tc, Th;

	assert(context != NULL);

	err = read_nibbles(stream, &Tc, &Th);
	RETURN_IF(err);

	printf("Tc = %" PRIu8 " (%s) Th = %" PRIu8 " (HT identifier)\n", Tc, Tc_to_str[Tc], Th);

	struct htable *htable = &context->htable[Th];

	htable->Tc = Tc;

	for (int i = 0; i < 16; ++i) {
		err = read_byte(stream, &htable->L[i]);
		RETURN_IF(err);
	}

	uint8_t *v_ = htable->V_;

	for (int i = 0; i < 16; ++i) {
		uint8_t L = htable->L[i];

		for (int l = 0; l < L; ++l) {
			err = read_byte(stream, &htable->V[i][l]);
			RETURN_IF(err);

			*v_ = htable->V[i][l];
			v_++;
		}
	}

	/* Annex C */
	struct hcode *hcode = &context->hcode[Th];
	err = conv_htable_to_hcode(htable, hcode);
	RETURN_IF(err);

	return RET_SUCCESS;
}

int parse_scan_header(FILE *stream, struct context *context)
{
	int err;
	/* Number of image components in scan */
	uint8_t Ns;

	err = read_byte(stream, &Ns);
	RETURN_IF(err);

	printf("Ns = %" PRIu8 " (Number of image components in scan)\n", Ns);

	for (int j = 0; j < Ns; ++j) {
		uint8_t Cs;
		uint8_t Td, Ta;

		err = read_byte(stream, &Cs);
		RETURN_IF(err);
		err = read_nibbles(stream, &Td, &Ta);
		RETURN_IF(err);

		printf("Cs%i = %" PRIu8 " (Component identifier), Td%i = %" PRIu8 " (DC HT identifier), Ta%i = %" PRIu8 " (AC HT identifier)\n", j, Cs, j, Td, j, Ta);

		context->component[Cs].Td = Td;
		context->component[Cs].Ta = Ta;
	}

	uint8_t Ss;
	uint8_t Se;
	uint8_t Ah, Al;

	err = read_byte(stream, &Ss);
	RETURN_IF(err);
	err = read_byte(stream, &Se);
	RETURN_IF(err);
	err = read_nibbles(stream, &Ah, &Al);
	RETURN_IF(err);

// 	assert(Ss == 0);
// 	assert(Se == 63);
	printf("Ss = %" PRIu8 " (the first DCT coefficient), Se = %" PRIu8 " (the last DCT coefficient)\n", Ss, Se);
// 	assert(Ah == 0);
// 	assert(Al == 0);
	printf("Ah = %" PRIu8 " (bit position high), Al = %" PRIu8 " (bit position low)\n", Ah, Al);

	return RET_SUCCESS;
}

int read_ecs(FILE *stream)
{
#if 0
	int err;
	size_t bytes = 0;

	do {
		uint8_t byte;

		err = read_ecs_byte(stream, &byte);
		switch (err) {
			case RET_SUCCESS:
				bytes++;
				continue;
			case RET_FAILURE_NO_MORE_DATA:
				goto end;
			default:
				return err;
		}
	} while (1);
end:
	printf("*** %zu bytes discarded ***\n", bytes);

	return RET_SUCCESS;
#else
	int err;
	struct bits bits;
	size_t count = 0;

	init_bits(&bits, stream);

	do {
		uint8_t bit;

		err = next_bit(&bits, &bit);
		switch (err) {
			case RET_SUCCESS:
				count++;
				continue;
			case RET_FAILURE_NO_MORE_DATA:
				goto end;
			default:
				return err;
		}
	} while (1);
end:
	printf("*** %zu bytes discarded ***\n", count / 8);

	return RET_SUCCESS;
#endif
}

int parse_restart_interval(FILE *stream, struct context *context)
{
	int err;
	uint16_t Ri;

	err = read_word(stream, &Ri);
	RETURN_IF(err);

	context->Ri = Ri;

	return RET_SUCCESS;
}

int parse_comment(FILE *stream, uint16_t len)
{
	size_t l = len - 2;

	char *buf = malloc(l + 1);

	if (buf == NULL) {
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	if (fread(buf, sizeof(char), l, stream) != l) {
		free(buf);
		return RET_FAILURE_FILE_IO;
	}

	buf[l] = 0;

	printf("%s\n", buf);

	free(buf);

	return RET_SUCCESS;
}

int parse_format(FILE *stream, struct context *context)
{
	int err;

	while (1) {
		uint16_t marker;

		err = read_marker(stream, &marker);
		RETURN_IF(err);

		/* An asterisk (*) indicates a marker which stands alone,
		 * that is, which is not the start of a marker segment. */
		switch (marker) {
			uint16_t len;
			long pos;
			/* SOI* Start of image */
			case 0xffd8:
				printf("SOI\n");
				break;
			/* APP0 */
			case 0xffe0:
				printf("APP0\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = skip_segment(stream, len);
				RETURN_IF(err);
				break;
			/* APP1 */
			case 0xffe1:
				printf("APP1\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = skip_segment(stream, len);
				RETURN_IF(err);
				break;
			/* APP2 */
			case 0xffe2:
				printf("APP2\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = skip_segment(stream, len);
				RETURN_IF(err);
				break;
			/* APP13 */
			case 0xffed:
				printf("APP13\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = skip_segment(stream, len);
				RETURN_IF(err);
				break;
			/* DQT Define quantization table(s) */
			case 0xffdb:
				printf("DQT\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_qtable(stream, context);
				RETURN_IF(err);
				break;
			/* SOF0 Baseline DCT */
			case 0xffc0:
				printf("SOF0\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_frame_header(stream, context);
				RETURN_IF(err);
				break;
			/* SOF2 Progressive DCT */
			case 0xffc2:
				printf("SOF2\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_frame_header(stream, context);
				RETURN_IF(err);
				break;
			/* DHT Define Huffman table(s) */
			case 0xffc4:
				printf("DHT\n");
				pos = ftell(stream);
				err = read_length(stream, &len);
				RETURN_IF(err);
				/* parse multiple tables in single DHT */
				do {
					err = parse_huffman_tables(stream, context);
					RETURN_IF(err);
				} while (ftell(stream) < pos + len);
				break;
			/* SOS Start of scan */
			case 0xffda:
				printf("SOS\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_scan_header(stream, context);
				RETURN_IF(err);
				err = read_ecs(stream);
				RETURN_IF(err);
				break;
			/* EOI* End of image */
			case 0xffd9:
				printf("EOI\n");
				return RET_SUCCESS;
			/* DRI Define restart interval */
			case 0xffdd:
				printf("DRI\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_restart_interval(stream, context);
				RETURN_IF(err);
				break;
			/* RSTm* Restart with modulo 8 count “m” */
			case 0xffd0:
			case 0xffd1:
			case 0xffd2:
			case 0xffd3:
			case 0xffd4:
			case 0xffd5:
			case 0xffd6:
			case 0xffd7:
				break;
			/* COM Comment */
			case 0xfffe:
				printf("COM\n");
				err = read_length(stream, &len);
				RETURN_IF(err);
				err = parse_comment(stream, len);
				RETURN_IF(err);
				break;
			default:
				fprintf(stderr, "unhandled marker 0x%" PRIx16 "\n", marker);
				return RET_FAILURE_FILE_UNSUPPORTED;
		}
	}
}

int process_jpeg_stream(FILE *stream)
{
	int err;

	struct context *context = malloc(sizeof(struct context));

	if (context == NULL) {
		fprintf(stderr, "malloc failure\n");
		return RET_FAILURE_MEMORY_ALLOCATION;
	}

	err = init_context(context);

	if (err) {
		goto end;
	}

	err = parse_format(stream, context);
end:
	free(context);

	return err;
}

int process_jpeg_file(const char *path)
{
	FILE *stream = fopen(path, "r");

	if (stream == NULL) {
		fprintf(stderr, "fopen failure\n");
		return RET_FAILURE_FILE_OPEN;
	}

	int err = process_jpeg_stream(stream);

	fclose(stream);

	return err;
}

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "Car3.jpg";

	process_jpeg_file(path);

	return 0;
}
