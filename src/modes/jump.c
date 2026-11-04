/* vifm
 * Copyright (C) 2026 davidsierradz.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "jump.h"

#include <curses.h> /* KEY_BACKSPACE */

#include <assert.h> /* assert() */
#include <ctype.h> /* tolower() */
#include <stddef.h> /* NULL size_t wchar_t */
#include <stdlib.h> /* calloc() free() malloc() */
#include <string.h> /* strchr() strlen() strncmp() */

#include "../cfg/config.h"
#include "../compat/curses.h"
#include "../engine/keys.h"
#include "../engine/mode.h"
#include "../ui/fileview.h"
#include "../ui/statusbar.h"
#include "../ui/ui.h"
#include "../utils/macros.h"
#include "../flist_pos.h"
#include "../flist_sel.h"
#include "../running.h"
#include "../status.h"
#include "modes.h"
#include "visual.h"
#include "wk.h"

/* Label characters used when 'jumplabels' isn't usable. */
#define DEFAULT_LABELS "asdghklqwertyuiopzxcvbnmfj"

/* Maximum length of a label (more than enough for any realistic number of
 * entries even with just two label characters). */
#define MAX_LABEL_LEN 63

static int def_handler(wchar_t key);
static void cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_h(key_info_t key_info, keys_info_t *keys_info);
static int collect_targets(const view_t *view, int first, int last,
		int targets[]);
static int build_label_set(const char from[], char to[]);
static char ** make_labels(int n, const char keys[]);
static void jump_to(int pos, int open);
static void leave_jump_mode(void);
static void free_labels(void);
static void update_ui(void);

/* View whose entries are labeled. */
static view_t *view;
/* Mode which was active before entering jump mode. */
static int prev_mode;
/* Whether labels are active. */
static int active;
/* Position of the first entry covered by labels array. */
static int first_pos;
/* Number of elements in the labels array. */
static int nlabels;
/* Labels of entries starting at first_pos, NULL for unlabeled entries. */
static char **labels;
/* Characters labels are made of. */
static char label_chars[128];
/* Whether the target should be opened after jumping to it. */
static int open_target;
/* Characters typed so far. */
static char typed[MAX_LABEL_LEN + 1];
/* Number of characters typed so far. */
static size_t ntyped;

static keys_add_info_t builtin_cmds[] = {
	{WK_C_c, {{&cmd_ctrl_c}, .descr = "abort jumping"}},
	{WK_C_h, {{&cmd_ctrl_h}, .descr = "remove last typed label character"}},
	{WK_ESC, {{&cmd_ctrl_c}, .descr = "abort jumping"}},
	{WK_DELETE, {{&cmd_ctrl_h}, .descr = "remove last typed label character"}},
#ifdef ENABLE_EXTENDED_KEYS
	{{K(KEY_BACKSPACE)}, {{&cmd_ctrl_h},
	                      .descr = "remove last typed label character"}},
#endif /* ENABLE_EXTENDED_KEYS */
};

void
modjump_init(void)
{
	int ret_code = vle_keys_add(builtin_cmds, ARRAY_LEN(builtin_cmds), JUMP_MODE);
	assert(ret_code == 0 && "Failed to initialize jump mode keys.");
	(void)ret_code;

	vle_keys_set_def_handler(JUMP_MODE, &def_handler);
}

