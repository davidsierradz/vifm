#include <stic.h>

#include <stdio.h> /* snprintf() */
#include <string.h> /* strcmp() strlen() */
#include <wctype.h> /* towupper() */

#include <test-utils.h>

#include "../../src/cfg/config.h"
#include "../../src/compat/fs_limits.h"
#include "../../src/engine/keys.h"
#include "../../src/engine/mode.h"
#include "../../src/modes/jump.h"
#include "../../src/modes/modes.h"
#include "../../src/modes/wk.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/fs.h"
#include "../../src/utils/path.h"
#include "../../src/utils/str.h"
#include "../../src/utils/utils.h"
#include "../../src/filelist.h"
#include "../../src/flist_pos.h"
#include "../../src/status.h"

static void setup_sandbox_list(void);
static void teardown_sandbox_list(void);
static wchar_t single_char_label(int pos);

static view_t *const view = &lwin;
static char cwd[PATH_MAX + 1];

SETUP_ONCE()
{
	assert_non_null(get_cwd(cwd, sizeof(cwd)));
}

SETUP()
{
	modes_init();
	conf_setup();

	view_setup(view);
	view->window_rows = 8;

	curr_view = &lwin;
	other_view = &rwin;

	columns_setup_column(SK_BY_NAME);

	assert_success(stats_init(&cfg));
}

TEARDOWN()
{
	modjump_abort();

	conf_teardown();

	view_teardown(view);

	vle_keys_reset();

	columns_teardown();
}

