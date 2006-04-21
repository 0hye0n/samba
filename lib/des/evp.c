#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <evp.h>

#include <krb5-types.h>

#include <aes.h>
#include <des.h>
#include <sha.h>
#include <rc2.h>
#include <rc4.h>
#include <md2.h>
#include <md4.h>
#include <md5.h>

struct hc_evp_md {
    int hash_size;
    int block_size;
    int ctx_size;
    int (*init)(EVP_MD_CTX *);
    int (*update)(EVP_MD_CTX *,const void *, size_t );
    int (*final)(void *, EVP_MD_CTX *);
    int (*cleanup)(EVP_MD_CTX *);
};

/*
 *
 */

size_t
EVP_MD_size(const EVP_MD *md)
{
    return md->hash_size;
}

size_t
EVP_MD_block_size(const EVP_MD *md)
{
    return md->block_size;
}

EVP_MD_CTX *
EVP_MD_CTX_create(void)
{
    return calloc(1, sizeof(EVP_MD_CTX));
}

void
EVP_MD_CTX_init(EVP_MD_CTX *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
}

void
EVP_MD_CTX_destroy(EVP_MD_CTX *ctx)
{
    EVP_MD_CTX_cleanup(ctx);
    free(ctx);
}

int
EVP_MD_CTX_cleanup(EVP_MD_CTX *ctx)
{
    if (ctx->md && ctx->md->cleanup)
	(ctx->md->cleanup)(ctx);
    ctx->md = NULL;
    ctx->engine = NULL;
    free(ctx->ptr);
    return 1;
}


const EVP_MD *
EVP_MD_CTX_md(EVP_MD_CTX *ctx)
{
    return ctx->md;
}

size_t
EVP_MD_CTX_size(EVP_MD_CTX *ctx)
{
    return EVP_MD_size(ctx->md);
}

size_t
EVP_MD_CTX_block_size(EVP_MD_CTX *ctx)
{
    return EVP_MD_block_size(ctx->md);
}

int
EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *md, ENGINE *engine)
{
    if (ctx->md != md || ctx->engine != engine) {
	EVP_MD_CTX_cleanup(ctx);
	ctx->md = md;
	ctx->engine = engine;

	ctx->ptr = calloc(1, md->ctx_size);
	if (ctx->ptr == NULL)
	    return 0;
    }
    (ctx->md->init)(ctx->ptr);
    return 1;
}

int
EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t size)
{
    (ctx->md->update)(ctx->ptr, data, size);
    return 1;
}

int
EVP_DigestFinal_ex(EVP_MD_CTX *ctx, void *hash, unsigned int *size)
{
    (ctx->md->final)(hash, ctx->ptr);
    if (size)
	*size = ctx->md->hash_size;
    return 1;
}

int
EVP_Digest(const void *data, size_t dsize, void *hash, unsigned int *hsize, 
	   const EVP_MD *md, ENGINE *engine)
{
    EVP_MD_CTX *ctx;
    int ret;

    ctx = EVP_MD_CTX_create();
    if (ctx == NULL)
	return 0;
    ret = EVP_DigestInit_ex(ctx, md, engine);
    if (ret != 1)
	return ret;
    ret = EVP_DigestUpdate(ctx, data, dsize);
    if (ret != 1)
	return ret;
    ret = EVP_DigestFinal_ex(ctx, hash, hsize);
    if (ret != 1)
	return ret;
    EVP_MD_CTX_destroy(ctx);
    return 1;
}

/*
 *
 */

static const struct hc_evp_md sha256 = {
    32,
    64,
    sizeof(SHA256_CTX),
    (void *)SHA256_Init,
    (void *)SHA256_Update,
    (void *)SHA256_Final,
    NULL
};

const EVP_MD *
EVP_sha256(void)
{
    return &sha256;
}

static const struct hc_evp_md sha1 = {
    20,
    64,
    sizeof(SHA_CTX),
    (void *)SHA1_Init,
    (void *)SHA1_Update,
    (void *)SHA1_Final,
    NULL
};

const EVP_MD *
EVP_sha1(void)
{
    return &sha1;
}

const EVP_MD *
EVP_sha(void)
{
    return &sha1;
}

const EVP_MD *
EVP_md5(void)
{
    static const struct hc_evp_md md5 = {
	16,
	64,
	sizeof(MD5_CTX),
	(void *)MD5_Init,
	(void *)MD5_Update,
	(void *)MD5_Final,
	NULL
    };
    return &md5;
}

