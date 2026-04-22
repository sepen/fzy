#include <ctype.h>
#include <langinfo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "match.h"
#include "tty_interface.h"
#include "../config.h"

static int isprint_unicode(char c) {
	return isprint(c) || c & (1 << 7);
}

static int is_boundary(char c) {
	return ~c & (1 << 7) || c & (1 << 6);
}

/* Terminal columns taken by *s, skipping CSI "\033[ ... @" through "~" (colors, etc.). */
static size_t tty_str_vis_columns(const char *s) {
	size_t w = 0;
	for (size_t i = 0; s[i];) {
		if ((unsigned char)s[i] == '\x1b' && s[i + 1] == '[') {
			i += 2;
			while (s[i] != '\0' && (s[i] < '@' || s[i] > '~'))
				i++;
			if (s[i] != '\0')
				i++;
			continue;
		}
		if ((unsigned char)s[i] < 0x20u && s[i] != '\t') {
			i++;
			continue;
		}
		if (s[i] == '\t') {
			w += 8 - (w % 8);
			i++;
			continue;
		}
		if (((unsigned char)s[i] & 0xc0) == 0x80) {
			i++;
			continue;
		}
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xf0)
			i += 4;
		else if (c >= 0xe0)
			i += 3;
		else if (c >= 0xc0)
			i += 2;
		else
			i++;
		w++;
	}
	return w;
}

static void fputs_prompt_query(tty_t *tty, const options_t *opt, const char *prompt, const char *query) {
	if (opt->color_sgr_prompt[0]) {
		fputs(opt->color_sgr_prompt, tty->fout);
		tty_invalidate_fg(tty);
	}
	fputs(prompt, tty->fout);
	if (opt->color_sgr_prompt[0]) {
		if (opt->color_sgr_fg[0]) {
			fputs(opt->color_sgr_fg, tty->fout);
			tty_invalidate_fg(tty);
		} else {
			tty_setfg(tty, TTY_COLOR_NORMAL);
		}
	}
	fputs(query, tty->fout);
}

/* Byte length of longest prefix of s whose visible width (CSI skipped) is <= maxcol. */
static size_t tty_str_vis_prefix_bytes(const char *s, size_t maxcol) {
	size_t w = 0;
	size_t i = 0;
	while (s[i]) {
		if ((unsigned char)s[i] == '\x1b' && s[i + 1] == '[') {
			i += 2;
			while (s[i] != '\0' && (s[i] < '@' || s[i] > '~'))
				i++;
			if (s[i] != '\0')
				i++;
			continue;
		}
		if ((unsigned char)s[i] < 0x20u && s[i] != '\t') {
			i++;
			continue;
		}
		if (s[i] == '\t') {
			size_t tw = 8 - (w % 8);
			if (w + tw > maxcol)
				return i;
			w += tw;
			i++;
			continue;
		}
		if (((unsigned char)s[i] & 0xc0) == 0x80) {
			i++;
			continue;
		}
		if (w >= maxcol)
			return i;
		unsigned char c = (unsigned char)s[i];
		size_t adv = 1;
		if (c >= 0xf0)
			adv = 4;
		else if (c >= 0xe0)
			adv = 3;
		else if (c >= 0xc0)
			adv = 2;
		w++;
		i += adv;
	}
	return i;
}

static void print_prompt_info_suffix(tty_t *tty, const options_t *options, const choices_t *choices) {
	if (options->info_mode != FZY_INFO_INLINE)
		return;
	unsigned long total = choices->size ? (unsigned long)choices->size : 1UL;
	tty_printf(tty, " < %lu/%lu", (unsigned long)choices->available, total);
}

static const char *g_border_tl;
static const char *g_border_tr;
static const char *g_border_bl;
static const char *g_border_br;
static const char *g_border_h;
static const char *g_border_v;

