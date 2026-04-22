#ifndef OPTIONS_H
#define OPTIONS_H OPTIONS_H

#define FZY_COLOR_SGR_LEN 48

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	FZY_INFO_HIDDEN = 0,
	FZY_INFO_DEFAULT = 1,
	FZY_INFO_INLINE = 2,
	FZY_INFO_INLINE_RIGHT = 3,
} fzy_info_mode_t;

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
	fzy_info_mode_t info_mode;
	int border;
	int use_color;
	const char *border_label;
	const char *color_spec;
	char color_sgr_fg[FZY_COLOR_SGR_LEN];
	char color_sgr_bg[FZY_COLOR_SGR_LEN];
	char color_sgr_border[FZY_COLOR_SGR_LEN];
	char color_sgr_prompt[FZY_COLOR_SGR_LEN];
	char color_sgr_header[FZY_COLOR_SGR_LEN];
	char color_sgr_info[FZY_COLOR_SGR_LEN];
	char color_sgr_label[FZY_COLOR_SGR_LEN];
} options_t;

void options_init(options_t *options);
void options_parse(options_t *options, int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif
