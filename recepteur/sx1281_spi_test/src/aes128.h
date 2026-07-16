/*
 * aes128.h — Self-contained AES-128 (ECB + CTR + CMAC)
 *
 * Portable C implementation, no external dependencies.
 * Used by ZLinky TIC Receiver on STM32 Nucleo-64.
 *
 * Functions:
 *   aes128_ecb_encrypt(plaintext, key, ciphertext)
 *   aes128_ctr_crypt(data, len, key, nonce)       — in-place, symmetric
 *   aes128_cmac(data, len, key, mac)               — 16-byte MAC output
 */

#ifndef AES128_H_
#define AES128_H_

#include <stdint.h>
#include <string.h>

/* ====== S-Box ====== */
static const uint8_t aes_sbox[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* ====== Round constant ====== */
static const uint8_t aes_rcon[11] = {
  0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

/* ====== GF(2^8) multiply by 2 ====== */
static inline uint8_t xtime(uint8_t x) {
  return (x << 1) ^ ((x & 0x80) ? 0x1b : 0x00);
}

/* ====== Key Expansion ====== */
static void aes128_key_expand(const uint8_t key[16], uint8_t roundKeys[176]) {
  int i;
  uint8_t temp[4];

  memcpy(roundKeys, key, 16);

  for (i = 4; i < 44; i++) {
    memcpy(temp, &roundKeys[(i - 1) * 4], 4);

    if (i % 4 == 0) {
      /* RotWord */
      uint8_t t = temp[0];
      temp[0] = temp[1];
      temp[1] = temp[2];
      temp[2] = temp[3];
      temp[3] = t;
      /* SubWord */
      temp[0] = aes_sbox[temp[0]];
      temp[1] = aes_sbox[temp[1]];
      temp[2] = aes_sbox[temp[2]];
      temp[3] = aes_sbox[temp[3]];
      /* Rcon */
      temp[0] ^= aes_rcon[i / 4];
    }

    roundKeys[i * 4 + 0] = roundKeys[(i - 4) * 4 + 0] ^ temp[0];
    roundKeys[i * 4 + 1] = roundKeys[(i - 4) * 4 + 1] ^ temp[1];
    roundKeys[i * 4 + 2] = roundKeys[(i - 4) * 4 + 2] ^ temp[2];
    roundKeys[i * 4 + 3] = roundKeys[(i - 4) * 4 + 3] ^ temp[3];
  }
}

/* ====== AddRoundKey ====== */
static void aes_add_round_key(uint8_t state[16], const uint8_t *rk) {
  for (int i = 0; i < 16; i++)
    state[i] ^= rk[i];
}

/* ====== SubBytes ====== */
static void aes_sub_bytes(uint8_t state[16]) {
  for (int i = 0; i < 16; i++)
    state[i] = aes_sbox[state[i]];
}

/* ====== ShiftRows ====== */
static void aes_shift_rows(uint8_t s[16]) {
  uint8_t t;
  /* Row 1: shift left 1 */
  t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
  /* Row 2: shift left 2 */
  t = s[2]; s[2] = s[10]; s[10] = t;
  t = s[6]; s[6] = s[14]; s[14] = t;
  /* Row 3: shift left 3 */
  t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
}

/* ====== MixColumns ====== */
static void aes_mix_columns(uint8_t s[16]) {
  for (int c = 0; c < 4; c++) {
    int i = c * 4;
    uint8_t a0 = s[i], a1 = s[i+1], a2 = s[i+2], a3 = s[i+3];
    uint8_t t = a0 ^ a1 ^ a2 ^ a3;
    s[i]   = a0 ^ xtime(a0 ^ a1) ^ t;
    s[i+1] = a1 ^ xtime(a1 ^ a2) ^ t;
    s[i+2] = a2 ^ xtime(a2 ^ a3) ^ t;
    s[i+3] = a3 ^ xtime(a3 ^ a0) ^ t;
  }
}

/* ====== AES-128-ECB Encrypt (single block) ====== */
static void aes128_ecb_encrypt(const uint8_t in[16], const uint8_t key[16], uint8_t out[16]) {
  uint8_t rk[176];
  uint8_t state[16];

  aes128_key_expand(key, rk);
  memcpy(state, in, 16);

  aes_add_round_key(state, &rk[0]);

  for (int round = 1; round < 10; round++) {
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_mix_columns(state);
    aes_add_round_key(state, &rk[round * 16]);
  }

  /* Final round (no MixColumns) */
  aes_sub_bytes(state);
  aes_shift_rows(state);
  aes_add_round_key(state, &rk[160]);

  memcpy(out, state, 16);
}

/* ====== AES-128-CTR Encrypt/Decrypt in-place ====== */
static void aes128_ctr_crypt(uint8_t *data, uint16_t len, const uint8_t key[16], const uint8_t nonce[16]) {
  uint8_t counter[16];
  uint8_t keystream[16];
  uint16_t offset = 0;

  memcpy(counter, nonce, 16);

  while (offset < len) {
    /* Encrypt counter block to get keystream */
    aes128_ecb_encrypt(counter, key, keystream);

    /* XOR keystream with data */
    uint16_t blockLen = len - offset;
    if (blockLen > 16) blockLen = 16;

    for (uint16_t i = 0; i < blockLen; i++)
      data[offset + i] ^= keystream[i];

    offset += blockLen;

    /* Increment counter (big-endian, last byte) */
    for (int i = 15; i >= 0; i--) {
      if (++counter[i] != 0) break;
    }
  }
}

/* ====== AES-128-CMAC ====== */

/* Left-shift a 16-byte block by 1 bit */
static void aes_cmac_lshift(const uint8_t in[16], uint8_t out[16]) {
  uint8_t carry = 0;
  for (int i = 15; i >= 0; i--) {
    out[i] = (in[i] << 1) | carry;
    carry = (in[i] >> 7) & 1;
  }
}

/* Generate CMAC subkeys K1, K2 */
static void aes_cmac_generate_subkeys(const uint8_t key[16], uint8_t K1[16], uint8_t K2[16]) {
  uint8_t zeros[16];
  uint8_t L[16];

  memset(zeros, 0, 16);
  aes128_ecb_encrypt(zeros, key, L);

  /* K1 = L << 1; if MSB(L)==1 then K1 ^= 0x87 */
  aes_cmac_lshift(L, K1);
  if (L[0] & 0x80)
    K1[15] ^= 0x87;

  /* K2 = K1 << 1; if MSB(K1)==1 then K2 ^= 0x87 */
  aes_cmac_lshift(K1, K2);
  if (K1[0] & 0x80)
    K2[15] ^= 0x87;
}

/**
 * Compute AES-128-CMAC (RFC 4493)
 * Output: 16-byte MAC
 */
static void aes128_cmac(const uint8_t *data, uint16_t len, const uint8_t key[16], uint8_t mac[16]) {
  uint8_t K1[16], K2[16];
  uint8_t X[16]; /* CBC state */
  uint8_t M_last[16];

  aes_cmac_generate_subkeys(key, K1, K2);

  int n = (len + 15) / 16; /* number of blocks */
  int flag; /* is last block complete? */

  if (n == 0) {
    n = 1;
    flag = 0; /* empty message → incomplete */
  } else {
    flag = (len % 16 == 0) ? 1 : 0;
  }

  /* Prepare last block */
  if (flag) {
    /* Complete: M_last = M_n XOR K1 */
    const uint8_t *lastBlock = &data[(n - 1) * 16];
    for (int i = 0; i < 16; i++)
      M_last[i] = lastBlock[i] ^ K1[i];
  } else {
    /* Incomplete: pad with 10...0, XOR K2 */
    int lastLen = len % 16;
    const uint8_t *lastBlock = (n > 0) ? &data[(n - 1) * 16] : data;
    memset(M_last, 0, 16);
    memcpy(M_last, lastBlock, lastLen);
    M_last[lastLen] = 0x80; /* padding */
    for (int i = 0; i < 16; i++)
      M_last[i] ^= K2[i];
  }

  /* CBC-MAC */
  memset(X, 0, 16);

  for (int i = 0; i < n - 1; i++) {
    /* X = AES(X XOR M_i) */
    for (int j = 0; j < 16; j++)
      X[j] ^= data[i * 16 + j];
    uint8_t Y[16];
    aes128_ecb_encrypt(X, key, Y);
    memcpy(X, Y, 16);
  }

  /* Last block */
  for (int j = 0; j < 16; j++)
    X[j] ^= M_last[j];
  aes128_ecb_encrypt(X, key, mac);
}

#endif /* AES128_H_ */
