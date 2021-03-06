/* Window functions
   Copyright (C) 2012-2017 Markus Uhlin. All rights reserved.

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

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
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
#include "io-loop.h"		/* get_prompt() */
#include "libUtils.h"
#include "printtext.h"		/* includes window.h */
#include "readline.h"		/* readline_top_panel() */
#include "statusbar.h"
#include "strHand.h"
#include "terminal.h"
#include "titlebar.h"

#define foreach_hash_table_entry(entry_p) \
	for (entry_p = &hash_table[0]; \
	     entry_p < &hash_table[ARRAY_SIZE(hash_table)]; \
	     entry_p++)

#define IS_AT_TOP (window->saved_size > 0 && \
		   window->saved_size == window->scroll_count)
#define SCROLL_OFFSET 6

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

static unsigned int
hash(const char *label)
{
    char		 c;
    char		*label_copy = strToLower(sw_strdup(label));
    char		*label_p    = label_copy;
    unsigned int	 hashval    = 0;
    unsigned int	 tmp;

    while ((c = *label_p++) != '\0') {
	hashval = (hashval << 4) + c;
	tmp = hashval & 0xf0000000;

	if (tmp) {
	    hashval ^= (tmp >> 24);
	    hashval ^= tmp;
	}
    }

    free(label_copy);
    return (hashval % ARRAY_SIZE(hash_table));
}

PIRC_WINDOW
window_by_label(const char *label)
{
    PIRC_WINDOW window;

    if (label == NULL || *label == '\0') {
	return (NULL);
    }

    for (window = hash_table[hash(label)];
	 window != NULL;
	 window = window->next) {
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
	WINDOW *pwin = readline_get_active_pwin();
	char *prompt = NULL;

	g_active_window = window;
	titlebar(" %s ", (window->title != NULL) ? window->title : "");
	statusbar_update_display_beta();
	if (pwin) {
	    werase(pwin);
	    prompt = get_prompt();
	    waddnstr(pwin, prompt, -1);
	    free(prompt);
	}
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
	WINDOW *pwin = readline_get_active_pwin();
	char *prompt = NULL;

	g_active_window = window;
	titlebar(" %s ", (window->title != NULL) ? window->title : "");
	statusbar_update_display_beta();
	if (pwin) {
	    werase(pwin);
	    prompt = get_prompt();
	    waddnstr(pwin, prompt, -1);
	    free(prompt);
	}
	readline_top_panel();
	(void) ungetch('\a');
    }

    return (0);
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

    entry->num_owners	= 0;
    entry->num_superops = 0;
    entry->num_ops	= 0;
    entry->num_halfops	= 0;
    entry->num_voices	= 0;
    entry->num_normal	= 0;
    entry->num_total	= 0;

    free_not_null(entry);
    entry = NULL;

    g_ntotal_windows--;
}

static void
reassign_window_refnums()
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW	 window;
    int		 ref_count = 1;

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    if (!Strings_match_ignore_case(window->label,
					   g_status_window_label)) {
		/* skip status window and assign new num */
		window->refnum = ++ref_count;
	    }
	}
    }

    sw_assert(g_status_window->refnum == 1);
    sw_assert(ref_count == g_ntotal_windows);
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

    entry	  = xcalloc(sizeof *entry, 1);
    entry->label  = sw_strdup(ctx->label);
    entry->title  =
	((isNull(ctx->title) || isEmpty(ctx->title))
	 ? NULL
	 : sw_strdup(ctx->title));
    entry->pan    = ctx->pan;
    entry->refnum = ctx->refnum;
    entry->buf    = textBuf_new();

    entry->saved_size	= 0;
    entry->scroll_count = 0;
    entry->scroll_mode	= false;

    for (n_ent = &entry->names_hash[0];
	 n_ent < &entry->names_hash[NAMES_HASH_TABLE_SIZE];
	 n_ent++) {
	*n_ent = NULL;
    }

    entry->received_names = false;

    entry->num_owners	= 0;
    entry->num_superops = 0;
    entry->num_ops	= 0;
    entry->num_halfops	= 0;
    entry->num_voices	= 0;
    entry->num_normal	= 0;
    entry->num_total	= 0;

    BZERO(entry->chanmodes, sizeof entry->chanmodes);
    entry->received_chanmodes = false;
    entry->received_chancreated = false;

    hashval             = hash(ctx->label);
    entry->next         = hash_table[hashval];
    hash_table[hashval] = entry;

    g_ntotal_windows++;

    return entry;
}

