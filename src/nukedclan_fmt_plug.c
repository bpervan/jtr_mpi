/* Nuked-Klan CMS DB cracker patch for JtR. Hacked together during
 * July of 2012 by Dhiru Kholia <dhiru.kholia at gmail.com>.
 *
 * This software is Copyright (c) 2012, Dhiru Kholia <dhiru.kholia at gmail.com>,
 * and it is hereby released to the general public under the following terms:
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted.
 *
 * Input Format => user:$nk$*HASHKEY*hash
 *
 * Where,
 *
 * HASHKEY => hex(HASHKEY value found in conf.inc.php)
 *
 * Modified by JimF, Jul 2012.  About 6x speed improvements.
 */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_nk;
#elif FMT_REGISTERS_H
john_register_one(&fmt_nk);
#else

#include <string.h>

#include "arch.h"
#include "md5.h"
#include "sha.h"
#include "misc.h"
#include "common.h"
#include "formats.h"
#include "params.h"
#include "options.h"
#include "common.h"

#ifdef _OPENMP
#include <omp.h>
// Tuned on core i7 quad HT
//   1  5059K
//  16  8507k
//  64  8907k   ** this was chosen.
// 128  8914k
// 256  8810k
#define OMP_SCALE    64
#endif

#include "memdbg.h"

#define FORMAT_LABEL		"nk"
#define FORMAT_NAME		"Nuked-Klan CMS"
#define ALGORITHM_NAME		"SHA1 MD5 32/" ARCH_BITS_STR
#define BENCHMARK_COMMENT	""
#define BENCHMARK_LENGTH	-1 /* change to 0 once there's any speedup for "many salts" */
#define PLAINTEXT_LENGTH	32
#define BINARY_SIZE		16
#define SALT_SIZE		sizeof(struct custom_salt)
#define BINARY_ALIGN		sizeof(ARCH_WORD_32)
#define SALT_ALIGN			sizeof(int)
#define MIN_KEYS_PER_CRYPT	1
#define MAX_KEYS_PER_CRYPT	64

static struct fmt_tests nk_tests[] = {
	{"$nk$*379637b4fcde21b2c5fbc9a00af505e997443267*#17737d3661312121d5ae7d5c6156c0298", "openwall"},
	{"$nk$*379637b4fcde21b2c5fbc9a00af505e997443267*#5c20384512ee36590f5f0ab38a46c6ced", "password"},
	// from pass_gen.pl
	{"$nk$*503476424c5362476f36463630796a6e6c656165*#2f27c20e65b88b76c913115cdec3d9a18", "test1"},
	{"$nk$*7a317a71794339586c434d50506b6e4356626a67*#b62a615f605c2fd520edde76577d30f90", "thatsworking"},
	{"$nk$*796b7375666d7545695032413769443977644132*#4aec90bd9a930faaa42a0d7d40056132e", "test3"},
	{NULL}
};

static char (*saved_key)[PLAINTEXT_LENGTH + 1];
static ARCH_WORD_32 (*crypt_out)[BINARY_SIZE / sizeof(ARCH_WORD_32)];

static struct custom_salt {
	unsigned char HASHKEY[41];
	int decal;
} *cur_salt;

static inline void hex_encode(unsigned char *str, int len, unsigned char *out)
{
	int i;
	for (i = 0; i < len; ++i) {
		out[0] = itoa16[str[i]>>4];
		out[1] = itoa16[str[i]&0xF];
		out += 2;
	}
}