static void border_update_glyphs(void) {
	const char *term = getenv("TERM");
	int unicode = term && strcmp(term, "dumb") != 0 && strcmp(term, "unknown") != 0;
	if (unicode) {
		const char *codeset = nl_langinfo(CODESET);
		if (!codeset || strcasecmp(codeset, "UTF-8") != 0)
			unicode = 0;
	}
	if (unicode) {
		g_border_tl = "\xe2\x94\x8c";
		g_border_tr = "\xe2\x94\x90";
		g_border_bl = "\xe2\x94\x94";
		g_border_br = "\xe2\x94\x98";
		g_border_h  = "\xe2\x94\x80";
		g_border_v  = "\xe2\x94\x82";
	} else {
		g_border_tl = g_border_tr = g_border_bl = g_border_br = "+";
		g_border_h = "-";
		g_border_v = "|";
	}
}

static int has_ui_border(const tty_t *tty, const options_t *opt) {
	return opt->border && tty_getwidth((tty_t *)tty) >= 5;
}

static void inner_colors(tty_t *tty, const options_t *opt) {
	tty_setnormal(tty);
	if (opt->color_sgr_bg[0])
		fputs(opt->color_sgr_bg, tty->fout);
	if (opt->color_sgr_fg[0])
		fputs(opt->color_sgr_fg, tty->fout);
}

/* One space between left border and inner column; inner_colors first so bg fills padding. */
static void border_inner_pad(tty_t *tty, const options_t *opt) {
	if (!has_ui_border(tty, opt))
		return;
	tty_setnowrap(tty);
	tty_setcol(tty, 1);
	inner_colors(tty, opt);
	fputc(' ', tty->fout);
	tty_setcol(tty, 2);
}

static void draw_border_horizontal(tty_t *tty, const options_t *opt, int bottom) {
	size_t w = tty_getwidth(tty);
	tty_setnormal(tty);
	tty_setcol(tty, 0);
	if (w == 0)
		return;
	if (w < 2)
		return;
	if (opt->color_sgr_bg[0])
		fputs(opt->color_sgr_bg, tty->fout);
	fputs(opt->color_sgr_border, tty->fout);
	fputs(bottom ? g_border_bl : g_border_tl, tty->fout);

	const char *lab =
	    (!bottom && opt->border_label && opt->border_label[0]) ? opt->border_label : NULL;
	size_t inner = w - 2;

	if (lab && inner > 0) {
		size_t n = tty_str_vis_prefix_bytes(lab, inner);
		char buf[1024];
		if (n >= sizeof buf)
			n = sizeof buf - 1;
		memcpy(buf, lab, n);
		buf[n] = '\0';
		size_t labvis = tty_str_vis_columns(buf);
		size_t left = (inner > labvis) ? (inner - labvis) / 2 : 0;
		size_t right = inner - left - labvis;
		for (size_t i = 0; i < left; i++)
			fputs(g_border_h, tty->fout);
		tty_setnormal(tty);
		if (opt->color_sgr_bg[0])
			fputs(opt->color_sgr_bg, tty->fout);
		if (opt->color_sgr_label[0]) {
			fputs(opt->color_sgr_label, tty->fout);
			tty_invalidate_fg(tty);
		} else if (opt->color_sgr_fg[0]) {
			fputs(opt->color_sgr_fg, tty->fout);
			tty_invalidate_fg(tty);
		} else {
			tty_setfg(tty, TTY_COLOR_NORMAL);
		}
		(void)fwrite(buf, 1, n, tty->fout);
		fputs(opt->color_sgr_border, tty->fout);
		if (opt->color_sgr_bg[0])
			fputs(opt->color_sgr_bg, tty->fout);
		for (size_t i = 0; i < right; i++)
			fputs(g_border_h, tty->fout);
	} else {
		for (size_t i = 0; i + 2 < w; i++)
			fputs(g_border_h, tty->fout);
	}
	fputs(bottom ? g_border_br : g_border_tr, tty->fout);
	tty_setnormal(tty);
}

static void border_left(tty_t *tty, const options_t *opt) {
	if (!has_ui_border(tty, opt))
		return;
	tty_setcol(tty, 0);
	tty_setnormal(tty);
	if (opt->color_sgr_bg[0])
		fputs(opt->color_sgr_bg, tty->fout);
	fputs(opt->color_sgr_border, tty->fout);
	fputs(g_border_v, tty->fout);
}