static void
apply_window_options(WINDOW *win)
{
    if (!is_scrollok(win)) {
	(void) scrollok(win, true);
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

void
new_window_title(const char *label, const char *title)
{
    PIRC_WINDOW window;

    if ((window = window_by_label(label)) == NULL ||
	title == NULL ||
	*title == '\0') {
	return;
    }

    free_not_null(window->title);
    window->title = sw_strdup(title);

    if (window == g_active_window) {
	titlebar(" %s ", title);
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
window_close_all_priv_conv(void)
{
    PIRC_WINDOW   window         = NULL;
    PIRC_WINDOW  *entry_p        = NULL;
    char         *priv_conv[200] = { NULL };
    char        **ar_p           = NULL;
    size_t        pc_assigned    = 0;

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    if (window == g_status_window || is_irc_channel(window->label))
		continue;
	    if (window->label)
		priv_conv[pc_assigned++] = sw_strdup(window->label);
	}
    }
    if (pc_assigned == 0) {
	napms(50);
	return;
    }
    for (ar_p = &priv_conv[0]; ar_p < &priv_conv[pc_assigned]; ar_p++) {
	destroy_chat_window(*ar_p);
	free(*ar_p);
    }
}

void
window_foreach_destroy_names(void)
{
    PIRC_WINDOW *entry_p;
    PIRC_WINDOW	 window;

    foreach_hash_table_entry(entry_p) {
	for (window = *entry_p; window != NULL; window = window->next) {
	    if (is_irc_channel(window->label)) {
		event_names_htbl_remove_all(window);
		window->received_names = false;
#if 1
		window->num_owners   = 0;
		window->num_superops = 0;
		window->num_ops	     = 0;
		window->num_halfops  = 0;
		window->num_voices   = 0;
		window->num_normal   = 0;
		window->num_total    = 0;
#endif
		BZERO(window->chanmodes, sizeof window->chanmodes);
		window->received_chanmodes = false;
		window->received_chancreated = false;
	    }
	}
    }
}

static void
window_redraw(PIRC_WINDOW window, const int rows, const int pos,
	      bool limit_output)
{
    PTEXTBUF_ELMT	 element   = NULL;
    WINDOW		*pwin	   = panel_window(window->pan);
    int			 i	   = 0;
    int			 rep_count = 0;

    if (element = textBuf_get_element_by_pos(window->buf, pos < 0 ? 0 : pos),
	!element) {
	return; /* Nothing stored in the buffer */
    }

#if 1
    werase(pwin);
    update_panels();
#endif

    if (limit_output) {
	while (element != NULL && i < rows) {
	    printtext_puts(pwin, element->text, element->indent, rows - i,
			   &rep_count);
	    element = element->next;
	    i += rep_count;
	}
    } else {
	while (element != NULL && i < rows) {
	    printtext_puts(pwin, element->text, element->indent, -1, NULL);
	    element = element->next;
	    i++;
	}
    }

    statusbar_update_display_beta();
    readline_top_panel();
}

/* textBuf_size(window->buf) - window->saved_size */
void
window_scroll_down(PIRC_WINDOW window)
{
    const int HEIGHT = LINES - 3;

    if (! (window->scroll_mode)) {
	if (!config_bool_unparse("disable_beeps", false))
	    term_beep();
	return;
    }

    window->scroll_count -= SCROLL_OFFSET;

    if (! (window->scroll_count > HEIGHT)) {
	window->saved_size   = 0;
	window->scroll_count = 0;
	window->scroll_mode  = false;
	window_redraw(window, HEIGHT, textBuf_size(window->buf) - HEIGHT,
		      false);
	return;
    }

    window_redraw(window, HEIGHT, window->saved_size - window->scroll_count,
		  false);
}

void
window_scroll_up(PIRC_WINDOW window)
{
    const int MIN_SIZE = LINES - 3;

    if (MIN_SIZE < 0 || !(textBuf_size(window->buf) > MIN_SIZE) || IS_AT_TOP) {
	if (!config_bool_unparse("disable_beeps", false))
	    term_beep();
	return;
    }

    if (! (window->scroll_mode)) {
	window->saved_size  = textBuf_size(window->buf);
	window->scroll_mode = true;
    }

    if (window->scroll_count > window->saved_size) /* past top */
	window->scroll_count = window->saved_size;
    else {
	if (window->scroll_count == 0) /* first page up */
	    window->scroll_count += MIN_SIZE;

	window->scroll_count += SCROLL_OFFSET;

	if (window->scroll_count > window->saved_size)
	    window->scroll_count = window->saved_size;
    }

    if (IS_AT_TOP)
	window_redraw(window, MIN_SIZE, 0, true);
    else
	window_redraw(window, MIN_SIZE,
	    window->saved_size - window->scroll_count, true);
}

void
window_select_next(void)
{
    const int refnum_next = g_active_window->refnum + 1;

    if (window_by_refnum(refnum_next) != NULL) {
	(void) changeWindow_by_refnum(refnum_next);
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

    const int HEIGHT = rows - 3;

    if (window->scroll_mode) {
	if (! (window->scroll_count > HEIGHT)) {
	    window->saved_size   = 0;
	    window->scroll_count = 0;
	    window->scroll_mode  = false;
	    window_redraw(window, HEIGHT, textBuf_size(window->buf) - HEIGHT,
			  false);
	} else {
	    window_redraw(window, HEIGHT,
		window->saved_size - window->scroll_count, true);
	}

	return;
    }

    window_redraw(window, HEIGHT, textBuf_size(window->buf) - HEIGHT, false);
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