void
modjump_enter(view_t *v)
{
	if(v->list_rows <= 1)
	{
		return;
	}

	const int first = v->top_line;
	const int last = MIN(v->top_line + v->window_cells, v->list_rows) - 1;
	if(last < first)
	{
		return;
	}

	char *const keys = label_chars;
	if(build_label_set(cfg.jump_labels, keys) < 2)
	{
		(void)build_label_set(DEFAULT_LABELS, keys);
	}

	int *const targets = malloc(sizeof(*targets)*(last - first + 1));
	if(targets == NULL)
	{
		return;
	}

	const int ntargets = collect_targets(v, first, last, targets);
	if(ntargets == 0)
	{
		free(targets);
		return;
	}

	view = v;
	prev_mode = vle_mode_get();

	if(ntargets == 1)
	{
		const int target = targets[0];
		free(targets);
		jump_to(target, /*open=*/0);
		return;
	}

	char **const target_labels = make_labels(ntargets, keys);
	labels = calloc(last - first + 1, sizeof(*labels));
	if(target_labels == NULL || labels == NULL)
	{
		if(target_labels != NULL)
		{
			int i;
			for(i = 0; i < ntargets; ++i)
			{
				free(target_labels[i]);
			}
		}
		free(target_labels);
		free(labels);
		labels = NULL;
		free(targets);
		return;
	}

	int i;
	for(i = 0; i < ntargets; ++i)
	{
		labels[targets[i] - first] = target_labels[i];
	}
	free(target_labels);
	free(targets);

	first_pos = first;
	nlabels = last - first + 1;
	ntyped = 0U;
	typed[0] = '\0';
	open_target = 0;
	active = 1;

	vle_mode_set(JUMP_MODE, VMT_SECONDARY);
	update_ui();
}

void
modjump_abort(void)
{
	if(active)
	{
		leave_jump_mode();
	}
}

const char *
modjump_get_label(const view_t *v, int pos)
{
	if(!active || v != view || pos < first_pos || pos >= first_pos + nlabels)
	{
		return NULL;
	}

	const char *const label = labels[pos - first_pos];
	if(label == NULL || strncmp(label, typed, ntyped) != 0)
	{
		return NULL;
	}

	return label + ntyped;
}

int
modjump_labels_valid(const char labels[])
{
	char keys[128];
	return build_label_set(labels, keys) >= 2;
}

/* Processes label characters.  Returns zero. */
static int
def_handler(wchar_t key)
{
	char *const *label = NULL;
	int nmatches = 0;

	if(key <= L' ' || key >= 127 || ntyped == MAX_LABEL_LEN)
	{
		leave_jump_mode();
		return 0;
	}

	/* Upper case version of a label character (unless it's a label character
	 * on its own) requests opening the target after jumping to it. */
	if(key >= L'A' && key <= L'Z' && strchr(label_chars, (char)key) == NULL &&
			strchr(label_chars, tolower((char)key)) != NULL)
	{
		key = tolower((char)key);
		open_target = 1;
	}

	typed[ntyped++] = (char)key;
	typed[ntyped] = '\0';

	int i;
	for(i = 0; i < nlabels; ++i)
	{
		if(labels[i] != NULL && strncmp(labels[i], typed, ntyped) == 0)
		{
			label = &labels[i];
			++nmatches;
		}
	}

	if(nmatches == 0)
	{
		leave_jump_mode();
	}
	else if(nmatches == 1 && (*label)[ntyped] == '\0')
	{
		jump_to(first_pos + (label - labels), open_target);
	}
	else
	{
		update_ui();
	}

	return 0;
}

/* Leaves the mode without moving cursor. */
static void
cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info)
{
	leave_jump_mode();
}

/* Forgets last typed character or leaves the mode if nothing was typed. */
static void
cmd_ctrl_h(key_info_t key_info, keys_info_t *keys_info)
{
	if(ntyped == 0U)
	{
		leave_jump_mode();
		return;
	}

	typed[--ntyped] = '\0';
	if(ntyped == 0U)
	{
		open_target = 0;
	}
	update_ui();
}

/* Fills targets array with positions of entries in [first, last] range ordered
 * by their distance from cursor (closer ones first, entries after the cursor
 * go first on ties).  Returns number of targets. */
static int
collect_targets(const view_t *view, int first, int last, int targets[])
{
	const int cursor = view->list_pos;
	int count = 0;

	int distance;
	for(distance = 1; ; ++distance)
	{
		const int after = cursor + distance;
		const int before = cursor - distance;
		if(after > last && before < first)
		{
			break;
		}

		if(after >= first && after <= last)
		{
			targets[count++] = after;
		}
		if(before >= first && before <= last)
		{
			targets[count++] = before;
		}
	}

	return count;
}

/* Extracts unique printable non-whitespace ASCII characters from the string
 * preserving their order.  The to buffer must be at least 128 bytes long.
 * Returns number of extracted characters. */
