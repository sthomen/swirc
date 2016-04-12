/* Handles event NOTICE
   Copyright (C) 2014, 2016 Markus Uhlin. All rights reserved.

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

#include "../dataClassify.h"
#include "../irc.h"
#include "../network.h"
#include "../printtext.h"
#include "../strHand.h"
#include "../theme.h"

#include "notice.h"

struct notice_context {
    char *srv_name;
    char *dest;
    char *msg;
};

static void
handle_notice_while_connecting(struct irc_message_compo *compo)
{
    struct printtext_context ctx = {
	.window     = g_status_window,
	.spec_type  = TYPE_SPEC_NONE,
	.include_ts = true,
    };
    const char	*msg      = strchr(compo->params, ':');
    const char	*srv_host = compo->prefix ? &compo->prefix[1] : "auth";

    if (msg == NULL || Strings_match(++msg, "")) {
	return;
    }

    printtext(&ctx, "%s!%s%c %s", Theme("color3"), srv_host, NORMAL, msg);
}

static void
handle_notice_from_my_server(const struct notice_context *ctx)
{
    struct printtext_context ptext_ctx = {
	.window     = g_status_window,
	.spec_type  = TYPE_SPEC_NONE,
	.include_ts = true,
    };

    if (g_my_nickname && Strings_match(ctx->dest, g_my_nickname))
	printtext(&ptext_ctx, "%s!%s%c %s", Theme("color3"), ctx->srv_name, NORMAL, ctx->msg);
}

/* event_notice

   Examples:
     :irc.server.com NOTICE <dest> :<msg>
     :<nick>!<user>@<host> NOTICE <dest> :<msg> */
void
event_notice(struct irc_message_compo *compo)
{
    char *dest, *msg;
    char *nick, *user, *host;
    char *params = &compo->params[0];
    char *prefix = compo->prefix ? &compo->prefix[0] : NULL;
    char *state1, *state2;
    struct printtext_context ptext_ctx;

    state1 = state2 = "";

    if (g_connection_in_progress || g_server_hostname == NULL) {
	handle_notice_while_connecting(compo);
	return;
    } else if (!prefix) { /* if this happens it's either the server or a bug (or both) */
	goto bad;
    } else if (Strfeed(params, 1) != 1) {
	goto bad;
    } else if ((dest = strtok_r(params, "\n", &state1)) == NULL ||
	       (msg = strtok_r(NULL, "\n", &state1)) == NULL) {
	goto bad;
    } else {
	if (*prefix == ':')
	    prefix++;
	if (*msg == ':')
	    msg++;
    }

    if (Strings_match(prefix, g_server_hostname)) {
	struct notice_context ctx = {
	    .srv_name = prefix,
	    .dest     = dest,
	    .msg      = msg,
	};

	handle_notice_from_my_server(&ctx);
	return;
    } else if (strchr(prefix, '!') == NULL || strchr(prefix, '@') == NULL) {
	goto bad;
    } else if ((nick = strtok_r(prefix, "!@", &state2)) == NULL
	       || (user = strtok_r(NULL, "!@", &state2)) == NULL
	       || (host = strtok_r(NULL, "!@", &state2)) == NULL) {
	goto bad;
    } else if (window_by_label(dest) != NULL && is_irc_channel(dest)) {
	ptext_ctx.window     = window_by_label(dest);
	ptext_ctx.spec_type  = TYPE_SPEC_NONE;
	ptext_ctx.include_ts = true;
	printtext(&ptext_ctx, "%s%s%s%c%s%s%s%c%s %s",
		  Theme("notice_lb"), Theme("notice_color1"), nick, NORMAL, Theme("notice_sep"),
		  Theme("notice_color2"), dest, NORMAL, Theme("notice_rb"), msg);
    } else {
	ptext_ctx.window     = window_by_label(dest) ? window_by_label(dest) : g_status_window;
	ptext_ctx.spec_type  = TYPE_SPEC_NONE;
	ptext_ctx.include_ts = true;
	printtext(&ptext_ctx, "%s%s%s%c%s%s%s@%s%c%s%s %s",
		  Theme("notice_lb"), Theme("notice_color1"), nick, NORMAL,
		  Theme("notice_inner_b1"), Theme("notice_color2"), user, host,
		  NORMAL, Theme("notice_inner_b2"), Theme("notice_rb"), msg);
    }

    return;

  bad:
    ptext_ctx.window     = g_status_window;
    ptext_ctx.spec_type  = TYPE_SPEC1_FAILURE;
    ptext_ctx.include_ts = true;
    printtext(&ptext_ctx, "On issuing event %s: An error occured", compo->command);
}