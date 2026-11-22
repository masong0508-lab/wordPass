// Focused host-only sweep using the existing competent/weak bot unchanged.
// balance_sweep.py builds baseline/current board and dictionary implementations
// around this identical player model, data and seed schedule.
#define main full_bot_main
#include "bot.c"
#undef main

int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    int language = langIndex(argv[1]), first = atoi(argv[2]), last = atoi(argv[3]);
    int attempts = atoi(argv[4]);
    u32 seedBase = (u32)strtoul(argv[5], NULL, 10);
    if (language < 0 || first < 1 || last > 39 || first > last || attempts < 1 || attempts > 1000)
        return 2;
    size_t size;
    dict_bin = loadFile("data/dict.bin", &size);
    pools_bin = loadFile("data/pools.bin", &size);
    variants_bin = loadFile("data/variants.bin", &size);
    dictInit(); poolsInit(); langInit();
    S.fire = FIRE_NORMAL; S.library = LIB_FULL; S.spelling = SPELL_BOTH;
    useLanguage(language);
    const Player *players[] = {&COMPETENT, &WEAK};
    puts("language,player,shelf,n,clears,fire,scorch,words,stall,turns,words_played,ignitions,unreachable_fire");
    for (unsigned player = 0; player < sizeof players / sizeof players[0]; player++)
        for (int shelf = first; shelf <= last; shelf++) {
            Stat stat = {0};
            for (int i = 0; i < attempts; i++) {
                // Both players start on the same board; before/after runs also
                // share the bot's independent perception/action RNG seed.
                u32 seed = seedBase + (u32)shelf * 100003u + (u32)i * 7919u;
                rseed(seed ^ 0xABCDEFu);
                setupClassic(shelf, seed);
                playShelf(players[player], &stat, false);
            }
            printf("%s,%s,%d,%d,%ld,%ld,%ld,%ld,%ld,%.3f,%.3f,%.3f,%ld\n",
                   argv[1], players[player]->name, shelf, attempts, stat.clears,
                   stat.fails[FAILX_FIRE], stat.fails[FAILX_SCORCH],
                   stat.fails[FAILX_WORDS], stat.fails[FAILX_TIMEOUT],
                   stat.turns / attempts, stat.words / attempts,
                   stat.ignitions / attempts, stat.fireFailUnreach);
            fflush(stdout);
        }
    return 0;
}