static int
build_label_set(const char from[], char to[])
{
	int count = 0;
	to[0] = '\0';

	if(from == NULL)
	{
		return 0;
	}

	for(; *from != '\0'; ++from)
	{
		const char c = *from;
		if(c <= ' ' || c >= 127 || strchr(to, c) != NULL)
		{
			continue;
		}

		to[count++] = c;
		to[count] = '\0';
	}

	return count;
}

/* Generates prefix-free labels for n targets out of a set of keys (at least two
 * of them).  Labels are as short as possible and earlier targets get shorter
 * labels.  Returns array of n newly allocated strings or NULL on error. */
static char **
make_labels(int n, const char keys[])
{
	const int k = strlen(keys);
	assert(k >= 2 && "Need at least two keys to generate labels.");

	/* Find the smallest depth d such that k^d >= n, nodes = k^(d - 1). */
	int depth = 1;
	long long nodes = 1;
	while(nodes*k < n)
	{
		nodes *= k;
		++depth;
	}

	/* Some of the nodes at depth (d - 1) become labels themselves and the rest
	 * (the last ones) are expanded into k labels of length d each. */
	const long long expanded = (depth == 1)
	                         ? 0
	                         : (n - nodes + (k - 2))/(k - 1);
	const long long leaves = (depth == 1) ? 0 : nodes - expanded;

	char **const result = calloc(n, sizeof(*result));
	if(result == NULL)
	{
		return NULL;
	}

	int i;
	for(i = 0; i < n; ++i)
	{
		long long node;
		int len;
		int last_key = -1;

		if(depth == 1)
		{
			node = i;
			len = 1;
		}
		else if(i < leaves)
		{
			node = i;
			len = depth - 1;
		}
		else
		{
			node = leaves + (i - leaves)/k;
			last_key = (i - leaves)%k;
			len = depth - 1;
		}

		char *const label = malloc(len + (last_key >= 0) + 1);
		if(label == NULL)
		{
			int j;
			for(j = 0; j < i; ++j)
			{
				free(result[j]);
			}
			free(result);
			return NULL;
		}

		int j;
		for(j = len - 1; j >= 0; --j)
		{
			label[j] = keys[node%k];
			node /= k;
		}
		if(last_key >= 0)
		{
			label[len++] = keys[last_key];
		}
		label[len] = '\0';

		result[i] = label;
	}

	return result;
}

/* Leaves the mode and moves cursor to specified position optionally opening
 * the target (selection in Visual mode) afterwards. */
static void
jump_to(int pos, int open)
{
	leave_jump_mode();

	/* The list could have been updated while labels were displayed. */
	if(pos >= view->list_rows)
	{
		pos = view->list_rows - 1;
	}

	if(prev_mode == VISUAL_MODE)
	{
		view->list_pos = pos;
		modvis_update();
		if(open)
		{
			modvis_open();
		}
	}
	else
	{
		fpos_set_pos(view, pos);
		if(open)
		{
			rn_open(view, FHE_RUN);
			flist_sel_stash(view);
			redraw_view(view);
		}
	}
}

/* Restores previous mode and removes labels from the screen. */
static void
leave_jump_mode(void)
{
	const int was_active = active;

	active = 0;
	free_labels();

	if(vle_mode_is(JUMP_MODE))
	{
		vle_mode_set(prev_mode, VMT_PRIMARY);
	}

	if(was_active)
	{
		redraw_view(view);
		curr_stats.save_msg = 0;
		modes_statusbar_update();
	}
}

/* Frees labels and resets related state. */
static void
free_labels(void)
{
	int i;
	for(i = 0; i < nlabels; ++i)
	{
		free(labels[i]);
	}
	free(labels);
	labels = NULL;
	nlabels = 0;
	ntyped = 0U;
	typed[0] = '\0';
}

/* Redraws the view to reflect current state of labels. */
static void
update_ui(void)
{
	redraw_view(view);
	ui_sb_msgf("-- JUMP%s --%s%s", (open_target ? " (open)" : ""),
			(ntyped == 0U ? "" : " "), typed);
	curr_stats.save_msg = 2;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
