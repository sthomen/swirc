/* Input output loop
   Copyright (C) 2014-2016 Markus Uhlin. All rights reserved.

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

#include "assertAPI.h"
#include "config.h"
#include "cursesInit.h"
#include "dataClassify.h"
#include "errHand.h"
#include "io-loop.h"
#include "irc.h"
#include "main.h"
#include "nestHome.h"
#include "network.h"
#include "printtext.h"
#include "readline.h"
#include "strHand.h"
#include "strdup_printf.h"
#include "terminal.h"
#include "theme.h"

#include "commands/connect.h"
#include "commands/jp.h"
#include "commands/me.h"
#include "commands/misc.h"
#include "commands/msg.h"
#include "commands/nick.h"
#include "commands/say.h"
#include "commands/topic.h"

#include "events/names.h"

bool g_io_loop = true;

static struct cmds_tag {
    char		*cmd;
    CMD_HANDLER_FN	 fn;
} cmds[] = {
    { "/connect",        cmd_connect         },
    { "/disconnect",     cmd_disconnect      },
    { "/join",           cmd_join            },
    { "/me",             cmd_me              },
    { "/mode",           cmd_mode            },
    { "/msg",            cmd_msg             },
    { "/n",              cmd_names           },
    { "/nick",           cmd_nick            },
    { "/part",           cmd_part            },
    { "/query",          cmd_query           },
    { "/quit",           cmd_quit            },
    { "/say",            cmd_say             },
    { "/topic",          cmd_topic           },
    { "/whois",          cmd_whois           },
};

static void
swirc_greeting()
{
#define USE_LARRY3D_LOGO 0
    struct printtext_context ptext_ctx;
    const char **ppcc;
    const char *logo[] = {
#if USE_LARRY3D_LOGO
	"                     __                              ",
	"    ____  __  __  __/\\_\\  _ __   ___               ",
	"   /',__\\/\\ \\/\\ \\/\\ \\/\\ \\/\\`'__\\/'___\\   ",
	"  /\\__, `\\ \\ \\_/ \\_/ \\ \\ \\ \\ \\//\\ \\__/   ",
	"  \\/\\____/\\ \\___x___/'\\ \\_\\ \\_\\\\ \\____\\  ",
	"   \\/___/  \\/__//__/   \\/_/\\/_/ \\/____/         ",
#else
	" _______          _________ _______  _______     ",
	"(  ____ \\|\\     /|\\__   __/(  ____ )(  ____ \\",
	"| (    \\/| )   ( |   ) (   | (    )|| (    \\/  ",
	"| (_____ | | _ | |   | |   | (____)|| |          ",
	"(_____  )| |( )| |   | |   |     __)| |          ",
	"      ) || || || |   | |   | (\\ (   | |         ",
	"/\\____) || () () |___) (___| ) \\ \\__| (____/\\",
	"\\_______)(_______)\\_______/|/   \\__/(_______/ ",
#endif
    };
    const size_t logo_size = ARRAY_SIZE(logo);

    ptext_ctx.window     = g_status_window;
    ptext_ctx.spec_type  = TYPE_SPEC1;
    ptext_ctx.include_ts = true;

    for (ppcc = &logo[0]; ppcc < &logo[logo_size]; ppcc++) {
	const char *color = Theme("sw_ascLogotype_color");
	char       *str   = sw_strdup(*ppcc);

	printtext(&ptext_ctx, "%s%s", color, trim(str));
	free(str);
    }

    printtext(&ptext_ctx, "");

    printtext(&ptext_ctx, "    Swirc %s by %s", g_swircVersion, g_swircAuthor);
    printtext(&ptext_ctx, "    Compiled on %s%s %s%s", LEFT_BRKT, __DATE__, __TIME__, RIGHT_BRKT);

    if (g_initialized_pairs < 0) {
	g_initialized_pairs = 0;
    }

    printtext(&ptext_ctx, "");
    printtext(&ptext_ctx, "Program settings are stored in %s%s%s", LEFT_BRKT, g_home_dir, RIGHT_BRKT);
    printtext(&ptext_ctx, "%c%hd%c color pairs have been initialized", BOLD, g_initialized_pairs, BOLD);
    printtext(&ptext_ctx, "");
}

static void
auto_connect()
{
    struct network_connect_context conn_ctx = {
	.server   = g_cmdline_opts->server,
	.port     = g_cmdline_opts->port,
	.password = g_connection_password ? g_cmdline_opts->password : NULL,
	.username = "",
	.rl_name  = "",
	.nickname = "",
    };
    struct printtext_context ptext_ctx = {
	.window     = g_status_window,
	.spec_type  = TYPE_SPEC1_FAILURE,
	.include_ts = true,
    };

    if (g_cmdline_opts->username) {
	conn_ctx.username = g_cmdline_opts->username;
    } else if (Config_mod("username")) {
	conn_ctx.username = Config_mod("username");
    } else {
	printtext(&ptext_ctx, "Unable to connect: No username");
	return;
    }

    if (g_cmdline_opts->rl_name) {
	conn_ctx.rl_name = g_cmdline_opts->rl_name;
    } else if (Config_mod("real_name")) {
	conn_ctx.rl_name = Config_mod("real_name");
    } else {
	printtext(&ptext_ctx, "Unable to connect: No real name");
	return;
    }

    if (g_cmdline_opts->nickname) {
	conn_ctx.nickname = g_cmdline_opts->nickname;
    } else if (Config_mod("nickname")) {
	conn_ctx.nickname = Config_mod("nickname");
    } else {
	printtext(&ptext_ctx, "Unable to connect: No nickname");
	return;
    }

    if (!is_valid_username(conn_ctx.username)) {
	printtext(&ptext_ctx, "Unable to connect: Invalid username: \"%s\"", conn_ctx.username);
	return;
    } else if (!is_valid_real_name(conn_ctx.rl_name)) {
	printtext(&ptext_ctx, "Unable to connect: Invalid real name: \"%s\"", conn_ctx.rl_name);
	return;
    } else if (!is_valid_nickname(conn_ctx.nickname)) {
	printtext(&ptext_ctx, "Unable to connect: Invalid nickname: \"%s\"", conn_ctx.nickname);
	return;
    } else {
	net_connect(&conn_ctx);
    }
}

/* must be freed */
static char *
get_prompt()
{
    if (Strings_match_ignore_case(g_active_window->label, g_status_window_label)) {
	return (sw_strdup("> "));
    } else if (is_irc_channel(g_active_window->label)) {
	return (Strdup_printf("%s: ", g_active_window->label));
    } else {
	return (Strdup_printf("%s> ", g_active_window->label)); /* a query */
    }

    /*NOTREACHED*/
    sw_assert_not_reached();
    return (sw_strdup(""));
}

