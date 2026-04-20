#ifndef OPTIONS_H
#define OPTIONS_H OPTIONS_H

#define FZY_COLOR_SGR_LEN 48

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int benchmark;
	const char *filter;
	const char *init_search;
	const char *tty_filename;
	int show_scores;
	unsigned int num_lines;
	int lines_user_set;
	unsigned int scrolloff;
	const char *prompt;
	const char *header;
	unsigned int workers;
	char input_delimiter;
	int show_info;
	int prompt_results;
	int border;
	const char *color_spec;
	char color_sgr_fg[FZY_COLOR_SGR_LEN];
	char color_sgr_bg[FZY_COLOR_SGR_LEN];
	char color_sgr_border[FZY_COLOR_SGR_LEN];
} options_t;

void options_init(options_t *options);
void options_parse(options_t *options, int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif
