#ifndef READLINE_H
#define READLINE_H

#include <setjmp.h> /* want type jmp_buf */

enum { /* custom, additional keys */
    MY_KEY_BS	  = '\010',	/* ^H */
    MY_KEY_DEL	  = '\177',	/* ^? */
    MY_KEY_EOT	  = '\004',	/* ^D */
    MY_KEY_ACK	  = '\006',	/* ^F */
    MY_KEY_STX	  = '\002',	/* ^B */
    MY_KEY_SO	  = '\016',	/* ^N */
    MY_KEY_DLE	  = '\020',	/* ^P */
    MY_KEY_RESIZE = '\033'
};

struct readline_session_context {
    wchar_t *buffer;
    int      bufpos;
    int      n_insert;
    bool     insert_mode;
    bool     no_bufspc;
    char    *prompt;
    int      prompt_size;
    WINDOW  *act;
};

extern jmp_buf g_readline_loc_info;
extern bool    g_readline_loop;
extern bool    g_resize_requested;

/*lint -sem(readline, r_null) */

char	*readline           (const char *prompt);
void	 readline_deinit    (void);
void	 readline_init      (void);
void	 readline_recreate  (int rows, int cols);
void	 readline_top_panel (void);

#endif