const EVP_MD *
EVP_md4(void)
{
    static const struct hc_evp_md md4 = {
	16,
	64,
	sizeof(MD4_CTX),
	(void *)MD4_Init,
	(void *)MD4_Update,
	(void *)MD4_Final,
	NULL
    };
    return &md4;
}

const EVP_MD *
EVP_md2(void)
{
    static const struct hc_evp_md md2 = {
	16,
	16,
	sizeof(MD2_CTX),
	(void *)MD2_Init,
	(void *)MD2_Update,
	(void *)MD2_Final,
	NULL
    };
    return &md2;
}

/*
 *
 */

static void
null_Init (void *m)
{
}
static void
null_Update (void *m, const void * data, size_t size)
{
}
static void
null_Final(void *res, struct md5 *m)
{
}

const EVP_MD *
EVP_md_null(void)
{
    static const struct hc_evp_md null = {
	0,
	0,
	0,
	(void *)null_Init,
	(void *)null_Update,
	(void *)null_Final,
	NULL
    };
    return &null;
}

#if 0
void	EVP_MD_CTX_init(EVP_MD_CTX *ctx);
int	EVP_DigestInit(EVP_MD_CTX *ctx, const EVP_MD *type);
int	EVP_DigestFinal(EVP_MD_CTX *ctx,unsigned char *md,unsigned int *s);
int	EVP_SignFinal(EVP_MD_CTX *, void *, size_t *, EVP_PKEY *);
int	EVP_VerifyFinal(EVP_MD_CTX *, const void *, size_t, EVP_PKEY *);
#endif

/*
 *
 */

size_t
EVP_CIPHER_block_size(const EVP_CIPHER *c)
{
    return c->block_size;
}

size_t
EVP_CIPHER_key_length(const EVP_CIPHER *c)
{
    return c->key_len;
}

size_t
EVP_CIPHER_iv_length(const EVP_CIPHER *c)
{
    return c->iv_len;
}

void
EVP_CIPHER_CTX_init(EVP_CIPHER_CTX *c)
{
    memset(c, 0, sizeof(*c));
}

int
EVP_CIPHER_CTX_cleanup(EVP_CIPHER_CTX *c)
{
    if (c->cipher && c->cipher->cleanup)
	c->cipher->cleanup(c);
    if (c->cipher_data) {
	free(c->cipher_data);
	c->cipher_data = NULL;
    }
    return 1;
}

#if 0
int
EVP_CIPHER_CTX_set_key_length(EVP_CIPHER_CTX *c, int length)
{
    return 0;
}

int
EVP_CIPHER_CTX_set_padding(EVP_CIPHER_CTX *c, int pad)
{
    return 0;
}
#endif

const EVP_CIPHER *
EVP_CIPHER_CTX_cipher(EVP_CIPHER_CTX *ctx)
{
    return ctx->cipher;
}

size_t
EVP_CIPHER_CTX_block_size(const EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_block_size(ctx->cipher);
}

size_t
EVP_CIPHER_CTX_key_length(const EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_key_length(ctx->cipher);
}

size_t
EVP_CIPHER_CTX_iv_length(const EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_iv_length(ctx->cipher);
}

unsigned long
EVP_CIPHER_CTX_flags(const EVP_CIPHER_CTX *ctx)
{
    return ctx->cipher->flags;
}

int
EVP_CIPHER_CTX_mode(const EVP_CIPHER_CTX *ctx)
{
    return EVP_CIPHER_CTX_flags(ctx) & EVP_CIPH_MODE;
}

void *
EVP_CIPHER_CTX_get_app_data(EVP_CIPHER_CTX *ctx)
{
    return ctx->app_data;
}

void
EVP_CIPHER_CTX_set_app_data(EVP_CIPHER_CTX *ctx, void *data)
{
    ctx->app_data = data;
}

