/* Window functions
   Copyright (C) 2012-2016 Markus Uhlin. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

   - Neither the name of the author nor the names of its contributors may be
     used to endorse or promote products derived from this software without
     specific prior written permission.

   THIS SOFTWARE IS PROVIDED THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE. */

#include "common.h"

/* names.h wants this header before itself */
#include "irc.h"
#include "events/names.h"

#include "assertAPI.h"
#include "config.h"
#if defined(WIN32) && defined(PDC_EXP_EXTRAS)
#include "curses-funcs.h"	/* is_scrollok() etc */
#endif
#include "dataClassify.h"
#include "errHand.h"
#include "libUtils.h"
#include "printtext.h"		/* includes window.h */
#include "readline.h"		/* readline_top_panel() */
#include "statusbar.h"
#include "strHand.h"
#include "terminal.h"
#include "titlebar.h"

#define foreach_hash_table_entry(entry_p) \
	for (entry_p = &hash_table[0]; entry_p < &hash_table[ARRAY_SIZE(hash_table)]; entry_p++)

/* Structure definitions
   ===================== */

struct hInstall_context {
    char	*label;
    char	*title;
    PANEL	*pan;
    int		 refnum;
};

/* Objects with external linkage
   ============================= */

const char	g_status_window_label[] = "(status)";
PIRC_WINDOW	g_status_window         = NULL;
PIRC_WINDOW	g_active_window         = NULL;
int		g_ntotal_windows        = 0;

/* Objects with internal linkage
   ============================= */

static PIRC_WINDOW hash_table[200];

/* Function declarations
   ===================== */

static PIRC_WINDOW	hInstall                (const struct hInstall_context *);
static unsigned int	hash                    (const char *label);
static void		apply_window_options    (WINDOW *);
static void		hUndef                  (PIRC_WINDOW entry);
static void		reassign_window_refnums (void);
static void		window_recreate         (PIRC_WINDOW, int rows, int cols);
static void		window_redraw           (PIRC_WINDOW, int rows);

void
windowSystem_init(void)
{
    PIRC_WINDOW *entry_p;

    foreach_hash_table_entry(entry_p) {
	*entry_p = NULL;
    }

    g_status_window = g_active_window = NULL;
    g_ntotal_windows = 0;

    if ((errno = spawn_chat_window(g_status_window_label, "")) != 0) {
	err_sys("spawn_chat_window error");
    }

    if ((g_status_window = window_by_label(g_status_window_label)) == NULL) {
	err_quit("Unable to locate the status window\nShouldn't happen.");
    }
}

void
windowSystem_deinit(void)
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW p, tmp;

    foreach_hash_table_entry(entry_p) {
	for (p = *entry_p; p != NULL; p = tmp) {
	    tmp = p->next;
	    hUndef(p);
	}
    }
}

int
spawn_chat_window(const char *label, const char *title)
{
    const int ntotalp1 = g_ntotal_windows + 1;
    struct integer_unparse_context unparse_ctx = {
	.setting_name	  = "max_chat_windows",
	.fallback_default = 60,
	.lo_limit	  = 10,
	.hi_limit	  = 200,
    };

    if (isNull(label) || isEmpty(label)) {
	return (EINVAL);	/* a label is required */
    } else if (window_by_label(label) != NULL) {
	return (0);		/* window already exists  --  reuse it */
    } else if (ntotalp1 > config_integer_unparse(&unparse_ctx)) {
	return (ENOSPC);
    } else {
	struct hInstall_context inst_ctx = {
	    .label  = (char *) label,
	    .title  = (char *) title,
	    .pan    = term_new_panel(LINES - 2, 0, 1, 0),
	    .refnum = g_ntotal_windows + 1,
	};
	PIRC_WINDOW entry = hInstall(&inst_ctx);

	apply_window_options(panel_window(entry->pan));
	errno = changeWindow_by_label(entry->label);
	sw_assert_perror(errno);
    }

    return (0);
}

int
destroy_chat_window(const char *label)
{
    PIRC_WINDOW window;

    if (isNull(label) || isEmpty(label) ||
	Strings_match_ignore_case(label, g_status_window_label)) {
	return (EINVAL);
    } else if ((window = window_by_label(label)) == NULL) {
	return (ENOENT);
    } else {
	hUndef(window);
	reassign_window_refnums();
	errno = changeWindow_by_refnum(g_ntotal_windows);
	sw_assert_perror(errno);
    }

    return (0);
}

static PIRC_WINDOW
hInstall(const struct hInstall_context *ctx)
{
    PIRC_WINDOW		 entry;
    PNAMES		*n_ent;
    unsigned int	 hashval;

    entry	  = xmalloc(sizeof *entry);
    entry->label  = sw_strdup(ctx->label);
    entry->title  =
	(isNull(ctx->title) || isEmpty(ctx->title) ? NULL : sw_strdup(ctx->title));
    entry->pan    = ctx->pan;
    entry->refnum = ctx->refnum;
    entry->buf    = textBuf_new();

    for (n_ent = &entry->names_hash[0]; n_ent < &entry->names_hash[NAMES_HASH_TABLE_SIZE]; n_ent++) {
	*n_ent = NULL;
    }

    entry->num_ops     = 0;
    entry->num_halfops = 0;
    entry->num_voices  = 0;
    entry->num_normal  = 0;
    entry->num_total   = 0;

    hashval             = hash(ctx->label);
    entry->next         = hash_table[hashval];
    hash_table[hashval] = entry;

    g_ntotal_windows++;

    return entry;
}

