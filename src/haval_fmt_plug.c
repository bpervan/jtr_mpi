/* HAVAL cracker patch for JtR. Hacked together during April of 2013 by Dhiru
 * Kholia <dhiru at openwall.com>.
 *
 * This software is Copyright (c) 2013 Dhiru Kholia <dhiru at openwall.com> and
 * it is hereby released to the general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_haval_256_3;
extern struct fmt_main fmt_haval_128_4;
#elif FMT_REGISTERS_H
john_register_one(&fmt_haval_256_3);
john_register_one(&fmt_haval_128_4);
#else

#include <string.h>
#include "arch.h"
#include "sph_haval.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#ifdef _OPENMP
static int omp_t = 1;
#include <omp.h>
// Tuned on core i7 quad HT
//       256-3  128-4
//   1   227k   228k
//  64  6359k  5489k
// 128  7953k  6654k
// 256  8923k  7618k
// 512  9804k  8223k
// 1k  10307k  8569k  ** set to this value
// 2k  10081k  8427k
// 4k  10551k  8893k
#define OMP_SCALE  1024
#endif
#include "memdbg.h"

#define FORMAT_TAG		"$haval$"
#define TAG_LENGTH		7
#define ALGORITHM_NAME		"32/" ARCH_BITS_STR
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1
#define PLAINTEXT_LENGTH	125
#define BINARY_SIZE256		32
#define BINARY_SIZE128		16
#define SALT_SIZE		0
#define BINARY_ALIGN		4
#define SALT_ALIGN		1
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	1

static struct fmt_tests haval_256_3_tests[] = {
	{"91850C6487C9829E791FC5B58E98E372F3063256BB7D313A93F1F83B426AEDCC", "HAVAL"},
	{"$haval$91850C6487C9829E791FC5B58E98E372F3063256BB7D313A93F1F83B426AEDCC", "HAVAL"},
	{NULL}
};

static struct fmt_tests haval_128_4_tests[] = {
	{"EE6BBF4D6A46A679B3A856C88538BB98", ""},
	{"$haval$ee6bbf4d6a46a679b3a856c88538bb98", ""},
	{NULL}
};

static char (*saved_key)[PLAINTEXT_LENGTH + 1];
static ARCH_WORD_32 (*crypt_out)[BINARY_SIZE256 / sizeof(ARCH_WORD_32)];

static void init(struct fmt_main *self)
{
#ifdef _OPENMP
	omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt *= omp_t;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt *= omp_t;
#endif
	saved_key = mem_calloc_tiny(sizeof(*saved_key) *
			self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_out = mem_calloc_tiny(sizeof(*crypt_out) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

static int valid(char *ciphertext, struct fmt_main *self, int len)
{
	char *p;

	p = ciphertext;

	if (!strncmp(p, FORMAT_TAG, TAG_LENGTH))
		p += TAG_LENGTH;
	if (strlen(p) != len)
		return 0;

	while(*p)
		if(atoi16[ARCH_INDEX(*p++)]==0x7f)
			return 0;
	return 1;
}

/* we need independant valids, since the $haval$ signature is the same */
/* otherwise, if we have input with a mix of both types, then ALL of them */
/* will validate, even though  only the ones of the proper type will actually */
/* be tested.  If we had a singleton crypt function (which both 128-4 and */
/* 256-3 used, then a single valid would also work. But since each have */
/* their own crypt, and they are NOT compatible, then we need separate valids */
static int valid3(char *ciphertext, struct fmt_main *self)
{
	return valid(ciphertext, self, 64);
}
static int valid4(char *ciphertext, struct fmt_main *self)
{
	return valid(ciphertext, self, 32);
}

static void *get_binary_256(char *ciphertext)
{
	static union {
		unsigned char c[32];
		ARCH_WORD dummy;
	} buf;
	unsigned char *out = buf.c;
	char *p;
	int i;

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		p = strrchr(ciphertext, '$') + 1;
	else
		p = ciphertext;
	for (i = 0; i < 32; i++) {
		out[i] =
		    (atoi16[ARCH_INDEX(*p)] << 4) |
		    atoi16[ARCH_INDEX(p[1])];
		p += 2;
	}

	return out;
}

static void *get_binary_128(char *ciphertext)
{
	static union {
		unsigned char c[16];
		ARCH_WORD dummy;
	} buf;
	unsigned char *out = buf.c;
	char *p;
	int i;

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		p = strrchr(ciphertext, '$') + 1;
	else
		p = ciphertext;
	for (i = 0; i < 16; i++) {
		out[i] =
		    (atoi16[ARCH_INDEX(*p)] << 4) |
		    atoi16[ARCH_INDEX(p[1])];
		p += 2;
	}

	return out;
}