static void border_right(tty_t *tty, const options_t *opt) {
	if (!has_ui_border(tty, opt))
		return;
	size_t w = tty_getwidth(tty);
	if (w < 5)
		return;
	tty_setcol(tty, (int)w - 2);
	tty_setnormal(tty);
	if (opt->color_sgr_bg[0])
		fputs(opt->color_sgr_bg, tty->fout);
	if (opt->color_sgr_fg[0])
		fputs(opt->color_sgr_fg, tty->fout);
	fputc(' ', tty->fout);
	tty_setcol(tty, (int)w - 1);
	if (opt->color_sgr_bg[0])
		fputs(opt->color_sgr_bg, tty->fout);
	fputs(opt->color_sgr_border, tty->fout);
	fputs(g_border_v, tty->fout);
	tty_setnormal(tty);
}

static void clear(tty_interface_t *state) {
	tty_t *tty = state->tty;
	options_t *options = state->options;

	tty_getwinsz(tty);
	tty_setcol(tty, 0);
	if (has_ui_border(tty, options)) {
		tty_moveup(tty, 1);
		tty_setcol(tty, 0);
	}
	size_t line = 0;
	unsigned int nclear = options->num_lines + (options->show_info ? 1 : 0) +
			      (options->info_mode == FZY_INFO_DEFAULT ? 1 : 0);
	if (options->header)
		nclear++;
	if (has_ui_border(tty, options))
		nclear += 2;
	while (line++ < nclear) {
		tty_newline(tty);
	}
	tty_clearline(tty);
	if (options->num_lines > 0) {
		tty_moveup(tty, line - 1);
	}
	tty_flush(tty);
}

static void draw_match(tty_interface_t *state, const char *choice, int selected, int max_vis_cols) {
	tty_t *tty = state->tty;
	options_t *options = state->options;
	char *search = state->last_search;

	int n = strlen(search);
	size_t positions[MATCH_MAX_LEN];
	for (int i = 0; i < n + 1 && i < MATCH_MAX_LEN; i++)
		positions[i] = -1;

	score_t score = match_positions(search, choice, &positions[0]);

	int col = 0;
	if (options->show_scores) {
		if (score == SCORE_MIN) {
			tty_printf(tty, "(     ) ");
		} else {
			tty_printf(tty, "(%5.2f) ", score);
		}
		col = 8;
	}

	if (selected)
#ifdef TTY_SELECTION_UNDERLINE
		tty_setunderline(tty);
#else
		tty_setinvert(tty);
#endif

	tty_setnowrap(tty);
	for (size_t i = 0, p = 0; choice[i] != '\0'; i++) {
		if (max_vis_cols < INT_MAX && is_boundary(choice[i]) && col >= max_vis_cols)
			break;
		if (positions[p] == i) {
			tty_setfg(tty, TTY_COLOR_HIGHLIGHT);
			p++;
		} else {
			if (options->color_sgr_fg[0]) {
				fputs(options->color_sgr_fg, tty->fout);
				tty_invalidate_fg(tty);
			} else {
				tty_setfg(tty, TTY_COLOR_NORMAL);
			}
		}
		if (choice[i] == '\n') {
			tty_putc(tty, ' ');
		} else {
			tty_printf(tty, "%c", choice[i]);
		}
		if (is_boundary(choice[i]))
			col++;
	}
	tty_setwrap(tty);
	tty_setnormal(tty);
	if (options->color_sgr_bg[0])
		fputs(options->color_sgr_bg, tty->fout);
	if (options->color_sgr_fg[0])
		fputs(options->color_sgr_fg, tty->fout);
}

static void draw_match_stats_line(tty_t *tty, const options_t *options, const choices_t *choices,
				  int bordered, int bracketed) {
	tty_printf(tty, "\n");
	tty_setnormal(tty);
	border_left(tty, options);
	if (bordered)
		border_inner_pad(tty, options);
	else
		inner_colors(tty, options);
	unsigned long av = (unsigned long)choices->available;
	unsigned long sz = (unsigned long)choices->size;
	if (bracketed)
		tty_printf(tty, "[%lu/%lu]", av, sz);
	else
		tty_printf(tty, "%lu/%lu", av, sz);
	tty_clearline(tty);
	border_right(tty, options);
	if (bordered)
		tty_setwrap(tty);
}