TEST(labels_are_single_chars_when_there_is_enough_of_them)
{
	setup_grid(view, 1, 20, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	assert_true(vle_mode_is(JUMP_MODE));

	assert_null(modjump_get_label(view, 0));
	assert_string_equal("a", modjump_get_label(view, 1));
	assert_string_equal("s", modjump_get_label(view, 2));
	assert_string_equal("d", modjump_get_label(view, 3));
	assert_string_equal("l", modjump_get_label(view, 7));
	/* Not visible. */
	assert_null(modjump_get_label(view, 8));

	(void)vle_keys_exec_timed_out(WK_d);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(3, view->list_pos);
	assert_null(modjump_get_label(view, 1));
}

TEST(closer_entries_get_labels_first)
{
	setup_grid(view, 1, 20, 1);
	view->list_pos = 3;

	(void)vle_keys_exec_timed_out(WK_SECTION);

	assert_string_equal("a", modjump_get_label(view, 4));
	assert_string_equal("s", modjump_get_label(view, 2));
	assert_string_equal("d", modjump_get_label(view, 5));
	assert_string_equal("g", modjump_get_label(view, 1));
	assert_string_equal("k", modjump_get_label(view, 0));

	(void)vle_keys_exec_timed_out(WK_k);
	assert_int_equal(0, view->list_pos);
}

TEST(only_visible_entries_are_labeled)
{
	setup_grid(view, 1, 20, 1);
	view->top_line = 10;
	view->list_pos = 12;

	(void)vle_keys_exec_timed_out(WK_SECTION);

	assert_null(modjump_get_label(view, 9));
	assert_string_equal("s", modjump_get_label(view, 11));
	assert_string_equal("a", modjump_get_label(view, 13));
	assert_non_null(modjump_get_label(view, 17));
	assert_null(modjump_get_label(view, 18));
}

TEST(multichar_labels_are_prefix_free_and_narrow_down)
{
	update_string(&cfg.jump_labels, "ab");

	setup_grid(view, 1, 20, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);

	assert_string_equal("aa", modjump_get_label(view, 1));
	assert_string_equal("aba", modjump_get_label(view, 2));
	assert_string_equal("abb", modjump_get_label(view, 3));
	assert_string_equal("baa", modjump_get_label(view, 4));
	assert_string_equal("bab", modjump_get_label(view, 5));
	assert_string_equal("bba", modjump_get_label(view, 6));
	assert_string_equal("bbb", modjump_get_label(view, 7));

	(void)vle_keys_exec_timed_out(WK_a WK_b);
	assert_true(vle_mode_is(JUMP_MODE));
	assert_null(modjump_get_label(view, 1));
	assert_string_equal("a", modjump_get_label(view, 2));
	assert_string_equal("b", modjump_get_label(view, 3));
	assert_null(modjump_get_label(view, 4));

	/* Backspace undoes last character. */
	(void)vle_keys_exec_timed_out(WK_C_h);
	assert_string_equal("ba", modjump_get_label(view, 2));
	assert_string_equal("a", modjump_get_label(view, 1));

	(void)vle_keys_exec_timed_out(WK_b WK_b);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(3, view->list_pos);
}

TEST(invalid_jumplabels_fall_back_to_defaults)
{
	update_string(&cfg.jump_labels, "x");

	setup_grid(view, 1, 20, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	assert_string_equal("a", modjump_get_label(view, 1));
}

TEST(escape_and_unknown_keys_abort)
{
	setup_grid(view, 1, 20, 1);
	view->list_pos = 2;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	(void)vle_keys_exec_timed_out(WK_ESC);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(2, view->list_pos);
	assert_null(modjump_get_label(view, 3));

	(void)vle_keys_exec_timed_out(WK_SECTION);
	(void)vle_keys_exec_timed_out(WK_C_c);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(2, view->list_pos);

	/* 'z' isn't used by any label among the first few. */
	update_string(&cfg.jump_labels, "asd");
	(void)vle_keys_exec_timed_out(WK_SECTION);
	(void)vle_keys_exec_timed_out(WK_z);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(2, view->list_pos);
}

TEST(sole_target_is_jumped_to_immediately)
{
	setup_grid(view, 1, 2, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(1, view->list_pos);
}

TEST(grid_cells_are_labeled)
{
	setup_grid(view, 2, 17, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	assert_string_equal("a", modjump_get_label(view, 1));
	assert_non_null(modjump_get_label(view, 15));
	assert_null(modjump_get_label(view, 16));
}

TEST(visual_mode_selection_is_extended)
{
	setup_grid(view, 1, 20, 1);
	view->list_pos = 1;

	/* Entries with empty names can't be selected. */
	int i;
	for(i = 0; i < view->list_rows; ++i)
	{
		char name[16];
		snprintf(name, sizeof(name), "file%d", i);
		replace_string(&view->dir_entry[i].name, name);
	}

	(void)vle_keys_exec_timed_out(WK_v);
	assert_true(vle_mode_is(VISUAL_MODE));

	/* Targets: 2 (a), 0 (s), 3 (d), 4 (g). */
	(void)vle_keys_exec_timed_out(WK_SECTION WK_g);
	assert_true(vle_mode_is(VISUAL_MODE));
	assert_int_equal(4, view->list_pos);

	assert_false(view->dir_entry[0].selected);
	assert_true(view->dir_entry[1].selected);
	assert_true(view->dir_entry[2].selected);
	assert_true(view->dir_entry[3].selected);
	assert_true(view->dir_entry[4].selected);
	assert_false(view->dir_entry[5].selected);

	(void)vle_keys_exec_timed_out(WK_C_c);
}

TEST(capital_label_opens_target)
{
	setup_sandbox_list();

	(void)vle_keys_exec_timed_out(WK_SECTION);
	const int c_pos = fpos_find_by_name(view, "c");
	assert_true(c_pos >= 0);
	const wchar_t keys[] = { towupper(single_char_label(c_pos)), L'\0' };

	char *saved_cwd = save_cwd();
	(void)vle_keys_exec_timed_out(keys);
	restore_cwd(saved_cwd);

	assert_true(vle_mode_is(NORMAL_MODE));
	assert_string_equal("c", get_last_path_component(view->curr_dir));

	teardown_sandbox_list();
}

TEST(removing_all_typed_chars_cancels_opening)
{
	update_string(&cfg.jump_labels, "ab");
	setup_sandbox_list();

	(void)vle_keys_exec_timed_out(WK_SECTION);

	/* Find an entry with a multi-character label. */
	int pos;
	const char *label = NULL;
	for(pos = 0; pos < view->list_rows; ++pos)
	{
		label = modjump_get_label(view, pos);
		if(label != NULL && strlen(label) > 1)
		{
			break;
		}
	}
	assert_true(pos < view->list_rows);

	wchar_t keys[16] = { towupper(label[0]), WC_C_h };
	size_t i;
	for(i = 0; label[i] != '\0'; ++i)
	{
		keys[2 + i] = label[i];
	}

	char *saved_cwd = save_cwd();
	(void)vle_keys_exec_timed_out(keys);
	restore_cwd(saved_cwd);

	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(pos, view->list_pos);
	assert_true(fpos_find_by_name(view, "c") >= 0);

	teardown_sandbox_list();
}

TEST(capital_label_char_is_a_label_on_its_own)
{
	update_string(&cfg.jump_labels, "aA");

	setup_grid(view, 1, 3, 1);
	view->list_pos = 0;

	(void)vle_keys_exec_timed_out(WK_SECTION);
	assert_string_equal("a", modjump_get_label(view, 1));
	assert_string_equal("A", modjump_get_label(view, 2));

	(void)vle_keys_exec_timed_out(WK_A);
	assert_true(vle_mode_is(NORMAL_MODE));
	assert_int_equal(2, view->list_pos);
}

TEST(labels_validation)
{
	assert_false(modjump_labels_valid(""));
	assert_false(modjump_labels_valid("a"));
	assert_false(modjump_labels_valid("aaa"));
	assert_false(modjump_labels_valid(" \t"));
	assert_true(modjump_labels_valid("ab"));
	assert_true(modjump_labels_valid("a1"));
}

/* Fills the view with directories from sandbox (cursor on the first entry). */
static void
setup_sandbox_list(void)
{
	create_dir(SANDBOX_PATH "/a");
	create_dir(SANDBOX_PATH "/b");
	create_dir(SANDBOX_PATH "/c");
	create_dir(SANDBOX_PATH "/d");

	make_abs_path(view->curr_dir, sizeof(view->curr_dir), SANDBOX_PATH, "", cwd);
	populate_dir_list(view, /*reload=*/0);
	assert_true(view->list_rows >= 4);

	view->column_count = 1;
	view->run_size = 1;
	view->window_cells = view->window_rows;
	view->list_pos = 0;
}

/* Removes directories created by setup_sandbox_list(). */
static void
teardown_sandbox_list(void)
{
	remove_dir(SANDBOX_PATH "/a");
	remove_dir(SANDBOX_PATH "/b");
	remove_dir(SANDBOX_PATH "/c");
	remove_dir(SANDBOX_PATH "/d");
}

/* Retrieves single-character label of an entry.  Returns the character. */
static wchar_t
single_char_label(int pos)
{
	const char *const label = modjump_get_label(view, pos);
	assert_non_null(label);
	assert_int_equal(1, strlen(label));
	return label[0];
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
