// Small, separate shelf-start checkpoints for the experimental RU/UK game.
// No dependence on retail Settings layout, native struct padding or ROM pointers.
#ifndef WORDPASS_SCRIPT_CHECKPOINT_H
#define WORDPASS_SCRIPT_CHECKPOINT_H
#include "dict_v4.h"
#include "tiles_v2.h"
#include "pools_v2.h"

#define SCRIPT_CHECKPOINT_BYTES 32
#define SCRIPT_CHECKPOINT_PATH 128
#define SCRIPT_CHECKPOINT_MAX_SCORE 999999999u
typedef struct {
    uint32_t language, pack, serial, shelf, seed, score;
} ScriptCheckpoint;

enum {
    SCRIPT_CHECKPOINT_NONE = 0, SCRIPT_CHECKPOINT_SAVED,
    SCRIPT_CHECKPOINT_LOADED, SCRIPT_CHECKPOINT_NO_CARD,
    SCRIPT_CHECKPOINT_NO_RECORD, SCRIPT_CHECKPOINT_DAILY,
    SCRIPT_CHECKPOINT_NOT_START, SCRIPT_CHECKPOINT_IO_ERROR,
    SCRIPT_CHECKPOINT_TRAINING
};

// Call only with validated views. Tag binds both the exact source packs and
// the shelf-generation revision, NOT a particular UI-only ROM build.
uint32_t scriptCheckpointTag(const V4Dict *dict, const V4Tiles *tiles,
                             const V4Pools *pools);
bool scriptCheckpointEncode(const ScriptCheckpoint *record,
                             uint8_t out[SCRIPT_CHECKPOINT_BYTES]);
bool scriptCheckpointDecode(const uint8_t *data, size_t length,
                             unsigned language, uint32_t pack,
                             ScriptCheckpoint *out);
// Derive a device-qualified, 8.3 filename in the retail settings directory.
// Both slots are disjoint from wordPass.cfg and from other packs/languages.
bool scriptCheckpointPath(const char *settingsPath, unsigned language,
                           uint32_t pack, unsigned slot,
                           char out[SCRIPT_CHECKPOINT_PATH]);
bool scriptCheckpointLoad(const char *settingsPath, unsigned language,
                           uint32_t pack, ScriptCheckpoint *out);
// Write only the inactive slot, verify its contents, preserve newest old
// valid slot. A failed/torn write cannot truncate the active checkpoint.
// The serial in the supplied record is ignored and assigned on store.
bool scriptCheckpointStore(const char *settingsPath,
                            const ScriptCheckpoint *record);
#endif
