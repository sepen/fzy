#include <ctype.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "options.h"

#include "../config.h"

#define OPT_INFO 256
#define OPT_BORDER 259
#define OPT_COLOR 262
#define OPT_BORDER_LABEL 263
#define OPT_NO_COLOR 264

#define FZY_DEFAULT_BORDER_SGR "\033[38;5;240m"
#define FZY_DEFAULT_PROMPT_SGR "\033[38;5;153m"
#define FZY_DEFAULT_INFO_SGR "\033[38;5;144m"
#define FZY_DEFAULT_HEADER_SGR "\033[38;5;109m"
#define FZY_DEFAULT_QUERY_SGR "\033[1m"
#define FZY_DEFAULT_CURSORLINE_FG_SGR "\033[1m"
#define FZY_DEFAULT_CURSORLINE_BG_SGR "\033[48;5;236m"

static const char *usage_str =
    ""
    "Usage: fzy [OPTION]...\n"
    " -l, --lines=LINES        Result lines (default: fill terminal height)\n"
    " -H, --header=HEADER      String to print as item list header\n"
    " -p, --prompt=PROMPT      Input prompt (default '> ')\n"
    "     --info=STYLE           Finder info style\n"
    "                          [default|hidden|inline|inline-right]\n"
    " -q, --query=QUERY        Use QUERY as the initial search string\n"
    " -e, --show-matches=QUERY Output the sorted matches of QUERY\n"
    " -t, --tty=TTY            Specify file to use as TTY device (default /dev/tty)\n"
    " -s, --show-scores        Show the scores of each match\n"
    " -0, --read-null          Read input delimited by ASCII NUL characters\n"
    " -j, --workers NUM        Use NUM workers for searching. (default is # of CPUs)\n"
    " -i, --show-info          Show selection info line\n"
    "     --border             Draw a padded box (Unicode lines if UTF-8 locale)\n"
    "     --border-label=LABEL Label for the top border (with --border; truncated if too wide)\n"
    "     --color=SPEC         Colorize with fg:N,bg:N,fg+:N,bg+:N,... (comma-separated; fg+/bg+ =\n"
    "                          selected row; N=color-name, 0-255 palette index, or #rgb/#rrggbb)\n"
    "     --no-color           Disable ANSI colors (overrides default theme and --color)\n"
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
				   {"info", required_argument, NULL, OPT_INFO},
				   {"border", no_argument, NULL, OPT_BORDER},
				   {"border-label", required_argument, NULL, OPT_BORDER_LABEL},
				   {"color", required_argument, NULL, OPT_COLOR},
				   {"no-color", no_argument, NULL, OPT_NO_COLOR},
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

/* Lowercase, strip spaces into buf. Returns -1 if result is empty. */
static int color_token_normalize(const char *val, char *buf, size_t buflen) {
	size_t j = 0;
	for (size_t i = 0; val[i] && j + 1 < buflen; i++) {
		if (!isspace((unsigned char)val[i]))
			buf[j++] = (char)tolower((unsigned char)val[i]);
	}
	buf[j] = '\0';
	return buf[0] ? 0 : -1;
}

static int hex_digit_value(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* buf is normalized; accepts #rgb or #rrggbb. */
static int parse_hex_rgb(const char *buf, int *r, int *g, int *b) {
	if (buf[0] != '#')
		return -1;
	size_t n = strlen(buf);
	if (n == 4) {
		int a = hex_digit_value((unsigned char)buf[1]);
		int b_ = hex_digit_value((unsigned char)buf[2]);
		int c = hex_digit_value((unsigned char)buf[3]);
		if (a < 0 || b_ < 0 || c < 0)
			return -1;
		*r = a * 17;
		*g = b_ * 17;
		*b = c * 17;
		return 0;
	}
	if (n == 7) {
		for (size_t i = 1; i < 7; i++) {
			if (hex_digit_value((unsigned char)buf[i]) < 0)
				return -1;
		}
		unsigned long v = strtoul(buf + 1, NULL, 16);
		*r = (int)((v >> 16) & 0xff);
		*g = (int)((v >> 8) & 0xff);
		*b = (int)(v & 0xff);
		return 0;
	}
	return -1;
}

static int color_palette_index(const char *buf, int *idx) {
	char *end = NULL;
	long n = strtol(buf, &end, 10);
	if (end && *end == '\0' && n >= 0 && n <= 255) {
		*idx = (int)n;
		return 0;
	}
	static const struct {
		const char *name;
		int idx;
	} map[] = {
		{"black", 0}, {"gray", 1}, {"grey", 8}, {"darkgray", 240}, {"darkgrey", 240},
		{"red", 9}, {"darkred", 88}, {"green", 40}, {"darkgreen", 28},
		{"yellow", 226}, {"darkyellow", 220}, {"orange", 208}, {"darkorange", 202},
		{"blue", 21}, {"darkblue", 27}, {"magenta", 5}, {"purple", 129}, {"cyan", 51},
		{"white", 15}, {"darkwhite", 231},{"lightgray", 250}, {"lightgrey", 250},
		{"pink", 218}, {"darkpink", 219}};
	for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
		if (!strcmp(buf, map[i].name)) {
			*idx = map[i].idx;
			return 0;
		}
	}
	return -1;
}

/*
 * is_bg: 48 vs 38 CSI.
 * cursor_fg: bold + foreground (selected row text).
 * Returns 0 on success.
 */
