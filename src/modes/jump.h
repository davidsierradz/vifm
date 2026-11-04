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

#ifndef VIFM__MODES__JUMP_H__
#define VIFM__MODES__JUMP_H__

/* Jump mode labels visible entries of a view with short key sequences (in the
 * style of hop.nvim) and moves cursor to the entry whose label gets typed. */

struct view_t;

/* Initializes jump mode. */
void modjump_init(void);

/* Labels visible entries of the view and enters jump mode.  If there is only
 * one target, cursor is moved to it immediately and the mode isn't entered. */
void modjump_enter(struct view_t *view);

/* Leaves jump mode without moving the cursor. */
void modjump_abort(void);

/* Retrieves part of the label of an entry at specified position that is still
 * to be typed.  Returns NULL if the entry has no label or its label doesn't
 * match what has been typed so far. */
const char * modjump_get_label(const struct view_t *view, int pos);

/* Checks whether string is suitable to be used as a set of label characters.
 * Returns non-zero if so, otherwise zero is returned. */
int modjump_labels_valid(const char labels[]);

#endif /* VIFM__MODES__JUMP_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