int
EVP_CipherInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *c, ENGINE *engine,
		  const void *key, const void *iv, int encp)
{
    if (encp == -1)
	encp = ctx->encrypt;
    else
	ctx->encrypt = (encp ? 1 : 0);

    if (c && (c != ctx->cipher)) {
	EVP_CIPHER_CTX_cleanup(ctx);
	ctx->cipher = c;
	ctx->key_len = c->key_len;

	ctx->cipher_data = malloc(c->ctx_size);
	if (ctx->cipher_data == NULL && c->ctx_size != 0)
	    return 0;

    } else if (ctx->cipher == NULL) {
	/* reuse of cipher, but not any cipher ever set! */
	return 0;
    }

    switch (EVP_CIPHER_CTX_flags(ctx)) {
    case EVP_CIPH_CBC_MODE:

	assert(EVP_CIPHER_CTX_iv_length(ctx) <= sizeof(ctx->iv));

	if (iv)
	    memcpy(ctx->oiv, iv, EVP_CIPHER_CTX_iv_length(ctx));
	memcpy(ctx->iv, ctx->oiv, EVP_CIPHER_CTX_iv_length(ctx));
	break;
    default:
	return 0;
    }

    if (key || (ctx->cipher->flags & EVP_CIPH_ALWAYS_CALL_INIT))
	ctx->cipher->init(ctx, key, iv, encp);

    return 1;
}

int
EVP_Cipher(EVP_CIPHER_CTX *ctx, void *out, const void *in,size_t size)
{
    return ctx->cipher->do_cipher(ctx, out, in, size);
}

/*
 *
 */

static int
enc_null_init(EVP_CIPHER_CTX *ctx,
		  const unsigned char * key,
		  const unsigned char * iv,
		  int encp)
{
    return 1;
}

static int
enc_null_do_cipher(EVP_CIPHER_CTX *ctx,
	      unsigned char *out,
	      const unsigned char *in,
	      unsigned int size)
{
    memmove(out, in, size);
    return 1;
}

static int
enc_null_cleanup(EVP_CIPHER_CTX *ctx)
{
    return 1;
}