static void print_prompt_info_inline_right(tty_t *tty, const options_t *options,
					   const choices_t *choices, const char *query, int bordered) {
	unsigned long total = choices->size ? (unsigned long)choices->size : 1UL;
	char buf[40];
	int n = snprintf(buf, sizeof buf, "%lu/%lu", (unsigned long)choices->available, total);
	if (n <= 0 || n >= (int)sizeof buf)
		return;
	size_t W = tty_getwidth(tty);
	int inner_lo = 0;
	int inner_cols = (int)W;
	if (bordered && (int)W >= 5) {
		inner_lo = 2;
		inner_cols = (int)W - 4;
	}
	size_t vis_used = tty_str_vis_columns(options->prompt) + tty_str_vis_columns(query);
	int right = inner_lo + inner_cols - n;
	int col = right;
	if (inner_lo + (int)vis_used > col)
		col = inner_lo + (int)vis_used;
	if (col + n > inner_lo + inner_cols)
		col = inner_lo + inner_cols - n;
	if (col < inner_lo)
		col = inner_lo;
	inner_colors(tty, options);
	tty_setnowrap(tty);
	tty_setcol(tty, col);
	tty_printf(tty, "%s", buf);
}

static void draw(tty_interface_t *state) {
	tty_t *tty = state->tty;
	choices_t *choices = state->choices;
	options_t *options = state->options;

	tty_getwinsz(tty);

	if (options->border)
		border_update_glyphs();

	if (!options->lines_user_set) {
		unsigned int tty_h = (unsigned int)tty_getheight(tty);
		unsigned int adj  = 1;
		if (options->show_info)
			adj++;
		if (options->info_mode == FZY_INFO_DEFAULT)
			adj++;
		if (options->header)
			adj++;
		if (options->border && tty_getwidth(tty) >= 5)
			adj += 2;
		if (tty_h > adj)
			options->num_lines = tty_h - adj;
		else
			options->num_lines = 1;
		if (options->num_lines > choices->size)
			options->num_lines = (unsigned int)choices->size;
		if (options->num_lines < 1u)
			options->num_lines = 1;
	}

	unsigned int num_lines = options->num_lines;
	size_t start = 0;
	size_t current_selection = choices->selection;
	if (current_selection + options->scrolloff >= num_lines) {
		start = current_selection + options->scrolloff - num_lines + 1;
		size_t available = choices_available(choices);
		if (start + num_lines >= available && available > 0) {
			start = available - num_lines;
		}
	}

	const int bordered = has_ui_border(tty, options);
	const int inner_cols = bordered ? (int)tty_getwidth(tty) - 4 : INT_MAX;

	if (bordered) {
		if (!state->border_first_draw)
			tty_moveup(tty, 1);
		draw_border_horizontal(tty, options, 0);
		tty_printf(tty, "\n");
	}

	tty_setnormal(tty);
	border_left(tty, options);
	if (bordered)
		border_inner_pad(tty, options);
	else
		inner_colors(tty, options);
	fputs_prompt_query(tty, options, options->prompt, state->search);
	if (options->info_mode == FZY_INFO_INLINE) {
		inner_colors(tty, options);
		print_prompt_info_suffix(tty, options, choices);
	} else if (options->info_mode == FZY_INFO_INLINE_RIGHT) {
		print_prompt_info_inline_right(tty, options, choices, state->search, bordered);
	}
	tty_clearline(tty);
	border_right(tty, options);
	if (bordered)
		tty_setwrap(tty);

	if (options->info_mode == FZY_INFO_DEFAULT)
		draw_match_stats_line(tty, options, choices, bordered, 0);

	if (options->header) {
		tty_printf(tty, "\n");
		tty_setnormal(tty);
		border_left(tty, options);
		if (bordered)
			border_inner_pad(tty, options);
		else
			inner_colors(tty, options);
		if (options->color_sgr_header[0]) {
			fputs(options->color_sgr_header, tty->fout);
			tty_invalidate_fg(tty);
		}
		tty_printf(tty, "%s", options->header);
		if (options->color_sgr_header[0]) {
			if (options->color_sgr_fg[0]) {
				fputs(options->color_sgr_fg, tty->fout);
				tty_invalidate_fg(tty);
			} else {
				tty_setfg(tty, TTY_COLOR_NORMAL);
			}
		}
		tty_clearline(tty);
		border_right(tty, options);
		if (bordered)
			tty_setwrap(tty);
	}

	if (options->show_info)
		draw_match_stats_line(tty, options, choices, bordered, 1);

	for (size_t i = start; i < start + num_lines; i++) {
		tty_printf(tty, "\n");
		tty_setnormal(tty);
		border_left(tty, options);
		if (bordered)
			border_inner_pad(tty, options);
		else
			inner_colors(tty, options);
		tty_clearline(tty);
		const char *choice = choices_get(choices, i);
		if (choice)
			draw_match(state, choice, i == choices->selection, inner_cols);
		border_right(tty, options);
		if (bordered)
			tty_setwrap(tty);
	}

	if (bordered) {
		tty_printf(tty, "\n");
		draw_border_horizontal(tty, options, 1);
	}

	{
		unsigned int above_list = (options->header ? 1 : 0) + (options->show_info ? 1 : 0) +
					  (options->info_mode == FZY_INFO_DEFAULT ? 1 : 0);
		unsigned int move = num_lines + above_list;
		if (bordered)
			move++;
		if (move)
			tty_moveup(tty, move);
	}

	tty_setnormal(tty);
	tty_setcol(tty, 0);
	border_left(tty, options);
	if (bordered)
		border_inner_pad(tty, options);
	else
		inner_colors(tty, options);
	/* Redraw prompt+query once from the inner column: cursor-only motion leaves some
	 * terminals with a corrupted line; reprinting after EL avoids duplicate full lines
	 * (--info=inline / --info=inline-right) and stray characters (plain prompt). */
	tty_clearline(tty);
	fputs_prompt_query(tty, options, options->prompt, state->search);
	if (options->info_mode == FZY_INFO_INLINE) {
		inner_colors(tty, options);
		print_prompt_info_suffix(tty, options, choices);
	} else if (options->info_mode == FZY_INFO_INLINE_RIGHT) {
		print_prompt_info_inline_right(tty, options, choices, state->search, bordered);
	}
	{
		int base = bordered ? 2 : 0;
		int col = base + (int)tty_str_vis_columns(options->prompt);
		size_t len = strlen(state->search);
		size_t c = state->cursor;
		if (c > len)
			c = len;
		if (c > SEARCH_SIZE_MAX)
			c = SEARCH_SIZE_MAX;
		if (c > 0) {
			char save = state->search[c];
			state->search[c] = '\0';
			col += (int)tty_str_vis_columns(state->search);
			state->search[c] = save;
		}
		tty_setcol(tty, col);
	}
	if (bordered)
		tty_setwrap(tty);
	tty_flush(tty);

	if (bordered)
		state->border_first_draw = 0;
}

