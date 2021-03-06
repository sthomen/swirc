/* Swirc statusbar
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

#if defined(WIN32) && defined(PDC_EXP_EXTRAS)
#include "curses-funcs.h" /* is_scrollok() */
#endif

#include "cursesInit.h"
#include "dataClassify.h"
#include "irc.h"
#include "printtext.h"
#include "statusbar.h"
#include "strHand.h"
#include "strdup_printf.h"
#include "terminal.h"
#include "theme.h"

char g_user_modes[100] = "";

static PANEL *statusbar_pan = NULL;

static void
apply_statusbar_options(WINDOW *win)
{
    if (is_scrollok(win)) {
	(void) scrollok(win, false);
    }
}

static short int
get_pair_num()
{
    short int fg, bg;
    short int pair_n;

    fg = theme_color_unparse("statusbar_fg", COLOR_WHITE);
    bg = theme_color_unparse("statusbar_bg", COLOR_BLACK);

    if ((pair_n = color_pair_find(fg, bg)) != -1) {
	return pair_n;
    }

    return 0;
}

void
statusbar_init(void)
{
    statusbar_pan = term_new_panel(1, 0, LINES - 2, 0);
    apply_statusbar_options(panel_window(statusbar_pan));
}

void
statusbar_deinit(void)
{
    term_remove_panel(statusbar_pan);
}

static char *
get_nick_and_server()
{
    static char buf[500];

    BZERO(buf, sizeof buf);

    if (g_my_nickname && g_server_hostname) {
	(void) sw_strcpy(buf, g_my_nickname, sizeof buf);
	(void) sw_strcat(buf, "(", sizeof buf);
	(void) sw_strcat(buf,
	    g_user_modes[0] == ':' ? &g_user_modes[1] : &g_user_modes[0],
	    sizeof buf);
	(void) sw_strcat(buf, ")", sizeof buf);
	(void) sw_strcat(buf, "@", sizeof buf);
	(void) sw_strcat(buf, g_server_hostname, sizeof buf);
    }

    return (&buf[0]);
}

static char *
get_chanmodes()
{
    PIRC_WINDOW win;
    static char buf[500];

    BZERO(buf, sizeof buf);

    if ((win = g_active_window) != NULL) {
	if (Strings_match_ignore_case(win->label, g_status_window_label)) {
	    sw_strcpy(buf, Theme("slogan"), sizeof buf);
	} else if (is_irc_channel(win->label)) {
	    (void) sw_strcpy(buf, win->label, sizeof buf);
	    (void) sw_strcat(buf, "(", sizeof buf);
	    (void) sw_strcat(buf, win->chanmodes, sizeof buf);
	    (void) sw_strcat(buf, ")", sizeof buf);
	} else {
	    sw_strcpy(buf, win->label, sizeof buf);
	}
    }

    return (&buf[0]);
}

void
statusbar_update_display_beta(void)
{
#define WERASE(win)   ((void) werase(win))
#define WBKGD(win, c) ((void) wbkgd(win, c))
    WINDOW     *win    = panel_window(statusbar_pan);
    chtype      blank  = ' ';
    short int   pair_n = get_pair_num();
    const char *lb     = Theme("statusbar_leftBracket");
    const char *rb     = Theme("statusbar_rightBracket");
    char       *out_s  = Strdup_printf(
	"%s %s%d/%d%s %s%s%s %s%s%s %s",
	Theme("statusbar_spec"),
	lb, g_active_window->refnum, g_ntotal_windows, rb,
	lb, get_nick_and_server(), rb,
	lb, get_chanmodes(), rb,
	g_active_window->scroll_mode ? "-- MORE --" : "");

    WERASE(win);
    WBKGD(win, blank | COLOR_PAIR(pair_n) | A_NORMAL);

    printtext_puts(win, g_no_colors ? squeeze_text_deco(out_s) : out_s, -1, -1,
		   NULL);
    free(out_s);
    statusbar_show();
}

void
statusbar_recreate(int rows, int cols)
{
    struct term_window_size newsize = {
	.rows	   = 1,
	.cols	   = cols,
	.start_row = rows - 2,
	.start_col = 0,
    };

    statusbar_pan = term_resize_panel(statusbar_pan, &newsize);
    apply_statusbar_options(panel_window(statusbar_pan));
    statusbar_update_display_beta();
}

void
statusbar_show(void)
{
    if (statusbar_pan) {
	(void) show_panel(statusbar_pan);
    }
}

void
statusbar_hide(void)
{
    if (statusbar_pan) {
	(void) hide_panel(statusbar_pan);
    }
}
