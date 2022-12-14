/*
 *  NS_fmt.c
 *  Written by Samuel Monux <smonux at gmail.com> in 2008, and placed
 *  in the public domain.  There's absolutely no warranty.
 *
 *  Netscreen OS password module. Passwords must be in this format
 *  <username>:<username>$<cryptedpass>
 *
 *  which appear in Netscreen config file
 *
 *  set admin name "<username>"
 *  set admin password "<cryptedpass>"
 *
 *  username is needed because is used as part of the salt.
 *
 *  Cryptedpass is generated this way (pseudocode):
 *
 *  b64 = array([A-Za-z0-9+/])
 *  md5_binary = MD5("<username>:Administration Tools:<password>")
 *
 *  md5_ascii = ""
 *  for every 16bits word "w" in md5_binary:
 *  	append(md5_ascii, b64[ w >> 12 & 0xf ])
 *  	append(md5_ascii, b64[ w >> 6  & 0x3f ])
 *  	append(md5_ascii, b64[ w       & 0x3f ])
 *
 *  ciphertext = md5_ascii
 *  for every c,p  ("nrcstn", [0, 6, 12, 17, 23, 29]):
 *  	interpolate  character "c" in position "p" in ciphertext
 *
 */

#if FMT_EXTERNS_H
extern struct fmt_main fmt_NS;
#elif FMT_REGISTERS_H
john_register_one(&fmt_NS);
#else

#include <string.h>

#include "arch.h"
#include "misc.h"
#include "md5.h"
#include "common.h"
#include "formats.h"
#include "memdbg.h"

#define FORMAT_LABEL			"md5ns"
#define FORMAT_NAME			"Netscreen"
#define ALGORITHM_NAME			"MD5 32/" ARCH_BITS_STR

#define BENCHMARK_COMMENT		""
#define BENCHMARK_LENGTH		-1

#define PLAINTEXT_LENGTH		25
#define CIPHERTEXT_LENGTH		50

#define BINARY_SIZE			16
#define SALT_SIZE			32
#define BINARY_ALIGN		sizeof(ARCH_WORD_32)
#define SALT_ALIGN			1

#define MIN_KEYS_PER_CRYPT		1
#define MAX_KEYS_PER_CRYPT		1


static struct fmt_tests tests[] = {
	{"admin$nMjFM0rdC9iOc+xIFsGEm3LtAeGZhn", "password"},
	{"a$nMf9FkrCIgHGccRAxsBAwxBtDtPHfn", "netscreen"},
	{"admin$nDa2MErEKCsMcuQOTsLNpGCtKJAq5n", "QUESTIONDEFENSE"},
	{NULL}
};

static unsigned short e64toshort[256];

#define ADM_LEN 22
static int salt_len, key_len;
static char cipher_salt[ SALT_SIZE + 1 ];
static char cipher_key[ PLAINTEXT_LENGTH + 1 ];
static const char *adm = ":Administration Tools:";
static char tocipher[ SALT_SIZE + ADM_LEN + PLAINTEXT_LENGTH ];
static ARCH_WORD_32 crypted[4];

static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void NS_init(struct fmt_main *self)
{
	int i;
	const char *pos;
	for (pos = b64, i = 0 ; *pos != 0 ; pos++, i++)
		e64toshort[(int)*pos] = i;
}

static int NS_valid(char *ciphertext, struct fmt_main *self)
{
	char *password;
	static char *netscreen = "nrcstn" ;
        static int  p[] = { 0, 6, 12, 17, 23, 29 };
	int i;

        password = ciphertext;

        while ((*password != '$') && (*password != '\0' ))
            password++;
        if (*password == '\0') return 0;

        if ((int)(password - ciphertext) > SALT_SIZE)
	        return 0;

        password++;

	if (strlen(password) != 30) return 0;
        if (strspn(password, b64) != 30) return 0;
	for (i = 0; i < 6 ; i++)
		if (netscreen[i] != password[p[i]]) return 0;

	for (i = 0; i < 30 ; i++) {
		char c = password[i];
		if (((c >= 'A') && ( c <= 'Z')) ||
		     ((c >= 'a') && ( c <= 'z')) ||
		     ((c >= '0') && ( c <= '9')) ||
		     (c == '+')  || ( c == '/'))
		continue;
		return 0;
	}
	return 1;
}

