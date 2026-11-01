// Stand-in for the bin2o-generated pools_bin.h: the test loads data/pools.bin with fread
// and points pools_bin at the buffer before poolsInit().
#ifndef CHECK_STUB_POOLS_BIN_H
#define CHECK_STUB_POOLS_BIN_H
extern const u8 *pools_bin;
#endif