static void
hUndef(PIRC_WINDOW entry)
{
    PIRC_WINDOW tmp;
    unsigned int hashval = hash(entry->label);

    if ((tmp = hash_table[hashval]) == entry) {
	hash_table[hashval] = entry->next;
    } else {
	while (tmp->next != entry) {
	    tmp = tmp->next;
	}

	tmp->next = entry->next;
    }

    free_and_null(& (entry->label));
    free_and_null(& (entry->title));

    term_remove_panel(entry->pan);

    entry->pan	  = NULL;
    entry->refnum = -1;

    textBuf_destroy(entry->buf);
    event_names_htbl_remove_all(entry);

    entry->num_ops     = 0;
    entry->num_halfops = 0;
    entry->num_voices  = 0;
    entry->num_normal  = 0;
    entry->num_total   = 0;

    free_not_null(entry);
    entry = NULL;

    g_ntotal_windows--;
}

static unsigned int
hash(const char *label)
{
    char c;
    unsigned int hashval = 0;
    unsigned int tmp;

    while ((c = *label++) != '\0') {
	hashval = (hashval << 4) + c;
	tmp = hashval & 0xf0000000;

	if (tmp) {
	    hashval ^= (tmp >> 24);
	    hashval ^= tmp;
	}
    }

    return (hashval % ARRAY_SIZE(hash_table));
}

static void
apply_window_options(WINDOW *win)
{
    if (!is_scrollok(win)) {
	(void) scrollok(win, TRUE);
    }
}

PIRC_WINDOW
window_by_label(const char *label)
{
    PIRC_WINDOW window;

    if (label == NULL || *label == '\0') {
	return (NULL);
    }

    for (window = hash_table[hash(label)]; window != NULL; window = window->next) {
	if (Strings_match_ignore_case(label, window->label))
	    return (window);
    }

    return (NULL);
}

PIRC_WINDOW
window_by_refnum(int refnum)
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW	 window;

    if (refnum < 1 || refnum > g_ntotal_windows) {
	return (NULL);
    }

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    if (refnum == window->refnum) {
		return (window);
	    }
	}
    }

    return (NULL);
}

int
changeWindow_by_label(const char *label)
{
    PIRC_WINDOW window;

    if ((window = window_by_label(label)) == NULL) {
	return (ENOENT);	/* window not found */
    } else if (window == g_active_window) {
	return (0);		/* window already active */
    } else if (top_panel(window->pan) == ERR) {
	return (EPERM);		/* top_panel() error */
    } else {
	g_active_window = window;
	titlebar(" %s ", (window->title != NULL) ? window->title : "");
	statusbar_update_display_beta();
	readline_top_panel();
	(void) ungetch('\a');
    }

    return (0);
}

int
changeWindow_by_refnum(int refnum)
{
    PIRC_WINDOW window;

    if ((window = window_by_refnum(refnum)) == NULL) {
	return (ENOENT);
    } else if (window == g_active_window) {
	return (0);
    } else if (top_panel(window->pan) == ERR) {
	return (EPERM);
    } else {
	g_active_window = window;
	titlebar(" %s ", (window->title != NULL) ? window->title : "");
	statusbar_update_display_beta();
	readline_top_panel();
	(void) ungetch('\a');
    }

    return (0);
}

void
new_window_title(const char *label, const char *title)
{
    PIRC_WINDOW window;

    if ((window = window_by_label(label)) == NULL
	|| title == NULL
	|| *title == '\0') {
	return;
    }

    free_not_null(window->title);
    window->title = sw_strdup(title);

    if (window == g_active_window) {
	titlebar(" %s ", title);
    }
}

static void
reassign_window_refnums(void)
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW	 window;
    int		 ref_count = 1;

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    if (Strings_match_ignore_case(window->label, g_status_window_label))
		window->refnum = ++ref_count; /* skip status window and assign new num */
	}
    }

    sw_assert(g_status_window->refnum == 1);
    sw_assert(ref_count == g_ntotal_windows);
}

void
windows_recreate_all(int rows, int cols)
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW	 window;

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    window_recreate(window, rows, cols);
	}
    }
}

static void
window_recreate(PIRC_WINDOW window, int rows, int cols)
{
    struct term_window_size newsize;

    newsize.rows      = rows - 2;
    newsize.cols      = cols;
    newsize.start_row = 1;
    newsize.start_col = 0;

    window->pan = term_resize_panel(window->pan, &newsize);
    apply_window_options(panel_window(window->pan));
    window_redraw(window, rows);
}

static void
window_redraw(PIRC_WINDOW window, int rows)
{
    PTEXTBUF_ELMT	 element;
    WINDOW		*pwin = panel_window(window->pan);
    int			 pos  = int_diff(textBuf_size(window->buf), rows);

    if ((element = textBuf_get_element_by_pos(window->buf, pos < 0 ? 0 : pos)) == NULL) {
	return; /* Nothing stored in the buffer */
    }

    for (; element != NULL; element = element->next) {
	printtext_puts(pwin, element->text, element->indent, -1, NULL);
    }
}

void
window_select_prev(void)
{
    const int refnum_prev = g_active_window->refnum - 1;

    if (window_by_refnum(refnum_prev) != NULL) {
	(void) changeWindow_by_refnum(refnum_prev);
    }
}

void
window_select_next(void)
{
    const int refnum_next = g_active_window->refnum + 1;

    if (window_by_refnum(refnum_next) != NULL) {
	(void) changeWindow_by_refnum(refnum_next);
    }
}