static ARCH_WORD_32 *get_binary(char *ciphertext)
{
	static union {
		unsigned long dummy;
		ARCH_WORD_32 i[BINARY_SIZE/sizeof(ARCH_WORD_32)];
	} _out;
	ARCH_WORD_32 *out = _out.i;
	char unscrambled[24];
	int i;
        MD5_u32plus a, b, c;
        MD5_u32plus d, e, f;
	char *pos;
#if ARCH_LITTLE_ENDIAN
        MD5_u32plus temp;
#endif

        pos = ciphertext;
	while (*pos++ != '$');

	memcpy(unscrambled, pos + 1, 6 );
	memcpy(unscrambled + 5, pos + 7, 6 );
	memcpy(unscrambled + 10, pos + 13, 5 );
	memcpy(unscrambled + 14, pos + 18, 6 );
	memcpy(unscrambled + 19, pos + 24, 5 );

	for ( i = 0 ; i < 4 ; i++ ) {
                a = e64toshort[ARCH_INDEX(unscrambled[6*i])];
                b = e64toshort[ARCH_INDEX(unscrambled[6*i + 1 ])];
                c = e64toshort[ARCH_INDEX(unscrambled[6*i + 2 ])];
                d = e64toshort[ARCH_INDEX(unscrambled[6*i + 3 ])];
                e = e64toshort[ARCH_INDEX(unscrambled[6*i + 4 ])];
                f = e64toshort[ARCH_INDEX(unscrambled[6*i + 5 ])];
#if ARCH_LITTLE_ENDIAN
                temp = (((a << 12) | (b << 6) | (c)) << 16) |
			    ((d << 12) | (e << 6) | (f));
		out[i] = ((temp << 24) & 0xff000000 ) |
		           ((temp << 8)  & 0x00ff0000 ) |
		           ((temp >> 8)  & 0x0000ff00 ) |
			   ((temp >> 24) & 0x000000ff );
#else
                out[i] = (((a << 12) | (b << 6) | (c)) << 16) |
			    ((d << 12) | (e << 6) | (f));
#endif
	}

	return out;
}

static int get_hash_0(int index)
{
	return crypted[0] & 0xf;
}

static int get_hash_1(int index)
{
	return crypted[0] & 0xff;
}

static int get_hash_2(int index)
{
	return crypted[0] & 0xfff;
}

static int get_hash_3(int index)
{
	return crypted[0] & 0xffff;
}

static int get_hash_4(int index)
{
	return crypted[0] & 0xfffff;
}

static int get_hash_5(int index)
{
	return crypted[0] & 0xffffff;
}

static int get_hash_6(int index)
{
	return crypted[0] & 0x7ffffff;
}

static char *get_salt(char *ciphertext)
{
	static char out[SALT_SIZE + 1];
	char *ipos, *opos;

	memset(&out, 0, sizeof(out));
	ipos = ciphertext;
	opos = out;
	while (*ipos != '$') *opos++ = *ipos++;
	*opos = '\0';

	return out;
}

static void set_salt (void *salt)
{
    salt_len = strlen((char *) salt);
    memcpy(cipher_salt, salt , salt_len);
}

static void  NS_set_key(char *key, int index)
{
    key_len = strlen((char *) key);
    if (key_len > PLAINTEXT_LENGTH)
	key_len = PLAINTEXT_LENGTH;
    memcpy(cipher_key, key, key_len);
}

static char *NS_get_key(int key)
{
    cipher_key[key_len] = 0;
    return cipher_key;
}

static int crypt_all(int *pcount, struct db_salt *salt)
{
	MD5_CTX ctx;

	MD5_Init(&ctx);
	memcpy(tocipher, cipher_salt, salt_len);
	memcpy(tocipher + salt_len, adm, ADM_LEN);
	memcpy(tocipher + salt_len + ADM_LEN, cipher_key, key_len);
	MD5_Update(&ctx , tocipher, salt_len + ADM_LEN + key_len);
	MD5_Final((void*)crypted, &ctx);

	return *pcount;
}

static int NS_cmp_all(void *binary, int index)
{
	return !memcmp(binary, crypted, BINARY_SIZE);
}

static int NS_cmp_exact(char *source, int index)
{
	return 1;
}

struct fmt_main fmt_NS = {
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
		FMT_CASE | FMT_8_BIT,
#if FMT_MAIN_VERSION > 11
		{ NULL },
#endif
		tests
	}, {
		NS_init,
		fmt_default_done,
		fmt_default_reset,
		fmt_default_prepare,
		NS_valid,
		fmt_default_split,
		(void *(*)(char *))get_binary,
		(void *(*)(char *))get_salt,
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
		NS_set_key,
		NS_get_key,
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
		NS_cmp_all,
		NS_cmp_all,
		NS_cmp_exact
	}
};

#endif /* plugin stanza */
