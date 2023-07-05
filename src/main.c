#include <stdio.h>
#include "cgba/gba.h"

static void usage(const char *progname)
{
    fprintf(stderr, "Usage: %s <romfile>\n", progname);
}

int main(int argc, const char **argv)
{
    gba_system gba;

    puts("CGBA: A Game Boy Advance Emulator\n"
         "---------------------------------");

    if (argc != 2)
    {
        usage(argv[0]);
        return 1;
    }

    printf("ROM file: %s\n", argv[1]);
    init_system_or_die(&gba, argv[1]);
    report_rom_info(gba.mem->rom);
    run_system(&gba);
    deinit_system(&gba);

    return 0;
}
