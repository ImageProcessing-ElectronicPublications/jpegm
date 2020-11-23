#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "common.h"
#include "io.h"

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

/* B.2.4.1 Quantization table-specification syntax */
int parse_qtable(FILE *stream, struct context *context)
{
	int err;
	uint8_t Pq, Tq;
	struct qtable *qtable;

	assert(context != NULL);

	err = read_nibbles(stream, &Pq, &Tq);
	RETURN_IF(err);

	printf("Pq = %" PRIu8 " Tq = %" PRIu8 "\n", Pq, Tq);

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

	printf("P = %" PRIu8 " Y = %" PRIu16 " X = %" PRIu16 " Nf = %" PRIu8 "\n", P, Y, X, Nf);

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

		printf("C = %" PRIu8 " H = %" PRIu8 " V = %" PRIu8 " Tq = %" PRIu8 "\n", C, H, V, Tq);

		context->component[C].H = H;
		context->component[C].V = V;
		context->component[C].Tq = Tq;
	}

	return RET_SUCCESS;
}

struct hcode {
	size_t huff_size[256];
	size_t last_k;
	uint16_t huff_code[256];
	uint16_t e_huf_co[256];
	size_t e_huf_si[256];
};

/* Figure C.1 – Generation of table of Huffman code sizes */
int generate_size_table(struct htable *htable, struct hcode *hcode)
{
	assert(htable != NULL);
	assert(hcode != NULL);

#define BITS(I)     (htable->L[(I) - 1])
#define HUFFSIZE(K) (hcode->huff_size[(K)])
#define LASTK       (hcode->last_k)

	size_t K = 0;
	size_t I = 1;
	size_t J = 1;

	do {
		while (J <= BITS(I)) {
			assert(K < 256);
			HUFFSIZE(K) = I;
			K++;
			J++;
		}
		I++;
		J = 1;
	} while (I <= 16);
	assert(K < 256);
	HUFFSIZE(K) = 0;
	LASTK = K;

#undef BITS
#undef HUFFSIZE
#undef LASTK

	printf("[DEBUG] last_k = %zu\n", hcode->last_k);

	return 0;
}

/* Figure C.2 – Generation of table of Huffman codes */
int generate_code_table(struct htable *htable, struct hcode *hcode)
{
	assert(htable != NULL);
	assert(hcode != NULL);

#define HUFFSIZE(K) (hcode->huff_size[(K)])
#define HUFFCODE(K) (hcode->huff_code[(K)])

	size_t K = 0;
	uint16_t CODE = 0;
	size_t SI = HUFFSIZE(0);

	do {
		do {
			assert(K < 256);
			HUFFCODE(K) = CODE;
			CODE++;
			K++;
			assert(K < 256);
		} while (HUFFSIZE(K) == SI);

		assert(K < 256);
		if (HUFFSIZE(K) == 0) {
			return 0;
		}

		do {
			CODE <<= 1;
			SI++;
			assert(K < 256);
		} while (HUFFSIZE(K) != SI);
	} while (1);

#undef HUFFSIZE
#undef HUFFCODE
}

/* Figure C.3 – Ordering procedure for encoding procedure code tables */
int order_codes(struct htable *htable, struct hcode *hcode)
{
	assert(htable != NULL);
	assert(hcode != NULL);

#define HUFFVAL(K)  (htable->V_[(K)])
#define EHUFCO(I)   (hcode->e_huf_co[(I)])
#define EHUFSI(I)   (hcode->e_huf_si[(I)])
#define LASTK       (hcode->last_k)
#define HUFFSIZE(K) (hcode->huff_size[(K)])
#define HUFFCODE(K) (hcode->huff_code[(K)])

	size_t K = 0;

	do {
		uint8_t I = HUFFVAL(K);
		EHUFCO(I) = HUFFCODE(K);
		EHUFSI(I) = HUFFSIZE(K);
		printf("[DEBUG] val=%i size=%zu code=%" PRIu16 "\n", I, EHUFSI(I), EHUFCO(I));
		K++;
	} while (K < LASTK);

#undef HUFFVAL
#undef EHUFCO
#undef EHUFSI
#undef LASTK
#undef HUFFSIZE
#undef HUFFCODE

	return 0;
}

int parse_huffman_tables(FILE *stream, struct context *context)
{
	int err;
	uint8_t Tc, Th;

	assert(context != NULL);

	err = read_nibbles(stream, &Tc, &Th);
	RETURN_IF(err);

	printf("Tc = %" PRIu8 " Th = %" PRIu8 "\n", Tc, Th);

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

#if 0
	/* Annex C */
	struct hcode hcode;
	generate_size_table(htable, &hcode);
	generate_code_table(htable, &hcode);
	order_codes(htable, &hcode);
#endif

	return RET_SUCCESS;
}

int parse_scan_header(FILE *stream, struct context *context)
{
	int err;
	/* Number of image components in scan */
	uint8_t Ns;

	err = read_byte(stream, &Ns);
	RETURN_IF(err);

	printf("Ns = %" PRIu8 "\n", Ns);

	for (int j = 0; j < Ns; ++j) {
		uint8_t Cs;
		uint8_t Td, Ta;

		err = read_byte(stream, &Cs);
		RETURN_IF(err);
		err = read_nibbles(stream, &Td, &Ta);
		RETURN_IF(err);

		printf("Cs%i = %" PRIu8 " Td%i = %" PRIu8 " Ta%i = %" PRIu8 "\n", j, Cs, j, Td, j, Ta);

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
	printf("Ss = %" PRIu8 " Se = %" PRIu8 "\n", Ss, Se);
// 	assert(Ah == 0);
// 	assert(Al == 0);
	printf("Ah = %" PRIu8 " Al = %" PRIu8 "\n", Ah, Al);

	return RET_SUCCESS;
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