static int get_hash_0(int index) { return crypt_out[index][0] & 0xf; }
static int get_hash_1(int index) { return crypt_out[index][0] & 0xff; }
static int get_hash_2(int index) { return crypt_out[index][0] & 0xfff; }
static int get_hash_3(int index) { return crypt_out[index][0] & 0xffff; }
static int get_hash_4(int index) { return crypt_out[index][0] & 0xfffff; }
static int get_hash_5(int index) { return crypt_out[index][0] & 0xffffff; }
static int get_hash_6(int index) { return crypt_out[index][0] & 0x7ffffff; }

static int crypt_256_3(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
	for (index = 0; index < count; index++)
#endif
	{
		sph_haval256_3_context ctx;

		sph_haval256_3_init(&ctx);
		sph_haval256_3(&ctx, saved_key[index], strlen(saved_key[index]));
		sph_haval256_3_close(&ctx, (unsigned char*)crypt_out[index]);
	}
	return count;
}

static int crypt_128_4(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
	for (index = 0; index < count; index++)
#endif
	{
		sph_haval128_4_context ctx;

		sph_haval128_4_init(&ctx);
		sph_haval128_4(&ctx, saved_key[index], strlen(saved_key[index]));
		sph_haval128_4_close(&ctx, (unsigned char*)crypt_out[index]);
	}
	return count;
}

static int cmp_all(void *binary, int count)
{
	int index = 0;
#ifdef _OPENMP
	for (; index < count; index++)
#endif
		if (!memcmp(binary, crypt_out[index], ARCH_SIZE))
			return 1;
	return 0;
}

static int cmp_one256(void *binary, int index)
{
	return !memcmp(binary, crypt_out[index], BINARY_SIZE256);
}

static int cmp_one128(void *binary, int index)
{
	return !memcmp(binary, crypt_out[index], BINARY_SIZE128);
}

static int cmp_exact(char *source, int index)
{
	return 1;
}

static void haval_set_key(char *key, int index)
{
	int saved_key_length = strlen(key);
	if (saved_key_length > PLAINTEXT_LENGTH)
		saved_key_length = PLAINTEXT_LENGTH;
	memcpy(saved_key[index], key, saved_key_length);
	saved_key[index][saved_key_length] = 0;
}

static char *get_key(int index)
{
	return saved_key[index];
}

static char *split(char *ciphertext, int index, struct fmt_main *self)
{
	static char out[TAG_LENGTH + 2 * BINARY_SIZE256 + 1];

	if (!strncmp(ciphertext, FORMAT_TAG, TAG_LENGTH))
		ciphertext += TAG_LENGTH;

	strcpy(out, FORMAT_TAG);
	strcpy(&out[TAG_LENGTH], ciphertext);
	strlwr(&out[TAG_LENGTH]);

	return out;
}

struct fmt_main fmt_haval_256_3 = {
	{
		"HAVAL-256-3",
		"",
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE256,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP | FMT_SPLIT_UNIFIES_CASE,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		haval_256_3_tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		valid3,
		split,
		get_binary_256,
		fmt_default_salt,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		fmt_default_source,
		{
			fmt_default_binary_hash_0,
			fmt_default_binary_hash_1,
			fmt_default_binary_hash_2,
			fmt_default_binary_hash_3,
			fmt_default_binary_hash_4,
			fmt_default_binary_hash_5,
			fmt_default_binary_hash_6
		},
		fmt_default_salt_hash,
		fmt_default_set_salt,
		haval_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_256_3,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one256,
		cmp_exact
	}
};


struct fmt_main fmt_haval_128_4 = {
	{
		"HAVAL-128-4",
		"",
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE128,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP | FMT_SPLIT_UNIFIES_CASE,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		haval_128_4_tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		valid4,
		split,
		get_binary_128,
		fmt_default_salt,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		fmt_default_source,
		{
			fmt_default_binary_hash_0,
			fmt_default_binary_hash_1,
			fmt_default_binary_hash_2,
			fmt_default_binary_hash_3,
			fmt_default_binary_hash_4,
			fmt_default_binary_hash_5,
			fmt_default_binary_hash_6
		},
		fmt_default_salt_hash,
		fmt_default_set_salt,
		haval_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_128_4,
		{
			get_hash_0,
			get_hash_1,
			get_hash_2,
			get_hash_3,
			get_hash_4,
			get_hash_5,
			get_hash_6
		},
		cmp_all,
		cmp_one128,
		cmp_exact
	}
};

#endif /* plugin stanza */
