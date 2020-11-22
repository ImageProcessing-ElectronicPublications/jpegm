#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <assert.h>

int skip_segment(FILE *stream, uint16_t len)
{
	if (fseek(stream, (long)len - 2, SEEK_CUR) != 0) {
		abort();
	}

	return 0;
}

uint8_t read_byte(FILE *stream)
{
	uint8_t byte;

	if (fread(&byte, sizeof(uint8_t), 1, stream) != 1) {
		abort();
	}

	return byte;
}

uint16_t read_word(FILE *stream)
{
	uint16_t word;

	if (fread(&word, sizeof(uint16_t), 1, stream) != 1) {
		abort();
	}

	word = ntohs(word);

	return word;
}

uint16_t read_length(FILE *stream)
{
	return read_word(stream);
}

/* B.1.1.2 Markers
 * All markers are assigned two-byte codes */
uint16_t read_marker(FILE *stream)
{
	/* Any marker may optionally be preceded by any
	 * number of fill bytes, which are bytes assigned code X’FF’. */

	uint8_t byte = read_byte(stream);

	if (byte != 0xff) {
		printf("unexpected byte value\n");
		abort();
	}

repeat:
	byte = read_byte(stream);

	switch (byte) {
		case 0xff:
			goto repeat;
		default:
			return UINT16_C(0xff00) | byte;
	}
}

struct qtable {
	/* Value 0 indicates 8-bit Qk values; value 1 indicates 16-bit Qk values. */
	uint8_t precision;
	/* in zig-zag scan order */
	uint16_t element[64];
};

int init_qtable(struct qtable *qtable)
{
	assert(qtable != NULL);

	qtable->precision = 0;

	for (int i = 0; i < 64; ++i) {
		qtable->element[i] = 0;
	}

	return 0;
}

struct component {
	/* Horizontal sampling factor, Vertical sampling factor */
	uint8_t H, V;
	/* Quantization table destination selector */
	uint8_t Tq;
};

int init_component(struct component *component)
{
	assert(component != NULL);

	component->H = 0;
	component->V = 0;

	component->Tq = 0;

	return 0;
}

struct context {
	/* Specifies one of four possible destinations at the decoder into
	 * which the quantization table shall be installed */
	struct qtable qtable[4];

	/*  Sample precision */
	uint8_t precision;

	/* Number of lines, Number of samples per line */
	uint16_t Y, X;

	/* Number of image components in frame */
	uint8_t components;

	struct component component[256];
};

int init_context(struct context *context)
{
	assert(context != NULL);

	for (int i = 0; i < 4; ++i) {
		init_qtable(&context->qtable[i]);
	}

	context->precision = 0;

	context->Y = 0;
	context->X = 0;

	context->components = 0;

	for (int i = 0; i < 256; ++i) {
		init_component(&context->component[i]);
	}

	return 0;
}

int read_nibbles(FILE *stream, uint8_t *first, uint8_t *second)
{
	uint8_t b;

	assert(first != NULL);
	assert(second != NULL);

	b = read_byte(stream);

	/* The first 4-bit parameter of the pair shall occupy the most significant 4 bits of the byte.  */
	*first = (b >> 4) & 15;
	*second = (b >> 0) & 15;

	return 0;
}

/* B.2.4.1 Quantization table-specification syntax */
int parse_qtable(FILE *stream, struct context *context)
{
	uint8_t Pq, Tq;
	struct qtable *qtable;

	assert(context != NULL);

	read_nibbles(stream, &Pq, &Tq);

	printf("Pq = %" PRIu8 " Tq = %" PRIu8 "\n", Pq, Tq);

	assert(Tq < 4);
	assert(Pq < 2);

	qtable = &context->qtable[Tq];

	qtable->precision = Pq;

	for (int i = 0; i < 64; ++i) {
		if (Pq == 0) {
			qtable->element[i] = (uint16_t)read_byte(stream);
		} else {
			qtable->element[i] = read_word(stream);
		}
	}

	return 0;
}

int parse_frame_header(FILE *stream, struct context *context)
{
	/* Sample precision */
	uint8_t P;
	/* Number of lines, Number of samples per line */
	uint16_t Y, X;
	/* Number of image components in frame */
	uint8_t Nf;

	assert(context != NULL);

	P = read_byte(stream);
	Y = read_word(stream);
	X = read_word(stream);
	Nf = read_byte(stream);

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

		C = read_byte(stream);
		read_nibbles(stream, &H, &V);
		Tq = read_byte(stream);

		printf("C = %" PRIu8 " H = %" PRIu8 " V = %" PRIu8 " Tq = %" PRIu8 "\n", C, H, V, Tq);

		context->component[C].H = H;
		context->component[C].V = V;
		context->component[C].Tq = Tq;
	}

	return 0;
}

int parse_huffman_tables(FILE *stream)
{
	/* TODO */
}

int parse(FILE *stream, struct context *context)
{
	while (1) {
		uint16_t marker = read_marker(stream);

		/* An asterisk (*) indicates a marker which stands alone,
		 * that is, which is not the start of a marker segment. */
		switch (marker) {
			uint16_t len;
			/* SOI* Start of image */
			case 0xffd8:
				printf("SOI\n");
				break;
			/* APP0 */
			case 0xffe0:
				printf("APP0\n");
				len = read_length(stream);
				skip_segment(stream, len);
				break;
			/* DQT Define quantization table(s) */
			case 0xffdb:
				printf("DQT\n");
				len = read_length(stream);
				parse_qtable(stream, context);
				break;
			/* SOF0 Baseline DCT */
			case 0xffc0:
				printf("SOF0\n");
				len = read_length(stream);
				parse_frame_header(stream, context);
				break;
			/* DHT Define Huffman table(s) */
			case 0xffc4:
				printf("DHT\n");
				len = read_length(stream);
				parse_huffman_tables(stream);
				break;
			default:
				printf("unhandled marker 0x%" PRIx16 "\n", marker);
				abort();
		}
	}

	return 1; /* not implemented */
}

int main(int argc, char *argv[])
{
	const char *path = "Car3.jpg";

	FILE *stream = fopen(path, "r");

	if (stream == NULL) {
		abort();
	}

	struct context *context = malloc(sizeof(struct context));

	if (context == NULL) {
		abort();
	}

	init_context(context);

	parse(stream, context);

	free(context);

	fclose(stream);

	return 0;
}