const EVP_CIPHER *
EVP_enc_null(void)
{
    static const EVP_CIPHER enc_null = {
	0,
	0,
	0,
	0,
	EVP_CIPH_CBC_MODE,
	enc_null_init,
	enc_null_do_cipher,
	enc_null_cleanup,
	0,
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &enc_null;
}

/*
 *
 */

struct rc2_cbc {
    unsigned int maximum_effective_key;
    RC2_KEY key;
};

static int
rc2_init(EVP_CIPHER_CTX *ctx,
	 const unsigned char * key,
	 const unsigned char * iv,
	 int encp)
{
    struct rc2_cbc *k = ctx->cipher_data;
    k->maximum_effective_key = EVP_CIPHER_CTX_key_length(ctx) * 8;
    RC2_set_key(&k->key,
		EVP_CIPHER_CTX_key_length(ctx),
		key,
		k->maximum_effective_key);
    return 1;
}

static int
rc2_do_cipher(EVP_CIPHER_CTX *ctx,
	      unsigned char *out,
	      const unsigned char *in,
	      unsigned int size)
{
    struct rc2_cbc *k = ctx->cipher_data;
    RC2_cbc_encrypt(in, out, size, &k->key, ctx->iv, ctx->encrypt);
    return 1;
}

static int
rc2_cleanup(EVP_CIPHER_CTX *ctx)
{
    memset(ctx->cipher_data, 0, sizeof(struct rc2_cbc));
    return 1;
}


const EVP_CIPHER *
EVP_rc2_cbc(void)
{
    static const EVP_CIPHER rc2_cbc = {
	0,
	RC2_BLOCK_SIZE,
	RC2_KEY_LENGTH,
	RC2_BLOCK_SIZE,
	EVP_CIPH_CBC_MODE,
	rc2_init,
	rc2_do_cipher,
	rc2_cleanup,
	sizeof(struct rc2_cbc),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &rc2_cbc;
}

const EVP_CIPHER *
EVP_rc2_40_cbc(void)
{
    static const EVP_CIPHER rc2_40_cbc = {
	0,
	RC2_BLOCK_SIZE,
	5,
	RC2_BLOCK_SIZE,
	EVP_CIPH_CBC_MODE,
	rc2_init,
	rc2_do_cipher,
	rc2_cleanup,
	sizeof(struct rc2_cbc),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &rc2_40_cbc;
}

const EVP_CIPHER *
EVP_rc2_64_cbc(void)
{
    static const EVP_CIPHER rc2_64_cbc = {
	0,
	RC2_BLOCK_SIZE,
	8,
	RC2_BLOCK_SIZE,
	EVP_CIPH_CBC_MODE,
	rc2_init,
	rc2_do_cipher,
	rc2_cleanup,
	sizeof(struct rc2_cbc),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &rc2_64_cbc;
}

/*
 *
 */

const EVP_CIPHER *
EVP_rc4(void)
{
    printf("evp rc4\n");
    abort();
    return NULL;
}

const EVP_CIPHER *
EVP_rc4_40(void)
{
    printf("evp rc4_40\n");
    abort();
    return NULL;
}

/*
 *
 */

struct des_ede3_cbc {
    DES_key_schedule ks[3];
};

static int
des_ede3_cbc_init(EVP_CIPHER_CTX *ctx,
		  const unsigned char * key,
		  const unsigned char * iv,
		  int encp)
{
    struct des_ede3_cbc *k = ctx->cipher_data;

    DES_key_sched((DES_cblock *)(key), &k->ks[0]);
    DES_key_sched((DES_cblock *)(key + 8), &k->ks[1]);
    DES_key_sched((DES_cblock *)(key + 16), &k->ks[2]);

    return 1;
}

static int
des_ede3_cbc_do_cipher(EVP_CIPHER_CTX *ctx,
		       unsigned char *out,
		       const unsigned char *in,
		       unsigned int size)
{
    struct des_ede3_cbc *k = ctx->cipher_data;
    DES_ede3_cbc_encrypt(in, out, size,
			 &k->ks[0], &k->ks[1], &k->ks[2],
			 (DES_cblock *)ctx->iv, ctx->encrypt);
    return 1;
}

static int
des_ede3_cbc_cleanup(EVP_CIPHER_CTX *ctx)
{
    memset(ctx->cipher_data, 0, sizeof(struct des_ede3_cbc));
    return 1;
}

const EVP_CIPHER *
EVP_des_ede3_cbc(void)
{
    static const EVP_CIPHER des_ede3_cbc = {
	0,
	8,
	24,
	8,
	EVP_CIPH_CBC_MODE,
	des_ede3_cbc_init,
	des_ede3_cbc_do_cipher,
	des_ede3_cbc_cleanup,
	sizeof(struct des_ede3_cbc),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &des_ede3_cbc;
}

/*
 *
 */

static int
aes_init(EVP_CIPHER_CTX *ctx,
	 const unsigned char * key,
	 const unsigned char * iv,
	 int encp)
{
    AES_KEY *k = ctx->cipher_data;
    if (ctx->encrypt)
	AES_set_encrypt_key(key, ctx->cipher->key_len * 8, k);
    else
	AES_set_decrypt_key(key, ctx->cipher->key_len * 8, k);
    return 1;
}

static int
aes_do_cipher(EVP_CIPHER_CTX *ctx,
	      unsigned char *out,
	      const unsigned char *in,
	      unsigned int size)
{
    AES_KEY *k = ctx->cipher_data;
    AES_cbc_encrypt(in, out, size, k, ctx->iv, ctx->encrypt);
    return 1;
}

static int
aes_cleanup(EVP_CIPHER_CTX *ctx)
{
    memset(ctx->cipher_data, 0, sizeof(AES_KEY));
    return 1;
}

const EVP_CIPHER *
EVP_aes_128_cbc(void)
{
    static const EVP_CIPHER aes_128_cbc = {
	0,
	16,
	16,
	16,
	EVP_CIPH_CBC_MODE,
	aes_init,
	aes_do_cipher,
	aes_cleanup,
	sizeof(AES_KEY),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &aes_128_cbc;
}

const EVP_CIPHER *
EVP_aes_192_cbc(void)
{
    static const EVP_CIPHER aes_192_cbc = {
	0,
	16,
	24,
	16,
	EVP_CIPH_CBC_MODE,
	aes_init,
	aes_do_cipher,
	aes_cleanup,
	sizeof(AES_KEY),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &aes_192_cbc;
}


const EVP_CIPHER *
EVP_aes_256_cbc(void)
{
    static const EVP_CIPHER aes_256_cbc = {
	0,
	16,
	32,
	16,
	EVP_CIPH_CBC_MODE,
	aes_init,
	aes_do_cipher,
	aes_cleanup,
	sizeof(AES_KEY),
	NULL,
	NULL,
	NULL,
	NULL
    };
    return &aes_256_cbc;
}

/*
 *
 */

int
EVP_BytesToKey(const EVP_CIPHER *type,
	       const EVP_MD *md, 
	       const void *salt,
	       const void *data, size_t datalen,
	       int count,
	       const void *key,
	       const void *iv)
{
    return 0;
}

