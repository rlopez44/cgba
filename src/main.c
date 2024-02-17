#include <stdio.h>
#include <unistd.h>
#include "cgba/gba.h"

struct input_args {
    char *biosfile;
    char *romfile;
};

static void usage(const char *progname)
{
    fprintf(stderr,
            "Usage: %s [-b biosfile] <romfile>\n"
            "Options:\n"
            "-b    Specify a BIOS file to load into the emulator\n",
            progname);
}

static int parse_args(int argc, char **argv, struct input_args *args)
{
    args->biosfile = NULL;
    args->romfile = NULL;
    opterr = false;

    int opt;
    while ((opt = getopt(argc, argv, "b:")) != -1)
    {
        switch (opt)
        {
            case 'b':
                args->biosfile = optarg;
                printf("BIOS file supplied: %s\n", args->biosfile);
                break;

            case '?':
                if (optopt == 'b')
                    fprintf(stderr, "Option '%c' specified but no BIOS file was given\n", optopt);
                else
                    fprintf(stderr, "Unrecognized option: '%c'\n", optopt);
                // fallthrough

            default:
                return -1;
        }
    }

    if (optind != argc - 1)
        return -1;

    args->romfile = argv[optind];

    return 0;
}

int main(int argc, char **argv)
{
    gba_system gba;
    struct input_args args;

    puts("CGBA: A Game Boy Advance Emulator\n"
         "---------------------------------");

    if (parse_args(argc, argv, &args))
    {
        usage(argv[0]);
        return 1;
    }

    printf("ROM file: %s\n", args.romfile);
    init_system_or_die(&gba, args.romfile, args.biosfile);
    report_rom_info(gba.mem->rom);
    run_system(&gba);
    deinit_system(&gba);

    return 0;
}
