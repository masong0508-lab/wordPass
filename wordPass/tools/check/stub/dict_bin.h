// Stand-in for the bin2o-generated dict_bin.h: the test loads data/dict.bin with fread
// and points dict_bin at the buffer before dictInit().
#ifndef CHECK_STUB_DICT_BIN_H
#define CHECK_STUB_DICT_BIN_H
extern const u8 *dict_bin;
#endif