static void update_search(tty_interface_t *state) {
	choices_search(state->choices, state->search);
	strcpy(state->last_search, state->search);
}

static void update_state(tty_interface_t *state) {
	if (strcmp(state->last_search, state->search)) {
		update_search(state);
		draw(state);
	}
}

static void action_emit(tty_interface_t *state) {
	update_state(state);

	/* Reset the tty as close as possible to the previous state */
	clear(state);

	/* ttyout should be flushed before outputting on stdout */
	tty_close(state->tty);

	const char *selection = choices_get(state->choices, state->choices->selection);
	if (selection) {
		/* output the selected result */
		printf("%s\n", selection);
	} else {
		/* No match, output the query instead */
		printf("%s\n", state->search);
	}

	state->exit = EXIT_SUCCESS;
}

static void action_del_char(tty_interface_t *state) {
	size_t length = strlen(state->search);
	if (state->cursor == 0) {
		return;
	}
	size_t original_cursor = state->cursor;

	do {
		state->cursor--;
	} while (!is_boundary(state->search[state->cursor]) && state->cursor);

	memmove(&state->search[state->cursor], &state->search[original_cursor], length - original_cursor + 1);
}

static void action_del_word(tty_interface_t *state) {
	size_t original_cursor = state->cursor;
	size_t cursor = state->cursor;

	while (cursor && isspace(state->search[cursor - 1]))
		cursor--;

	while (cursor && !isspace(state->search[cursor - 1]))
		cursor--;

	memmove(&state->search[cursor], &state->search[original_cursor], strlen(state->search) - original_cursor + 1);
	state->cursor = cursor;
}

