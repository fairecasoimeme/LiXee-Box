#pragma once
#include <stdint.h>
#include <stddef.h>

#define LINKY_ZB_CLUSTER  0xFF66u
#define LINKY_CLUSTER_ID  0xFFF1FC01u
#define LINKY_ATTR_ID(zb) (0xFFF10000u | (uint16_t)(zb))

typedef enum : uint8_t { LK_U8, LK_U16, LK_U32, LK_STR, LK_OCT } linky_kind_t;
typedef struct { uint16_t zb; linky_kind_t kind; uint8_t len; bool wr; } linky_attr_t;

static const linky_attr_t kLinkyAttrs[] = {
  {0x0000,LK_STR,16,0},{0x0001,LK_STR,4,0},{0x0002,LK_U8,0,0},{0x0003,LK_U8,0,0},{0x0004,LK_U8,0,0},
  {0x0005,LK_U16,0,0},{0x0006,LK_U16,0,0},{0x0007,LK_U16,0,0},{0x0008,LK_U16,0,0},
  {0x0009,LK_STR,6,0},{0x0010,LK_STR,16,0},
  {0x0100,LK_U8,0,1},{0x0300,LK_U8,0,0},                       // PeriodicSend (writable), Mode
  {0x0200,LK_STR,16,0},{0x0201,LK_U8,0,0},{0x0202,LK_STR,13,0},
  {0x0203,LK_U32,0,0},{0x0204,LK_U32,0,0},{0x0205,LK_U32,0,0},{0x0206,LK_U32,0,0},
  {0x0207,LK_U16,0,0},{0x0208,LK_U16,0,0},{0x0209,LK_U16,0,0},{0x0210,LK_U16,0,0},{0x0211,LK_U16,0,0},
  {0x0212,LK_U16,0,0},{0x0213,LK_U16,0,0},{0x0214,LK_U16,0,0},{0x0400,LK_U16,0,0},
  {0x0215,LK_STR,32,0},{0x0216,LK_STR,16,0},{0x0217,LK_OCT,8,0},
  {0x0218,LK_U8,0,0},{0x0219,LK_U8,0,0},{0x0220,LK_U8,0,0},{0x0221,LK_U8,0,0},
  {0x0222,LK_U8,0,0},{0x0223,LK_U8,0,0},{0x0224,LK_U16,0,0},{0x0225,LK_U8,0,0},
  {0x0226,LK_U8,0,0},{0x0227,LK_STR,127,0},{0x0228,LK_STR,127,0},
};
static const size_t kLinkyCount = sizeof(kLinkyAttrs) / sizeof(kLinkyAttrs[0]);

static inline const linky_attr_t *linky_find(uint16_t zb) {
  for (size_t i = 0; i < kLinkyCount; i++) if (kLinkyAttrs[i].zb == zb) return &kLinkyAttrs[i];
  return nullptr;
}