static void
handle_cmds(const char *data)
{
    struct cmds_tag *sp;
    const size_t ar_sz = ARRAY_SIZE(cmds);
    char *cp;

    for (sp = &cmds[0]; sp < &cmds[ar_sz]; sp++) {
	cp = Strdup_printf("%s ", sp->cmd);

	if (Strings_match(data, sp->cmd)) {
	    sp->fn("");
	    free(cp);
	    break;
	} else if (!strncmp( data, cp, strlen(cp) )) {
	    sp->fn(&data[strlen(cp)]);
	    free(cp);
	    break;
	} else {
	    free(cp);
	}
    }
}

static void
transmit_user_input(const char *input)
{
    struct printtext_context ctx = {
	.window     = g_active_window,
	.spec_type  = TYPE_SPEC_NONE,
	.include_ts = true,
    };

    if (net_send("PRIVMSG %s :%s", g_active_window->label, input) < 0) {
	g_on_air = false;
	return;
    }

    if (!is_irc_channel(g_active_window->label))
	printtext(&ctx, "%s%s%s %s", Theme("nick_s1"), g_my_nickname, Theme("nick_s2"), input);
    else {
	PNAMES	n = NULL;
	char	c = ' ';

	if ((n = event_names_htbl_lookup(g_my_nickname, g_active_window->label)) == NULL) {
	    err_log(0, "In transmit_user_input: hash table lookup error");
	    return;
	}

	if (n->is_owner)        c = '~';
	else if (n->is_superop) c = '&';
	else if (n->is_op)      c = '@';
	else if (n->is_halfop)  c = '%';
	else if (n->is_voice)   c = '+';
	else c = ' ';

	printtext(&ctx, "%s%c%s%s %s", Theme("nick_s1"), c, g_my_nickname, Theme("nick_s2"), input);
    }
}

void
enter_io_loop(void)
{
    new_window_title(g_status_window_label, "Swirc titlebar [tm]");

    if (config_bool_unparse("startup_greeting", true)) {
	swirc_greeting();
    }

    if (g_auto_connect) {
	auto_connect();
    }

    do {
	char *prompt, *line;
	const char cmd_char = '/';

	prompt = get_prompt();
	line   = readline(prompt);
	free(prompt);

	if (line == NULL) {
	    if (g_resize_requested) {
		term_resize_all();
	    }

	    continue;
	} else if (*line == cmd_char) {
	    handle_cmds(line);
	} else {
	    if (g_on_air && !Strings_match(g_active_window->label, g_status_window_label)) {
		struct printtext_context ptext_ctx = {
		    .window     = g_active_window,
		    .spec_type  = TYPE_SPEC_NONE,
		    .include_ts = true,
		};

		if (config_bool_unparse("recode", true)) {
		    ptext_ctx.spec_type = TYPE_SPEC1_FAILURE;
		    printtext(&ptext_ctx, "Can't recode user input before transmit (yet unsupported)");
		} else { /* don't recode... */
		    transmit_user_input(line);
		}
	    }
	}

	free(line);
    } while (g_io_loop);
}