static int color_value_to_sgr(const char *val, int is_bg, int cursor_fg, char *out, size_t outlen) {
	char buf[64];
	if (color_token_normalize(val, buf, sizeof buf) != 0)
		return -1;
	int ix;
	if (color_palette_index(buf, &ix) == 0) {
		int n;
		if (cursor_fg)
			n = snprintf(out, outlen, "\033[1m\033[38;5;%dm", ix);
		else if (is_bg)
			n = snprintf(out, outlen, "\033[48;5;%dm", ix);
		else
			n = snprintf(out, outlen, "\033[38;5;%dm", ix);
		return (n > 0 && (size_t)n < outlen) ? 0 : -1;
	}
	int r, g, b;
	if (parse_hex_rgb(buf, &r, &g, &b) != 0)
		return -1;
	int n;
	if (cursor_fg)
		n = snprintf(out, outlen, "\033[1m\033[38;2;%d;%d;%dm", r, g, b);
	else if (is_bg)
		n = snprintf(out, outlen, "\033[48;2;%d;%d;%dm", r, g, b);
	else
		n = snprintf(out, outlen, "\033[38;2;%d;%d;%dm", r, g, b);
	return (n > 0 && (size_t)n < outlen) ? 0 : -1;
}

static void options_apply_default_theme(options_t *options) {
	if (!options->use_color) {
		options->color_sgr_fg[0]            = '\0';
		options->color_sgr_bg[0]            = '\0';
		options->color_sgr_prompt[0]        = '\0';
		options->color_sgr_header[0]        = '\0';
		options->color_sgr_info[0]          = '\0';
		options->color_sgr_label[0]         = '\0';
		options->color_sgr_border[0]        = '\0';
		options->color_sgr_query[0]         = '\0';
		options->color_sgr_cursorline_fg[0] = '\0';
		options->color_sgr_cursorline_bg[0] = '\0';
		return;
	}
	snprintf(options->color_sgr_prompt, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_PROMPT_SGR);
	snprintf(options->color_sgr_info, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_INFO_SGR);
	snprintf(options->color_sgr_header, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_HEADER_SGR);
	snprintf(options->color_sgr_query, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_QUERY_SGR);
	snprintf(options->color_sgr_cursorline_fg, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_CURSORLINE_FG_SGR);
	snprintf(options->color_sgr_cursorline_bg, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_CURSORLINE_BG_SGR);
	options->color_sgr_fg[0]    = '\0';
	options->color_sgr_bg[0]    = '\0';
	options->color_sgr_label[0] = '\0';
	snprintf(options->color_sgr_border, FZY_COLOR_SGR_LEN, "%s", FZY_DEFAULT_BORDER_SGR);
}

static void options_apply_color_spec(options_t *options) {
	if (!options->use_color)
		return;
	options_apply_default_theme(options);
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
		char *sgr_dest = NULL;
		int is_bg = 0, cursor_fg = 0;
		if (!strcasecmp(key, "fg")) {
			sgr_dest = options->color_sgr_fg;
		} else if (!strcasecmp(key, "bg")) {
			sgr_dest = options->color_sgr_bg;
			is_bg    = 1;
		} else if (!strcasecmp(key, "border")) {
			sgr_dest = options->color_sgr_border;
		} else if (!strcasecmp(key, "prompt")) {
			sgr_dest = options->color_sgr_prompt;
		} else if (!strcasecmp(key, "header")) {
			sgr_dest = options->color_sgr_header;
		} else if (!strcasecmp(key, "info")) {
			sgr_dest = options->color_sgr_info;
		} else if (!strcasecmp(key, "fg+")) {
			sgr_dest   = options->color_sgr_cursorline_fg;
			cursor_fg  = 1;
		} else if (!strcasecmp(key, "bg+")) {
			sgr_dest = options->color_sgr_cursorline_bg;
			is_bg    = 1;
		} else if (!strcasecmp(key, "label") || !strcasecmp(key, "border-label")) {
			sgr_dest = options->color_sgr_label;
		} else {
			fprintf(stderr,
				"Unknown --color key: %s (use fg, bg, fg+, bg+, border, prompt, header, info, label)\n",
				key);
			free(spec);
			exit(EXIT_FAILURE);
		}
		if (color_value_to_sgr(val, is_bg, cursor_fg, sgr_dest, FZY_COLOR_SGR_LEN) != 0) {
			fprintf(stderr, "Unknown color in --color: %s\n", val);
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
	options->info_mode       = FZY_INFO_DEFAULT;
	options->border          = 0;
	options->border_label    = NULL;
	options->color_spec      = NULL;
	options->use_color       = 1;
	options_apply_default_theme(options);
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
				/* BSD getopt: "-e=foo" yields optarg "=foo"; GNU uses "foo". */
				if (optarg && optarg[0] == '=')
					options->filter = optarg + 1;
				else
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
			case OPT_INFO: {
				const char *m = optarg;
				if (!m || !*m) {
					fprintf(stderr,
						"--info requires an argument (default, hidden, inline, or inline-right)\n");
					usage(argv[0]);
					exit(EXIT_FAILURE);
				}
				if (!strcasecmp(m, "hidden"))
					options->info_mode = FZY_INFO_HIDDEN;
				else if (!strcasecmp(m, "default"))
					options->info_mode = FZY_INFO_DEFAULT;
				else if (!strcasecmp(m, "inline"))
					options->info_mode = FZY_INFO_INLINE;
				else if (!strcasecmp(m, "inline-right"))
					options->info_mode = FZY_INFO_INLINE_RIGHT;
				else {
					fprintf(stderr,
						"Invalid --info: %s (use default, hidden, inline, or inline-right)\n",
						m);
					usage(argv[0]);
					exit(EXIT_FAILURE);
				}
			} break;
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
				options->use_color  = 1;
				options->color_spec = optarg;
				options_apply_color_spec(options);
				break;
			case OPT_NO_COLOR:
				options->use_color = 0;
				options_apply_default_theme(options);
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