static void action_del_all(tty_interface_t *state) {
	memmove(state->search, &state->search[state->cursor], strlen(state->search) - state->cursor + 1);
	state->cursor = 0;
}

static void action_prev(tty_interface_t *state) {
	update_state(state);
	choices_prev(state->choices);
}

static void action_ignore(tty_interface_t *state) {
	(void)state;
}

static void action_next(tty_interface_t *state) {
	update_state(state);
	choices_next(state->choices);
}

static void action_left(tty_interface_t *state) {
	if (state->cursor > 0) {
		state->cursor--;
		while (!is_boundary(state->search[state->cursor]) && state->cursor)
			state->cursor--;
	}
}

static void action_right(tty_interface_t *state) {
	if (state->cursor < strlen(state->search)) {
		state->cursor++;
		while (!is_boundary(state->search[state->cursor]))
			state->cursor++;
	}
}

static void action_beginning(tty_interface_t *state) {
	state->cursor = 0;
}

static void action_end(tty_interface_t *state) {
	state->cursor = strlen(state->search);
}

static void action_pageup(tty_interface_t *state) {
	update_state(state);
	for (size_t i = 0; i < state->options->num_lines && state->choices->selection > 0; i++)
		choices_prev(state->choices);
}

static void action_pagedown(tty_interface_t *state) {
	update_state(state);
	for (size_t i = 0; i < state->options->num_lines && state->choices->selection < state->choices->available - 1; i++)
		choices_next(state->choices);
}

static void action_autocomplete(tty_interface_t *state) {
	update_state(state);
	const char *current_selection = choices_get(state->choices, state->choices->selection);
	if (current_selection) {
		strncpy(state->search, choices_get(state->choices, state->choices->selection), SEARCH_SIZE_MAX);
		state->cursor = strlen(state->search);
	}
}

static void action_exit(tty_interface_t *state) {
	clear(state);
	tty_close(state->tty);

	state->exit = EXIT_FAILURE;
}

static void append_search(tty_interface_t *state, char ch) {
	char *search = state->search;
	size_t search_size = strlen(search);
	if (search_size < SEARCH_SIZE_MAX) {
		memmove(&search[state->cursor+1], &search[state->cursor], search_size - state->cursor + 1);
		search[state->cursor] = ch;

		state->cursor++;
	}
}

void tty_interface_init(tty_interface_t *state, tty_t *tty, choices_t *choices, options_t *options) {
	state->tty = tty;
	state->choices = choices;
	state->options = options;
	state->ambiguous_key_pending = 0;
	state->border_first_draw  = 1;

	strcpy(state->input, "");
	strcpy(state->search, "");
	strcpy(state->last_search, "");

	state->exit = -1;

	if (options->init_search)
		strncpy(state->search, options->init_search, SEARCH_SIZE_MAX);

	state->cursor = strlen(state->search);

	update_search(state);
}

typedef struct {
	const char *key;
	void (*action)(tty_interface_t *);
} keybinding_t;

#define KEY_CTRL(key) ((const char[]){((key) - ('@')), '\0'})

