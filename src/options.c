#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "options.h"

#include "../config.h"

#define OPT_PROMPT_RESULTS 256
#define OPT_BORDER 259

static const char *usage_str =
    ""
    "Usage: fzy [OPTION]...\n"
    " -l, --lines=LINES        Result lines (default: fill terminal height)\n"
    " -H, --header=STR         String to print as item list header\n"
    " -p, --prompt=PROMPT      Input prompt (default '> ')\n"
    "     --prompt-results     Append \" < M/T\" to prompt (matches / total items)\n"
    " -q, --query=QUERY        Use QUERY as the initial search string\n"
    " -e, --show-matches=QUERY Output the sorted matches of QUERY\n"
    " -t, --tty=TTY            Specify file to use as TTY device (default /dev/tty)\n"
    " -s, --show-scores        Show the scores of each match\n"
    " -0, --read-null          Read input delimited by ASCII NUL characters\n"
    " -j, --workers NUM        Use NUM workers for searching. (default is # of CPUs)\n"
    " -i, --show-info          Show selection info line\n"
    "     --border             Padded box (Unicode lines if UTF-8 locale)\n"
    " -h, --help     Display this help and exit\n"
    " -v, --version  Output version information and exit\n";

static void usage(const char *argv0) {
	fprintf(stderr, usage_str, argv0);
}

static struct option longopts[] = {{"show-matches", required_argument, NULL, 'e'},
				   {"query", required_argument, NULL, 'q'},
				   {"lines", required_argument, NULL, 'l'},
				   {"tty", required_argument, NULL, 't'},
				   {"header", required_argument, NULL, 'H'},
				   {"prompt", required_argument, NULL, 'p'},
				   {"show-scores", no_argument, NULL, 's'},
				   {"read-null", no_argument, NULL, '0'},
				   {"version", no_argument, NULL, 'v'},
				   {"benchmark", optional_argument, NULL, 'b'},
				   {"workers", required_argument, NULL, 'j'},
				   {"show-info", no_argument, NULL, 'i'},
				   {"prompt-results", no_argument, NULL, OPT_PROMPT_RESULTS},
				   {"border", no_argument, NULL, OPT_BORDER},
				   {"help", no_argument, NULL, 'h'},
				   {NULL, 0, NULL, 0}};

void options_init(options_t *options) {
	/* set defaults */
	options->benchmark       = 0;
	options->filter          = NULL;
	options->init_search     = NULL;
	options->show_scores     = 0;
	options->scrolloff       = 1;
	options->tty_filename    = DEFAULT_TTY;
	options->num_lines       = 0;
	options->lines_user_set  = 0;
	options->prompt          = DEFAULT_PROMPT;
	options->header          = NULL;
	options->workers         = DEFAULT_WORKERS;
	options->input_delimiter = '\n';
	options->show_info       = DEFAULT_SHOW_INFO;
	options->prompt_results  = 0;
	options->border          = 0;
}

void options_parse(options_t *options, int argc, char *argv[]) {
	options_init(options);

	int c;
	while ((c = getopt_long(argc, argv, "vhs0e:q:l:t:p:H:j:i", longopts, NULL)) != -1) {
		switch (c) {
			case 'v':
				printf("%s " VERSION " © 2014-2025 John Hawthorn\n", argv[0]);
				exit(EXIT_SUCCESS);
			case 's':
				options->show_scores = 1;
				break;
			case '0':
				options->input_delimiter = '\0';
				break;
			case 'q':
				options->init_search = optarg;
				break;
			case 'e':
				options->filter = optarg;
				break;
			case 'b':
				if (optarg) {
					if (sscanf(optarg, "%d", &options->benchmark) != 1) {
						usage(argv[0]);
						exit(EXIT_FAILURE);
					}
				} else {
					options->benchmark = 100;
				}
				break;
			case 't':
				options->tty_filename = optarg;
				break;
			case 'p':
				options->prompt = optarg;
				break;
			case 'H':
				options->header = optarg;
				break;
			case 'j':
				if (sscanf(optarg, "%u", &options->workers) != 1) {
					usage(argv[0]);
					exit(EXIT_FAILURE);
				}
				break;
			case 'l': {
				int l;
				if (!strcmp(optarg, "max")) {
					l = INT_MAX;
				} else if (sscanf(optarg, "%d", &l) != 1 || l < 3) {
					fprintf(stderr, "Invalid format for --lines: %s\n", optarg);
					fprintf(stderr, "Must be integer in range 3..\n");
					usage(argv[0]);
					exit(EXIT_FAILURE);
				}
				options->num_lines     = (unsigned int)l;
				options->lines_user_set = 1;
			} break;
			case 'i':
				options->show_info = 1;
				break;
			case OPT_PROMPT_RESULTS:
				options->prompt_results = 1;
				break;
			case OPT_BORDER:
				options->border = 1;
				break;
			case 'h':
			default:
				usage(argv[0]);
				exit(EXIT_SUCCESS);
		}
	}
	if (optind != argc) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
}