static void init(struct fmt_main *self)
{
#ifdef _OPENMP
	static int omp_t = 1;
	omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt *= omp_t;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt *= omp_t;
#endif
	saved_key = mem_calloc_tiny(sizeof(*saved_key) *
			self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	crypt_out = mem_calloc_tiny(sizeof(*crypt_out) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
}

static int ishex(char *q)
{
	while (atoi16[ARCH_INDEX(*q)] != 0x7F)
		q++;
	return !*q;
}

static int valid(char *ciphertext, struct fmt_main *self)
{
	char *ptr, *ctcopy, *keeptr;

	if (strncmp(ciphertext, "$nk$*", 5))
		return 0;
	if (!(ctcopy = strdup(ciphertext)))
		return 0;
	keeptr = ctcopy;
	ctcopy += 5;	/* skip leading "$nk$" */
	if (!(ptr = strtok(ctcopy, "*")))
		goto error;
	/* HASHKEY is of fixed length 40 */
	if (strlen(ptr) != 40)
		goto error;
	if(!ishex(ptr))
		goto error;
	if (!(ptr = strtok(NULL, "*")))
		goto error;
	/* skip two characters, for "nk_tests[]" this is '#'
	 * followed by decal value */
	if (strlen(ptr) <= 2)
		goto error;
	ptr += 2;
	/* hash is of fixed length 32 */
	if (strlen(ptr) != 32)
		goto error;
	if(!ishex(ptr))
		goto error;

	MEM_FREE(keeptr);
	return 1;

error:
	MEM_FREE(keeptr);
	return 0;
}

static void *get_salt(char *ciphertext)
{
	static struct custom_salt cs;
	char _ctcopy[256], *ctcopy=_ctcopy;
	char *p;
	int i;
	strnzcpy(ctcopy, ciphertext, 255);
	ctcopy += 5;	/* skip over "$nk$*" */
	p = strtok(ctcopy, "*");
	for (i = 0; i < 20; i++)
		cs.HASHKEY[i] = atoi16[ARCH_INDEX(p[i * 2])] * 16
			+ atoi16[ARCH_INDEX(p[i * 2 + 1])];
	p = strtok(NULL, "*");
	cs.decal = atoi16[ARCH_INDEX(p[1])];
	return (void *)&cs;
}

static void *get_binary(char *ciphertext)
{
	static union {
		unsigned char c[BINARY_SIZE+1];
		ARCH_WORD dummy;
	} buf;
	unsigned char *out = buf.c;
	char *p;
	int i;
	p = strrchr(ciphertext, '*') + 1 + 2;
	for (i = 0; i < BINARY_SIZE; i++) {
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

static void set_salt(void *salt)
{
	cur_salt = (struct custom_salt *)salt;
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
	int index = 0;

#ifdef _OPENMP
#pragma omp parallel for
#endif
	for (index = 0; index < count; index++) {
		unsigned char pass[40+1];
		unsigned char out[80];
		int i, k;
		int idx = 0;
		MD5_CTX c;
		SHA_CTX ctx;
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, saved_key[index], strlen(saved_key[index]));
		SHA1_Final(out, &ctx);
		hex_encode(out, 20, pass);
		for (i = 0, k=cur_salt->decal; i < 40; ++i, ++k) {
			out[idx++] = pass[i];
			if(k>19) k = 0;
			out[idx++] = cur_salt->HASHKEY[k];
		}
		MD5_Init(&c);
		MD5_Update(&c, out, 80);
		MD5_Final((unsigned char*)crypt_out[index], &c);
	}
	return count;
}

static int cmp_all(void *binary, int count)
{
	int index = 0;
	for (; index < count; index++)
		if (*((ARCH_WORD_32*)binary) == crypt_out[index][0])
			return 1;
	return 0;
}

static int cmp_one(void *binary, int index)
{
	return *((ARCH_WORD_32*)binary) == crypt_out[index][0];
}

static int cmp_exact(char *source, int index)
{
	void *binary = get_binary(source);
	return !memcmp(binary, crypt_out[index], BINARY_SIZE);
}

static void nk_set_key(char *key, int index)
{
	strcpy(saved_key[index], key);
}

static char *get_key(int index)
{
	return saved_key[index];
}

struct fmt_main fmt_nk = {
	{
		FORMAT_LABEL,
		FORMAT_NAME,
		ALGORITHM_NAME,
		BENCHMARK_COMMENT,
		BENCHMARK_LENGTH,
		PLAINTEXT_LENGTH,
		BINARY_SIZE,
		BINARY_ALIGN,
		SALT_SIZE,
		SALT_ALIGN,
		MIN_KEYS_PER_CRYPT,
		MAX_KEYS_PER_CRYPT,
		FMT_CASE | FMT_8_BIT | FMT_OMP,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		nk_tests
	}, {
		init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		valid,
		fmt_default_split,
		get_binary,
		get_salt,
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
		set_salt,
		nk_set_key,
		get_key,
		fmt_default_clear_keys,
		crypt_all,
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
		cmp_one,
		cmp_exact
	}
};

#endif /* plugin stanza */
