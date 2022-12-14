/*
 * this is a SAP PASSCODE (CODEVN G) plugin for john the ripper.
 * tested on linux/x86 only, rest is up to you.. at least, someone did the reversing :-)
 *
 * please note: this code is in a "works for me"-state, feel free to modify/speed up/clean/whatever it...
 *
 * (c) x7d8 sap loverz, public domain, btw
 * cheers: see test-cases.
 *
 * Heavily modified by magnum 2011-2012 for performance and for SIMD, OMP and
 * encodings support. Copyright (c) 2011, 2012 magnum, and it is hereby released
 * to the general public under the following terms:  Redistribution and use in
 * source and binary forms, with or without modification, are permitted.
 */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_sapG;
#elif FMT_REGISTERS_H
john_register_one(&fmt_sapG);
#else

#include <string.h>
#include <ctype.h>

#include "arch.h"

#ifdef MMX_COEF
#define NBKEYS	(MMX_COEF * SHA1_SSE_PARA)
#endif
#include "sse-intrinsics.h"

#include "misc.h"
#include "common.h"
#include "formats.h"
#include "sha.h"
#include "options.h"
#include "unicode.h"
#include "johnswap.h"

#define FORMAT_LABEL			"sapg"
#define FORMAT_NAME			"SAP CODVN F/G (PASSCODE)"

#define ALGORITHM_NAME			"SHA1 " SHA1_ALGORITHM_NAME

static unsigned int omp_t = 1;
#if defined(_OPENMP)
#include <omp.h>
#ifdef MMX_COEF
#define OMP_SCALE			128
#else
#define OMP_SCALE			2048
#endif
#endif

#include "memdbg.h"

#define BENCHMARK_COMMENT		""
#define BENCHMARK_LENGTH		0

#define SALT_FIELD_LENGTH		40
#define USER_NAME_LENGTH		12 /* max. length of user name in characters */
#define SALT_LENGTH			(USER_NAME_LENGTH*3)	/* 12 characters of UTF-8 */
#define PLAINTEXT_LENGTH		40 /* Characters of UTF-8 */
#define UTF8_PLAINTEXT_LENGTH		(PLAINTEXT_LENGTH*3) /* worst case */

#define BINARY_SIZE			20
#define BINARY_ALIGN			4
#define SALT_SIZE			sizeof(struct saltstruct)
#define SALT_ALIGN			4
#define CIPHERTEXT_LENGTH		(SALT_LENGTH + 1 + 2*BINARY_SIZE)	/* SALT + $ + 2x20 bytes for SHA1-representation */

#ifdef MMX_COEF
#define MIN_KEYS_PER_CRYPT		NBKEYS
#define MAX_KEYS_PER_CRYPT		NBKEYS
#define GETPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&60)*MMX_COEF + (3-((i)&3)) + (index>>(MMX_COEF>>1))*SHA_BUF_SIZ*MMX_COEF*4 ) //for endianity conversion
#define GETWORDPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&60)*MMX_COEF + (index>>(MMX_COEF>>1))*SHA_BUF_SIZ*MMX_COEF*4 )
#define GETSTARTPOS(index)		( (index&(MMX_COEF-1))*4 + (index>>(MMX_COEF>>1))*SHA_BUF_SIZ*MMX_COEF*4 )
#define GETOUTPOS(i, index)		( (index&(MMX_COEF-1))*4 + ((i)&(0xffffffff-3))*MMX_COEF + (3-((i)&3)) + (index>>(MMX_COEF>>1))*20*MMX_COEF ) //for endianity conversion

#else
#define MIN_KEYS_PER_CRYPT		1
#define MAX_KEYS_PER_CRYPT		1
#endif

