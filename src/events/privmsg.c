/* Handles event PRIVMSG
   Copyright (C) 2016, 2017 Markus Uhlin. All rights reserved.

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

#include "../errHand.h"
#include "../irc.h"
#include "../libUtils.h"
#include "../main.h"
#include "../network.h"
#include "../printtext.h"
#include "../strHand.h"
#include "../strdup_printf.h"
#include "../theme.h"

#include "names.h"
#include "privmsg.h"

struct special_msg_context {
    char *nick;
    char *user;
    char *host;
    char *dest;
    char *msg;
};

static void
acknowledge_ctcp_request(const char *cmd, const struct special_msg_context *ctx)
{
    struct printtext_context pt_ctx = {
	.window	    = g_active_window,
	.spec_type  = TYPE_SPEC3,
	.include_ts = true,
    };

    printtext(&pt_ctx, "%c%s%c %s%s@%s%s requested CTCP %c%s%c form %c%s%c",
	BOLD, ctx->nick, BOLD, LEFT_BRKT, ctx->user, ctx->host, RIGHT_BRKT,
	BOLD, cmd, BOLD, BOLD, ctx->dest, BOLD);
}

static void
handle_special_msg(const struct special_msg_context *ctx)
{
    char *msg = sw_strdup(ctx->msg);
    struct printtext_context pt_ctx;

    squeeze(msg, "\001");
    msg = trim(msg);

    if (Strings_match_ignore_case(ctx->dest, g_my_nickname)) {
	if (!strncmp(msg, "ACTION ", 7) &&
	    (pt_ctx.window = window_by_label(ctx->nick)) == NULL)
	    spawn_chat_window(ctx->nick, ctx->nick);
	pt_ctx.window = window_by_label(ctx->nick);
    } else {
	pt_ctx.window = window_by_label(ctx->dest);
    }

    if (! (pt_ctx.window))
	pt_ctx.window = g_active_window;
    pt_ctx.spec_type  = TYPE_SPEC_NONE;
    pt_ctx.include_ts = true;

    if (!strncmp(msg, "ACTION ", 7)) {
	printtext(&pt_ctx, " - %s %s", ctx->nick, &msg[7]);
    } else if (!strncmp(msg, "VERSION", 8)) {
	if (net_send("NOTICE %s :\001VERSION Swirc %s by %s\001",
		     ctx->nick, g_swircVersion, g_swircAuthor) < 0)
	    g_on_air = false;
	acknowledge_ctcp_request("VERSION", ctx);
    } else if (!strncmp(msg, "TIME", 5)) {
	if (net_send("NOTICE %s :\001TIME %s\001",
		     ctx->nick, current_time("%c")) < 0)
	    g_on_air = false;
	acknowledge_ctcp_request("TIME", ctx);
    } else {
	/* do nothing */;
    }

    free(msg);
}

static void
broadcast_window_activity(PIRC_WINDOW src)
{
    struct printtext_context ctx = {
	.window	    = g_active_window,
	.spec_type  = TYPE_SPEC1_SUCCESS,
	.include_ts = true,
    };

    if (src)
	printtext(&ctx, "activity at window %c%s%c (refnum: %d)",
		  BOLD, src->label, BOLD, src->refnum);
}

/* event_privmsg

   Examples:
     :<nick>!<user>@<host> PRIVMSG <dest> :<msg>
     :<nick>!<user>@<host> PRIVMSG <dest> :\001ACTION ...\001
     :<nick>!<user>@<host> PRIVMSG <dest> :\001VERSION\001 */
void
event_privmsg(struct irc_message_compo *compo)
{
    char	*dest, *msg;
    char	*nick, *user, *host;
    char	*params = &compo->params[0];
    char	*prefix = compo->prefix ? &compo->prefix[0] : NULL;
    char	*state1, *state2;
    struct printtext_context ctx = {
	.window	    = NULL,
	.spec_type  = TYPE_SPEC_NONE,
	.include_ts = true,
    };

    state1 = state2 = "";

    if (!prefix)
	return;
    if (*prefix == ':')
	prefix++;
    if ((nick = strtok_r(prefix, "!@", &state1)) == NULL)
	return;
    user = strtok_r(NULL, "!@", &state1);
    host = strtok_r(NULL, "!@", &state1);
    if (!user || !host) {
	user = "<no user>";
	host = "<no host>";
    }
    if (Strfeed(params, 1) != 1 ||
	(dest = strtok_r(params, "\n", &state2)) == NULL ||
	(msg = strtok_r(NULL, "\n", &state2)) == NULL)
	return;
    if (*msg == ':')
	msg++;
    if (*msg == '\001') {
	struct special_msg_context msg_ctx = {
	    .nick = nick,
	    .user = user,
	    .host = host,
	    .dest = dest,
	    .msg  = msg,
	};

	handle_special_msg(&msg_ctx);
	return;
    }
    if (Strings_match_ignore_case(dest, g_my_nickname)) {
	if (window_by_label(nick) == NULL &&
	    spawn_chat_window(nick, nick) != 0)
	    return;
    } else {
	if (window_by_label(dest) == NULL &&
	    spawn_chat_window(dest, "No title.") != 0)
	    return;
    }

    if (Strings_match_ignore_case(dest, g_my_nickname)) {
	if ((ctx.window = window_by_label(nick)) == NULL) {
	    err_log(0, "In event_privmsg: can't find a window with label %s",
		    nick);
	    return;
	}

	printtext(&ctx, "%s%s%s%c%s %s",
	    Theme("nick_s1"), COLOR2, nick, NORMAL, Theme("nick_s2"), msg);

	if (ctx.window != g_active_window)
	    broadcast_window_activity(ctx.window);
    } else {
	PNAMES	n = NULL;
	char	c = ' ';

	if ((ctx.window = window_by_label(dest)) == NULL ||
	    (n = event_names_htbl_lookup(nick, dest)) == NULL) {
	    err_log(0, "In event_privmsg: "
		"bogus window label / hash table lookup error");
	    return;
	}

	if (n->is_owner)        c = '~';
	else if (n->is_superop) c = '&';
	else if (n->is_op)      c = '@';
	else if (n->is_halfop)  c = '%';
	else if (n->is_voice)   c = '+';
	else c = ' ';

	char *s1 = Strdup_printf("%s:", g_my_nickname);
	char *s2 = Strdup_printf("%s,", g_my_nickname);
	char *s3 = Strdup_printf("%s ", g_my_nickname);

	if (!strncasecmp(msg, s1, strlen(s1)) ||
	    !strncasecmp(msg, s2, strlen(s2)) ||
	    !strncasecmp(msg, s3, strlen(s3)) ||
	    Strings_match_ignore_case(msg, g_my_nickname)) {
	    printtext(&ctx, "%s%c%s%s%c%s %s",
		Theme("nick_s1"), c, COLOR4, nick, NORMAL, Theme("nick_s2"),
		msg);
	    if (ctx.window != g_active_window)
		broadcast_window_activity(ctx.window);
	} else {
	    printtext(&ctx, "%s%c%s%s%c%s %s",
		Theme("nick_s1"), c, COLOR2, nick, NORMAL, Theme("nick_s2"),
		msg);
	}

	free(s1);
	free(s2);
	free(s3);
    }
}
