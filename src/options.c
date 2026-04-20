#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "options.h"

#include "../config.h"

#define OPT_PROMPT_RESULTS 256
#define OPT_BORDER 259
#define OPT_COLOR 262
#define OPT_BORDER_LABEL 263

#define FZY_DEFAULT_BORDER_SGR "\033[38;5;240m"

static const char *usage_str =
    ""
    "Usage: fzy [OPTION]...\n"
    " -l, --lines=LINES        Result lines (default: fill terminal height)\n"
    " -H, --header=HEADER      String to print as item list header\n"
    " -p, --prompt=PROMPT      Input prompt (default '> ')\n"
    "     --prompt-results     Append \" < M/T\" to prompt (matches / total items)\n"
    " -q, --query=QUERY        Use QUERY as the initial search string\n"
    " -e, --show-matches=QUERY Output the sorted matches of QUERY\n"
    " -t, --tty=TTY            Specify file to use as TTY device (default /dev/tty)\n"
    " -s, --show-scores        Show the scores of each match\n"
    " -0, --read-null          Read input delimited by ASCII NUL characters\n"
    " -j, --workers NUM        Use NUM workers for searching. (default is # of CPUs)\n"
    " -i, --show-info          Show selection info line\n"
    "     --border             Draw a padded box (Unicode lines if UTF-8 locale)\n"
    "     --border-label=LABEL Label for the top border (with --border; truncated if too wide)\n"
    "     --color=SPEC         Colorize UI with fg:N,bg:N,border:N,... (name or 0-255)\n"
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
				   {"border-label", required_argument, NULL, OPT_BORDER_LABEL},
				   {"color", required_argument, NULL, OPT_COLOR},
				   {"help", no_argument, NULL, 'h'},
				   {NULL, 0, NULL, 0}};

static void trim_in_place(char *s) {
	char *a = s;
	while (*a && isspace((unsigned char)*a))
		a++;
	if (a != s)
		memmove(s, a, strlen(a) + 1);
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
}

static int color_value_to_index(const char *val, int *idx) {
	char buf[64];
	size_t j = 0;
	for (size_t i = 0; val[i] && j + 1 < sizeof buf; i++) {
		if (!isspace((unsigned char)val[i]))
			buf[j++] = (char)tolower((unsigned char)val[i]);
	}
	buf[j] = '\0';
	if (buf[0] == '\0')
		return -1;
	char *end = NULL;
	long n = strtol(buf, &end, 10);
	if (end && *end == '\0' && n >= 0 && n <= 255) {
		*idx = (int)n;
		return 0;
	}
	static const struct {
		const char *name;
		int idx;
	} map[] = {{"black", 0},	 {"red", 9},	{"green", 40}, {"yellow", 226},
		   {"blue", 21},	 {"magenta", 5}, {"purple", 129}, {"cyan", 51},
		   {"white", 15},	 {"gray", 240}, {"grey", 240},	{"orange", 208},
		   {"pink", 218}};
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
		if (!strcmp(buf, map[i].name)) {
			*idx = map[i].idx;
			return 0;
		}
	}
	return -1;
}

static void options_apply_color_spec(options_t *options) {
	options->color_sgr_fg[0] = '\0';
	options->color_sgr_bg[0] = '\0';
	snprintf(options->color_sgr_border, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_BORDER_SGR);
	char *spec = strdup(options->color_spec);
	if (!spec) {
		perror("strdup");
		exit(EXIT_FAILURE);
	}
	char *saveptr = NULL;
	char *tok = strtok_r(spec, ",", &saveptr);
	while (tok) {
		trim_in_place(tok);
		if (*tok == '\0') {
			fprintf(stderr, "Invalid --color: empty segment\n");
			free(spec);
			exit(EXIT_FAILURE);
		}
		char *colon = strchr(tok, ':');
		if (!colon || colon == tok) {
			fprintf(stderr, "Invalid --color segment (expected key:value): %s\n", tok);
			free(spec);
			exit(EXIT_FAILURE);
		}
		*colon = '\0';
		char *key = tok;
		char *val = colon + 1;
		trim_in_place(key);
		trim_in_place(val);
		int ix;
		if (color_value_to_index(val, &ix) != 0) {
			fprintf(stderr, "Unknown color in --color: %s\n", val);
			free(spec);
			exit(EXIT_FAILURE);
		}
		if (!strcasecmp(key, "fg")) {
			snprintf(options->color_sgr_fg, FZY_COLOR_SGR_LEN, "\033[38;5;%dm", ix);
		} else if (!strcasecmp(key, "bg")) {
			snprintf(options->color_sgr_bg, FZY_COLOR_SGR_LEN, "\033[48;5;%dm", ix);
		} else if (!strcasecmp(key, "border")) {
			snprintf(options->color_sgr_border, FZY_COLOR_SGR_LEN, "\033[38;5;%dm", ix);
		} else {
			fprintf(stderr, "Unknown --color key: %s (use fg, bg, border)\n", key);
			free(spec);
			exit(EXIT_FAILURE);
		}
		tok = strtok_r(NULL, ",", &saveptr);
	}
	free(spec);
}

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
	options->border_label    = NULL;
	options->color_spec      = NULL;
	options->color_sgr_fg[0] = '\0';
	options->color_sgr_bg[0] = '\0';
	snprintf(options->color_sgr_border, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_BORDER_SGR);
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
			case OPT_BORDER_LABEL:
				options->border_label = optarg;
				break;
			case OPT_COLOR:
				if (!optarg || !*optarg) {
					fprintf(stderr, "--color requires a non-empty argument\n");
					usage(argv[0]);
					exit(EXIT_FAILURE);
				}
				options->color_spec = optarg;
				options_apply_color_spec(options);
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