//this array is from disp+work (sap's worker process)
#define MAGIC_ARRAY_SIZE 160
static const unsigned char theMagicArray[MAGIC_ARRAY_SIZE]=
{0x91, 0xAC, 0x51, 0x14, 0x9F, 0x67, 0x54, 0x43, 0x24, 0xE7, 0x3B, 0xE0, 0x28, 0x74, 0x7B, 0xC2,
 0x86, 0x33, 0x13, 0xEB, 0x5A, 0x4F, 0xCB, 0x5C, 0x08, 0x0A, 0x73, 0x37, 0x0E, 0x5D, 0x1C, 0x2F,
 0x33, 0x8F, 0xE6, 0xE5, 0xF8, 0x9B, 0xAE, 0xDD, 0x16, 0xF2, 0x4B, 0x8D, 0x2C, 0xE1, 0xD4, 0xDC,
 0xB0, 0xCB, 0xDF, 0x9D, 0xD4, 0x70, 0x6D, 0x17, 0xF9, 0x4D, 0x42, 0x3F, 0x9B, 0x1B, 0x11, 0x94,
 0x9F, 0x5B, 0xC1, 0x9B, 0x06, 0x05, 0x9D, 0x03, 0x9D, 0x5E, 0x13, 0x8A, 0x1E, 0x9A, 0x6A, 0xE8,
 0xD9, 0x7C, 0x14, 0x17, 0x58, 0xC7, 0x2A, 0xF6, 0xA1, 0x99, 0x63, 0x0A, 0xD7, 0xFD, 0x70, 0xC3,
 0xF6, 0x5E, 0x74, 0x13, 0x03, 0xC9, 0x0B, 0x04, 0x26, 0x98, 0xF7, 0x26, 0x8A, 0x92, 0x93, 0x25,
 0xB0, 0xA2, 0x0D, 0x23, 0xED, 0x63, 0x79, 0x6D, 0x13, 0x32, 0xFA, 0x3C, 0x35, 0x02, 0x9A, 0xA3,
 0xB3, 0xDD, 0x8E, 0x0A, 0x24, 0xBF, 0x51, 0xC3, 0x7C, 0xCD, 0x55, 0x9F, 0x37, 0xAF, 0x94, 0x4C,
 0x29, 0x08, 0x52, 0x82, 0xB2, 0x3B, 0x4E, 0x37, 0x9F, 0x17, 0x07, 0x91, 0x11, 0x3B, 0xFD, 0xCD };

// For backwards compatibility, we must support salts padded with spaces to a field width of 40
static struct fmt_tests tests[] = {
	{"DDIC$6066CD3147915331EC4C602847D27A75EB3E8F0A", "DDIC"},
	// invalid, because password is too short (would work during login, but not during password change),
	// magnum wants to keep thesse tests anyway, because they help verifying key buffer cleaning:
	{"F           $646A0AD270DF651065669A45D171EDD62DFE39A1", "X"},
	{"JOHNNY                                  $7D79B478E70CAAE63C41E0824EAB644B9070D10A", "CYBERPUNK"},
	{"VAN$D15597367F24090F0A501962788E9F19B3604E73", "hauser"},
	{"ROOT$1194E38F14B9F3F8DA1B181F14DEB70E7BDCC239", "KID"},
	// invalid, because password is too short (would work during login, but not during password change):
	{"MAN$22886450D0AB90FDA7F91C4F3DD5619175B372EA", "u"},
#if 0
	// This test case is invalid since the user name can just be
	// up to 12 characters long.
	// So, unless the user name doesn't contain non-ascii characters,
	// it will not be longer than 12 bytes.
	// Also, "-------" is not a valid SAP password, since the first 3 characters
	// are identical.
	{"------------------------------------$463BDDCF2D2D6E07FC64C075A0802BD87A39BBA6", "-------"},
#else
	// SAP user name consisting of 12 consecutive EURO characters:
	{"\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac"
	 "\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac"
	 "$B20D15C088481780CD44FCF2003AAAFBD9710C7C", "--+----"},
#endif
	{"SAP*                                $60A0F7E06D95BC9FB45F605BDF1F7B660E5D5D4E", "MaStEr"},
	{"DOLLAR$$$---$E0180FD4542D8B6715E7D0D9EDE7E2D2E40C3D4D", "Dollar$$$---"},
	{NULL}
};