static const keybinding_t keybindings[] = {{"\x1b", action_exit},       /* ESC */
					   {"\x7f", action_del_char},	/* DEL */

					   {KEY_CTRL('H'), action_del_char}, /* Backspace (C-H) */
					   {KEY_CTRL('W'), action_del_word}, /* C-W */
					   {KEY_CTRL('U'), action_del_all},  /* C-U */
					   {KEY_CTRL('I'), action_autocomplete}, /* TAB (C-I ) */
					   {KEY_CTRL('C'), action_exit},	 /* C-C */
					   {KEY_CTRL('D'), action_exit},	 /* C-D */
					   {KEY_CTRL('G'), action_exit},	 /* C-G */
					   {KEY_CTRL('M'), action_emit},	 /* CR */
					   {KEY_CTRL('P'), action_prev},	 /* C-P */
					   {KEY_CTRL('N'), action_next},	 /* C-N */
					   {KEY_CTRL('K'), action_prev},	 /* C-K */
					   {KEY_CTRL('J'), action_next},	 /* C-J */
					   {KEY_CTRL('A'), action_beginning},    /* C-A */
					   {KEY_CTRL('E'), action_end},		 /* C-E */

					   {"\x1bOD", action_left}, /* LEFT */
					   {"\x1b[D", action_left}, /* LEFT */
					   {"\x1bOC", action_right}, /* RIGHT */
					   {"\x1b[C", action_right}, /* RIGHT */
					   {"\x1b[1~", action_beginning}, /* HOME */
					   {"\x1b[H", action_beginning}, /* HOME */
					   {"\x1b[4~", action_end}, /* END */
					   {"\x1b[F", action_end}, /* END */
					   {"\x1b[A", action_prev}, /* UP */
					   {"\x1bOA", action_prev}, /* UP */
					   {"\x1b[B", action_next}, /* DOWN */
					   {"\x1bOB", action_next}, /* DOWN */
					   {"\x1b[5~", action_pageup},
					   {"\x1b[6~", action_pagedown},
					   {"\x1b[200~", action_ignore},
					   {"\x1b[201~", action_ignore},
					   {NULL, NULL}};

#undef KEY_CTRL

static void handle_input(tty_interface_t *state, const char *s, int handle_ambiguous_key) {
	state->ambiguous_key_pending = 0;

	char *input = state->input;
	strcat(state->input, s);

	/* Figure out if we have completed a keybinding and whether we're in the
	 * middle of one (both can happen, because of Esc). */
	int found_keybinding = -1;
	int in_middle = 0;
	for (int i = 0; keybindings[i].key; i++) {
		if (!strcmp(input, keybindings[i].key))
			found_keybinding = i;
		else if (!strncmp(input, keybindings[i].key, strlen(state->input)))
			in_middle = 1;
	}

	/* If we have an unambiguous keybinding, run it.  */
	if (found_keybinding != -1 && (!in_middle || handle_ambiguous_key)) {
		keybindings[found_keybinding].action(state);
		strcpy(input, "");
		return;
	}

	/* We could have a complete keybinding, or could be in the middle of one.
	 * We'll need to wait a few milliseconds to find out. */
	if (found_keybinding != -1 && in_middle) {
		state->ambiguous_key_pending = 1;
		return;
	}

	/* Wait for more if we are in the middle of a keybinding */
	if (in_middle)
		return;

	/* No matching keybinding, add to search */
	for (int i = 0; input[i]; i++)
		if (isprint_unicode(input[i]))
			append_search(state, input[i]);

	/* We have processed the input, so clear it */
	strcpy(input, "");
}

int tty_interface_run(tty_interface_t *state) {
	draw(state);

	for (;;) {
		do {
			while(!tty_input_ready(state->tty, -1, 1)) {
				/* We received a signal (probably WINCH) */
				draw(state);
			}

			char s[2] = {tty_getchar(state->tty), '\0'};
			handle_input(state, s, 0);

			if (state->exit >= 0)
				return state->exit;

			draw(state);
		} while (tty_input_ready(state->tty, state->ambiguous_key_pending ? KEYTIMEOUT : 0, 0));

		if (state->ambiguous_key_pending) {
			char s[1] = "";
			handle_input(state, s, 1);

			if (state->exit >= 0)
				return state->exit;
		}

		update_state(state);
	}

	return state->exit;
}