static UTF8 (*saved_plain)[UTF8_PLAINTEXT_LENGTH + 1];
static int *keyLen;

#ifdef MMX_COEF

// max intermediate crypt size is 256 bytes
// multiple key buffers for lengths > 55
#define LIMB	5
static unsigned char *saved_key[LIMB];
static unsigned char *crypt_key;
static unsigned char *interm_crypt;
static unsigned int *clean_pos;

#else

static UTF8 (*saved_key)[UTF8_PLAINTEXT_LENGTH + 1];
static ARCH_WORD_32 (*crypt_key)[BINARY_SIZE / sizeof(ARCH_WORD_32)];

#endif

static struct saltstruct {
	unsigned int l;
	unsigned char s[SALT_LENGTH];
} *cur_salt;

static void init(struct fmt_main *self)
{
	static int warned = 0;
#ifdef MMX_COEF
	int i;
#endif
	// This is needed in order NOT to upper-case german double-s
	// in UTF-8 mode.
	initUnicode(UNICODE_MS_NEW);

	if (!options.listconf && pers_opts.target_enc != UTF_8 &&
	    !(options.flags & FLG_TEST_CHK) && warned++ == 0)
		fprintf(stderr, "Warning: SAP-F/G format should always be UTF-8.\nConvert your input files to UTF-8 and use --input-encoding=utf8\n");

	// Max 40 characters or 120 bytes of UTF-8, We actually do not truncate
	// multibyte input at 40 characters because it's too expensive.
	if (pers_opts.target_enc == UTF_8)
		self->params.plaintext_length = UTF8_PLAINTEXT_LENGTH;

#if defined (_OPENMP)
	omp_t = omp_get_max_threads();
	self->params.min_keys_per_crypt = omp_t * MIN_KEYS_PER_CRYPT;
	omp_t *= OMP_SCALE;
	self->params.max_keys_per_crypt = omp_t * MAX_KEYS_PER_CRYPT;
#endif

	saved_plain = mem_calloc_tiny(sizeof(*saved_plain) * self->params.max_keys_per_crypt, MEM_ALIGN_NONE);
	keyLen = mem_calloc_tiny(sizeof(*keyLen) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
#ifdef MMX_COEF
	clean_pos = mem_calloc_tiny(sizeof(*clean_pos) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	for(i = 0; i < LIMB; i++)
		saved_key[i] = mem_calloc_tiny(SHA_BUF_SIZ*4 * self->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	interm_crypt = mem_calloc_tiny(20 * self->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
	crypt_key = mem_calloc_tiny(20 * self->params.max_keys_per_crypt, MEM_ALIGN_SIMD);
#else
	crypt_key = mem_calloc_tiny(sizeof(*crypt_key) * self->params.max_keys_per_crypt, MEM_ALIGN_WORD);
	saved_key = saved_plain;
#endif
}

static int valid(char *ciphertext, struct fmt_main *self)
{
	int i, j;
	char *p;

	if (!ciphertext) return 0;
	p = strrchr(ciphertext, '$');
	if (!p) return 0;

	if (p - ciphertext > SALT_FIELD_LENGTH) return 0;
	if (strlen(&p[1]) != BINARY_SIZE * 2) return 0;

	j = 0;
	for (i = 0; i < p - ciphertext; i++) {
		// even those lower case non-ascii characters with a
		// corresponding upper case character could be rejected
		if (ciphertext[i] >= 'a' && ciphertext[i] <= 'z')
			return 0;
		else if (ciphertext[i] & 0x80)
			j++;

		// Reject if user name is longer than 12 characters.
		// This is not accurate, but close enough.
		// To be exact, I'd need to keep j unchanged for
		// the first byte of each character, instead of
		// incrementing j for every byte >= 0x80.
		if (i >= USER_NAME_LENGTH + j && ciphertext[i] != ' ')
			return 0;
	}
	// SAP user name cannot start with ! or ?
	if (ciphertext[0] == '!' || ciphertext[0] == '?') return 0;

	p++;

	// SAP and sap2john.pl always use upper case A-F for hashes,
	// so don't allow a-f
	for (i = 0; i < BINARY_SIZE * 2; i++)
		if (!(((p[i]>='0' && p[i]<='9')) ||
		      ((p[i]>='A' && p[i]<='F')) ))
			return 0;

	return 1;
}

static void set_salt(void *salt)
{
	cur_salt = salt;
}

static void *get_salt(char *ciphertext)
{
	char *p;
	static struct saltstruct out;

	p = strrchr(ciphertext, '$');
	out.l = (int)(p - ciphertext);

	memset(out.s, 0, sizeof(out.s));
	memcpy(out.s, ciphertext, out.l);

	return &out;
}

static void clear_keys(void)
{
	memset(keyLen, 0, sizeof(*keyLen) * omp_t * MAX_KEYS_PER_CRYPT);
}

static void set_key(char *key, int index)
{
	memcpy((char*)saved_plain[index], key, UTF8_PLAINTEXT_LENGTH);
	keyLen[index] = -1;
}

static char *get_key(int index) {
	return (char*)saved_plain[index];
}

static int cmp_all(void *binary, int count) {
#ifdef MMX_COEF
	unsigned int x,y=0;

#ifdef _OPENMP
	for(;y<SHA1_SSE_PARA*omp_t;y++)
#else
	for(;y<SHA1_SSE_PARA;y++)
#endif
	for(x=0;x<MMX_COEF;x++)
	{
		if( ((unsigned int*)binary)[0] == ((unsigned int*)crypt_key)[x+y*MMX_COEF*5] )
			return 1;
	}
	return 0;
#else
	unsigned int index;
	for (index = 0; index < count; index++)
		if (!memcmp(binary, crypt_key[index], BINARY_SIZE))
			return 1;
	return 0;
#endif
}

static int cmp_exact(char *source, int index){
	return 1;
}

static int cmp_one(void *binary, int index)
{
#ifdef MMX_COEF
	unsigned int x,y;
	x = index&(MMX_COEF-1);
	y = index>>(MMX_COEF>>1);

	if( (((unsigned int*)binary)[0] != ((unsigned int*)crypt_key)[x+y*MMX_COEF*5])   |
	    (((unsigned int*)binary)[1] != ((unsigned int*)crypt_key)[x+y*MMX_COEF*5+MMX_COEF]) |
	    (((unsigned int*)binary)[2] != ((unsigned int*)crypt_key)[x+y*MMX_COEF*5+2*MMX_COEF]) |
	    (((unsigned int*)binary)[3] != ((unsigned int*)crypt_key)[x+y*MMX_COEF*5+3*MMX_COEF])|
	    (((unsigned int*)binary)[4] != ((unsigned int*)crypt_key)[x+y*MMX_COEF*5+4*MMX_COEF]) )
		return 0;
	return 1;
#else
	return !memcmp(binary, crypt_key[index], BINARY_SIZE);
#endif
}

/*
 * calculate the length of data that has to be hashed from the magic array. pass the first hash result in here.
 * this is part of the walld0rf-magic
 * The return value will always be between 32 and 82, inclusive
 */
#if MMX_COEF
static inline unsigned int extractLengthOfMagicArray(unsigned const char *pbHashArray, unsigned int index)
#else
static inline unsigned int extractLengthOfMagicArray(unsigned const char *pbHashArray)
#endif
{
	unsigned int modSum = 0;

#if MMX_COEF
	unsigned const char *p = &pbHashArray[GETOUTPOS(3, index)];
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	p += 4*(MMX_COEF - 1);
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	modSum += *p++ % 6;
	p += 4*(MMX_COEF - 1) + 2;
	modSum += *p++ % 6;
	modSum += *p % 6;
#else
	unsigned int i;

	for (i=0; i<=9; i++)
		modSum += pbHashArray[i] % 6;
#endif
	return modSum + 0x20; //0x20 is hardcoded...
}

/*
 * Calculate the offset into the magic array. pass the first hash result in here
 * part of the walld0rf-magic
 * The return value will always be between 0 and 70, inclusive
 */
#if MMX_COEF
static inline unsigned int extractOffsetToMagicArray(unsigned const char *pbHashArray, unsigned int index)
#else
static inline unsigned int extractOffsetToMagicArray(unsigned const char *pbHashArray)
#endif
{
	unsigned int modSum = 0;

#if MMX_COEF
	unsigned const int *p = (unsigned int*)&pbHashArray[GETOUTPOS(11, index)];
	unsigned int temp;

	temp = *p & 0x0707;
	modSum += (temp >> 8) + (unsigned char)temp;
	p += MMX_COEF;
	temp = *p & 0x07070707;
	modSum += (temp >> 24) + (unsigned char)(temp >> 16) +
		(unsigned char)(temp >> 8) + (unsigned char)temp;
	p += MMX_COEF;
	temp = *p & 0x07070707;
	modSum += (temp >> 24) + (unsigned char)(temp >> 16) +
		(unsigned char)(temp >> 8) + (unsigned char)temp;
#else
	unsigned int i;

	for (i = 19; i >= 10; i--)
		modSum += pbHashArray[i] % 8;
#endif
	return modSum;
}

#if MMX_COEF
static inline void crypt_done(unsigned const int *source, unsigned int *dest, int index)
{
	unsigned int i;
	unsigned const int *s = &source[(index&(MMX_COEF-1)) + (index>>(MMX_COEF>>1))*5*MMX_COEF];
	unsigned int *d = &dest[(index&(MMX_COEF-1)) + (index>>(MMX_COEF>>1))*5*MMX_COEF];

	for (i = 0; i < 5; i++) {
		*d = *s;
		s += MMX_COEF;
		d += MMX_COEF;
	}
}
#endif

static int crypt_all(int *pcount, struct db_salt *salt)
{
	int count = *pcount;
#if MMX_COEF

#if defined(_OPENMP)
	int t;
#pragma omp parallel for
	for (t = 0; t < omp_t; t++)
#define ti (t*NBKEYS+index)
#else
#define t  0
#define ti index
#endif
	{
		unsigned int index, i, longest;
		int len;
		unsigned int crypt_len[NBKEYS];

		longest = 0;

		for (index = 0; index < NBKEYS; index++) {

			// Store key into vector key buffer
			if ((len = keyLen[ti]) < 0) {
				ARCH_WORD_32 *keybuf_word = (ARCH_WORD_32*)&saved_key[0][GETSTARTPOS(ti)];
				const ARCH_WORD_32 *wkey = (ARCH_WORD_32*)saved_plain[ti];
				ARCH_WORD_32 temp;

				len = 0;
				while(((unsigned char)(temp = *wkey++))) {
					if (!(temp & 0xff00))
					{
						*keybuf_word = JOHNSWAP(temp & 0xff);
						len++;
						break;
					}
					if (!(temp & 0xff0000))
					{
						*keybuf_word = JOHNSWAP(temp & 0xffff);
						len+=2;
						break;
					}
					*keybuf_word = JOHNSWAP(temp);
					if (!(temp & 0xff000000))
					{
						len+=3;
						break;
					}
					len += 4;
					if (len & 63)
						keybuf_word += MMX_COEF;
					else
						keybuf_word = (ARCH_WORD_32*)&saved_key[len>>6][GETSTARTPOS(ti)];
				}

				// Back-out of trailing spaces
				while(len && saved_plain[ti][len - 1] == ' ')
					saved_plain[ti][--len] = 0;
				keyLen[ti] = len;
			}

			// 1.	we need to SHA1 the password and username
			for (i = 0; i < cur_salt->l; i++)
				saved_key[(len+i)>>6][GETPOS((len + i), ti)] = cur_salt->s[i];
			len += i;

			saved_key[len>>6][GETPOS(len, ti)] = 0x80;

			// Clean rest of this buffer
			i = len;
			while (++i & 3)
				saved_key[i>>6][GETPOS(i, ti)] = 0;
			for (; i < (((len+8)>>6)+1)*64; i += 4)
				*(ARCH_WORD_32*)&saved_key[i>>6][GETWORDPOS(i, ti)] = 0;

			// This should do good but Valgrind insists it's a waste
			//if (clean_pos[ti] < i)
			//	clean_pos[ti] = len + 1;

			if (len > longest)
				longest = len;
			((unsigned int*)saved_key[(len+8)>>6])[15*MMX_COEF + (ti&3) + (ti>>2)*SHA_BUF_SIZ*MMX_COEF] = len << 3;
			crypt_len[index] = len;
		}

		SSESHA1body(&saved_key[0][t*SHA_BUF_SIZ*4*NBKEYS], (unsigned int*)&crypt_key[t*20*NBKEYS], NULL, SSEi_MIXED_IN);

		// Do another and possibly a third limb
		memcpy(&interm_crypt[t*20*NBKEYS], &crypt_key[t*20*NBKEYS], 20*NBKEYS);
		for (i = 1; i < (((longest + 8) >> 6) + 1); i++) {
			SSESHA1body(&saved_key[i][t*SHA_BUF_SIZ*4*NBKEYS], (unsigned int*)&interm_crypt[t*20*NBKEYS], (unsigned int*)&interm_crypt[t*20*NBKEYS], SSEi_MIXED_IN|SSEi_RELOAD);
			// Copy any output that is done now
			for (index = 0; index < NBKEYS; index++)
				if (((crypt_len[index] + 8) >> 6) == i)
					crypt_done((unsigned int*)interm_crypt, (unsigned int*)crypt_key, ti);
		}

		longest = 0;

		for (index = 0; index < NBKEYS; index++) {
			unsigned int offsetMagicArray;
			unsigned int lengthIntoMagicArray;
			const unsigned char *p;
			int i;

			// If final crypt ends up to be 56-61 bytes (or so), this must be clean
			for (i = 0; i < LIMB; i++)
				if (keyLen[ti] < i * 64 + 55)
					((unsigned int*)saved_key[i])[15*MMX_COEF + (ti&3) + (ti>>2)*SHA_BUF_SIZ*MMX_COEF] = 0;

			len = keyLen[ti];
			lengthIntoMagicArray = extractLengthOfMagicArray(crypt_key, ti);
			offsetMagicArray = extractOffsetToMagicArray(crypt_key, ti);

			// 2.	now, hash again --> sha1($password+$partOfMagicArray+$username) --> this is CODVNG passcode...
			i = len - 1;
			p = &theMagicArray[offsetMagicArray];
			// Copy a char at a time until aligned (at destination)...
			while (++i & 3)
				saved_key[i>>6][GETPOS(i, ti)] = *p++;
			// ...then a word at a time. This is a good boost, we are copying between 32 and 82 bytes here.
			for (;i < lengthIntoMagicArray + len; i += 4, p += 4)
				*(ARCH_WORD_32*)&saved_key[i>>6][GETWORDPOS(i, ti)] = JOHNSWAP(*(ARCH_WORD_32*)p);

			// Now, the salt. This is typically too short for the stunt above.
			for (i = 0; i < cur_salt->l; i++)
				saved_key[(len+lengthIntoMagicArray+i)>>6][GETPOS((len + lengthIntoMagicArray + i), ti)] = cur_salt->s[i];
			len += lengthIntoMagicArray + cur_salt->l;
			saved_key[len>>6][GETPOS(len, ti)] = 0x80;
			crypt_len[index] = len;

			// Clean the rest of this buffer as needed
			i = len;
			while (++i & 3)
				saved_key[i>>6][GETPOS(i, ti)] = 0;
			for (; i < clean_pos[ti]; i += 4)
				*(ARCH_WORD_32*)&saved_key[i>>6][GETWORDPOS(i, ti)] = 0;

			clean_pos[ti] = len + 1;
			if (len > longest)
				longest = len;

			((unsigned int*)saved_key[(len+8)>>6])[15*MMX_COEF + (ti&3) + (ti>>2)*SHA_BUF_SIZ*MMX_COEF] = len << 3;
		}

		SSESHA1body(&saved_key[0][t*SHA_BUF_SIZ*4*NBKEYS], (unsigned int*)&interm_crypt[t*20*NBKEYS], NULL, SSEi_MIXED_IN);

		// Typically, no or very few crypts are done at this point so this is faster than to memcpy the lot
		for (index = 0; index < NBKEYS; index++)
			if (crypt_len[index] < 56)
				crypt_done((unsigned int*)interm_crypt, (unsigned int*)crypt_key, ti);

		// Do another and possibly a third, fourth and fifth limb
		for (i = 1; i < (((longest + 8) >> 6) + 1); i++) {
			SSESHA1body(&saved_key[i][t*SHA_BUF_SIZ*4*NBKEYS], (unsigned int*)&interm_crypt[t*20*NBKEYS], (unsigned int*)&interm_crypt[t*20*NBKEYS], SSEi_MIXED_IN|SSEi_RELOAD);
			// Copy any output that is done now
			for (index = 0; index < NBKEYS; index++)
				if (((crypt_len[index] + 8) >> 6) == i)
					crypt_done((unsigned int*)interm_crypt, (unsigned int*)crypt_key, ti);
		}
	}
#undef t
#undef ti

#else

#ifdef _OPENMP
	int index;
#pragma omp parallel for
	for (index = 0; index < count; index++)
#else
#define index 0
#endif
	{
		unsigned int offsetMagicArray, lengthIntoMagicArray;
		unsigned char temp_key[BINARY_SIZE];
		unsigned char tempVar[UTF8_PLAINTEXT_LENGTH + MAGIC_ARRAY_SIZE + SALT_LENGTH]; //max size...
		SHA_CTX ctx;

		if (keyLen[index] < 0) {
			keyLen[index] = strlen((char*)saved_key[index]);

			// Back-out of trailing spaces
			while (saved_key[index][keyLen[index] - 1] == ' ') {
				saved_key[index][--keyLen[index]] = 0;
				if (keyLen[index] == 0) break;
			}
		}

		//1.	we need to SHA1 the password and username
		memcpy(tempVar, saved_key[index], keyLen[index]);  //first: the password
		memcpy(tempVar + keyLen[index], cur_salt->s, cur_salt->l); //second: the salt(username)

		SHA1_Init(&ctx);
		SHA1_Update(&ctx, tempVar, keyLen[index] + cur_salt->l);
		SHA1_Final((unsigned char*)temp_key, &ctx);

		lengthIntoMagicArray = extractLengthOfMagicArray(temp_key);
		offsetMagicArray = extractOffsetToMagicArray(temp_key);

		//2.     now, hash again --> sha1($password+$partOfMagicArray+$username) --> this is CODVNG passcode...
		memcpy(tempVar + keyLen[index], &theMagicArray[offsetMagicArray], lengthIntoMagicArray);
		memcpy(tempVar + keyLen[index] + lengthIntoMagicArray, cur_salt->s, cur_salt->l);

		SHA1_Init(&ctx);
		SHA1_Update(&ctx, tempVar, keyLen[index] + lengthIntoMagicArray + cur_salt->l);
		SHA1_Final((unsigned char*)crypt_key[index], &ctx);
	}
#undef index

#endif
	return count;
}

static void *binary(char *ciphertext)
{
	static int outbuf[BINARY_SIZE / sizeof(int)];
	char *realcipher = (char*)outbuf;
	int i;
	char* newCiphertextPointer;

	newCiphertextPointer = strrchr(ciphertext, '$') + 1;

	for(i=0;i<BINARY_SIZE;i++)
	{
		realcipher[i] = atoi16[ARCH_INDEX(newCiphertextPointer[i*2])]*16 + atoi16[ARCH_INDEX(newCiphertextPointer[i*2+1])];
	}
#ifdef MMX_COEF
	alter_endianity((unsigned char*)realcipher, BINARY_SIZE);
#endif
	return (void*)realcipher;
}

#if 0 // Not possible with current interface
static char *source(struct db_password *pw, char Buf[LINE_BUFFER_SIZE] )
{
	struct saltstruct *salt_s = (struct saltstruct*)(pw->source);
	unsigned char realcipher[BINARY_SIZE];
	unsigned char *cpi;
	char *cpo;
	int i;

	memcpy(realcipher, pw->binary, BINARY_SIZE);
#ifdef MMX_COEF
	alter_endianity(realcipher, BINARY_SIZE);
#endif
	memcpy(Buf, salt_s->s, salt_s->l);
	cpo = &Buf[salt_s->l];
	*cpo++ = '$';

	cpi = realcipher;

	for (i = 0; i < BINARY_SIZE; ++i) {
		*cpo++ = itoa16u[(*cpi)>>4];
		*cpo++ = itoa16u[*cpi&0xF];
		++cpi;
	}
	*cpo = 0;
	return Buf;
}
#endif

#ifdef MMX_COEF
#define KEY_OFF ((index/MMX_COEF)*MMX_COEF*5+(index&(MMX_COEF-1)))
static int get_hash_0(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xf; }
static int get_hash_1(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xff; }
static int get_hash_2(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xfff; }
static int get_hash_3(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xffff; }
static int get_hash_4(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xfffff; }
static int get_hash_5(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0xffffff; }
static int get_hash_6(int index) { return ((ARCH_WORD_32*)crypt_key)[KEY_OFF] & 0x7ffffff; }
#else
static int get_hash_0(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xf; }
static int get_hash_1(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xff; }
static int get_hash_2(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xfff; }
static int get_hash_3(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xffff; }
static int get_hash_4(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xfffff; }
static int get_hash_5(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0xffffff; }
static int get_hash_6(int index) { return *(ARCH_WORD_32*)crypt_key[index] & 0x7ffffff; }
#endif

// Here, we remove any salt padding and trim it to 36 bytes
static char *split(char *ciphertext, int index, struct fmt_main *self)
{
	static char out[CIPHERTEXT_LENGTH + 1];
	char *p;
	int i;

	p = strrchr(ciphertext, '$');

	i = (int)(p - ciphertext) - 1;
	while (ciphertext[i] == ' ' || i >= SALT_LENGTH)
		i--;
	i++;

	memset(out, 0, sizeof(out));
	memcpy(out, ciphertext, i);
	strnzcpy(&out[i], p, CIPHERTEXT_LENGTH + 1 - i);

	return out;
}

// Public domain hash function by DJ Bernstein
static int salt_hash(void *salt)
{
	struct saltstruct *s = (struct saltstruct*)salt;
	unsigned int hash = 5381;
	unsigned int i;

	for (i = 0; i < s->l; i++)
		hash = ((hash << 5) + hash) ^ s->s[i];

	return hash & (SALT_HASH_SIZE - 1);
}

static void done(void)
{
	initUnicode(UNICODE_UNICODE);
}

struct fmt_main fmt_sapG = {
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
#if !defined(MMX_COEF) || defined(SHA1_SSE_PARA)
		FMT_OMP |
#endif
		FMT_CASE | FMT_8_BIT | FMT_UTF8,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		tests
	}, {
		init,
		done,
		fmt_default_reset,
		fmt_default_prepare,
		valid,
		split,
		binary,
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
		salt_hash,
		set_salt,
		set_key,
		get_key,
		clear_keys,
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
