/* Prints and handles text
   Copyright (C) 2012-2018 Markus Uhlin. All rights reserved.

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

#include <locale.h>
#include <wctype.h>

#include "assertAPI.h"
#include "config.h"
#if defined(WIN32) && defined(PDC_EXP_EXTRAS)
#include "curses-funcs.h" /* is_scrollok() */
#endif
#include "cursesInit.h"
#include "dataClassify.h"
#include "errHand.h"
#include "libUtils.h"
#include "main.h"
#include "printtext.h"
#include "strHand.h"
#include "strdup_printf.h"
#include "terminal.h"
#include "theme.h"

#define WADDCH(win, c)        ((void) waddch(win, c))
#define WATTR_OFF(win, attrs) ((void) wattr_off(win, attrs, NULL))
#define WATTR_ON(win, attrs)  ((void) wattr_on(win, attrs, NULL))
#define WCOLOR_SET(win, cpn)  ((void) wcolor_set(win, cpn, NULL))

#define STRLEN_CAST(string) strlen((char *) string)

/* Structure definitions
   ===================== */

struct message_components {
    char *text;
    int   indent;
};

struct text_decoration_bools {
    bool is_blink;
    bool is_bold;
    bool is_color;
    bool is_reverse;
    bool is_underline;
};

struct case_default_context {
    WINDOW    *win;
    wchar_t    wc;
    int        nextchar_empty;
    int        indent;
    int        max_lines;
    ptrdiff_t  diff;
};

/* Objects with external linkage
   ============================= */

#if defined(UNIX)
pthread_mutex_t g_puts_mutex;
#elif defined(WIN32)
HANDLE g_puts_mutex;
#endif

/* Objects with internal linkage
   ============================= */

#if defined(UNIX)
static pthread_once_t  vprinttext_init_done = PTHREAD_ONCE_INIT;
static pthread_once_t  puts_init_done       = PTHREAD_ONCE_INIT;
static pthread_mutex_t vprinttext_mutex;
#elif defined(WIN32)
static init_once_t vprinttext_init_done = ONCE_INITIALIZER;
static init_once_t puts_init_done       = ONCE_INITIALIZER;
static HANDLE      vprinttext_mutex;
#endif

static struct ptext_colorMap_tag {
    short int color;
#if defined(UNIX)
    attr_t at;
#elif defined(WIN32)
    chtype at;
#endif
} ptext_colorMap[] = {
    { COLOR_WHITE,   A_BOLD },
    { COLOR_BLACK,   A_DIM  },
    { COLOR_BLUE,    A_DIM  },
    { COLOR_GREEN,   A_DIM  },
    { COLOR_RED,     A_BOLD },
    { COLOR_RED,     A_DIM  },
    { COLOR_MAGENTA, A_DIM  },
    { COLOR_YELLOW,  A_DIM  },
    { COLOR_YELLOW,  A_BOLD },
    { COLOR_GREEN,   A_BOLD },
    { COLOR_CYAN,    A_DIM  },
    { COLOR_CYAN,    A_BOLD },
    { COLOR_BLUE,    A_BOLD },
    { COLOR_MAGENTA, A_BOLD },
    { COLOR_BLACK,   A_BOLD },
    { COLOR_WHITE,   A_DIM  },
};

/**
 * Helper function for squeeze_text_deco().
 */
static SW_INLINE void
handle_foo_situation(char **buffer, long int *i, long int *j,
		     const char *reject)
{
    if (!(*buffer)[*i]) {
	return;
    } else if ((*buffer)[*i] == COLOR) {
	(*i)--;
    } else if (strchr(reject, (*buffer)[*i]) == NULL) {
	(*buffer)[(*j)++] = (*buffer)[*i];
    }
}

/**
 * Squeeze text-decoration from a buffer
 *
 * @param buffer Target buffer
 * @return The result
 */
char *
squeeze_text_deco(char *buffer)
{
    bool has_comma;
    char *reject;
    long int i, j;

    if (buffer == NULL) {
	err_exit(EINVAL, "squeeze_text_deco error");
    } else if (*buffer == '\0') {
	return (buffer);
    }

    reject = Strdup_printf(
	"%c%c%c%c%c", BLINK, BOLD, NORMAL, REVERSE, UNDERLINE);

    for (i = j = 0; buffer[i] != '\0'; i++) {
	switch (buffer[i]) {
	case COLOR:
	{
	    /*
	     * check for ^CN
	     */
	    if (!sw_isdigit(buffer[++i])) {
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    }

	    /*
	     * check for ^CNN or ^CN,
	     */
	    if (!sw_isdigit(buffer[++i]) && buffer[i] != ',') {
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    }

	    has_comma = buffer[i++] == ',';

	    /*
	     * check for ^CNN, or ^CN,N
	     */
	    if (!has_comma && buffer[i] == ',') {
		has_comma = true;
	    } else if (has_comma && sw_isdigit(buffer[i])) {
		/* ^CN,N */;
	    } else if (has_comma && !sw_isdigit(buffer[i])) {
		i--;
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    } else {
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    }

	    sw_assert(has_comma);

	    /*
	     * check for ^CNN,N or ^CN,NN
	     */
	    if (buffer[i] == ',') { /* ^CNN, */
		if (!sw_isdigit(buffer[++i])) {
		    i--;
		    handle_foo_situation(&buffer, &i, &j, reject);
		    break;
		}
	    } else { /* ^CN,N */
		sw_assert(sw_isdigit(buffer[i]));
		if (sw_isdigit(buffer[++i])) /* we have ^CN,NN? */
		    break;
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    }

	    /*
	     * check for ^CNN,NN
	     */
	    if (!sw_isdigit(buffer[++i])) {
		handle_foo_situation(&buffer, &i, &j, reject);
		break;
	    }

	    break;
	} /* case COLOR */
	default:
	    if (strchr(reject, buffer[i]) == NULL) {
		buffer[j++] = buffer[i];
	    }
	    break;
	} /* switch block */
    }

    free(reject);
    buffer[j] = '\0';

    return (buffer);
}

/**
 * Search for a color pair with given foreground/background.
 *
 * @param fg Foreground
 * @param bg Background
 * @return A color pair number, or -1 if not found.
 */
short int
color_pair_find(short int fg, short int bg)
{
    const short int gipp1 = g_initialized_pairs + 1;
    short int pnum; /* pair number */
    short int x, y;

    for (pnum = 1; pnum < gipp1; pnum++) {
	if (pair_content(pnum, &x, &y) == ERR) {
	    return -1;
	} else if (x == fg && y == bg) { /* found match */
	    return pnum;
	}
    }

    return -1;
}

/**
 * Print an error message to the active window and free a
 * char-pointer.
 *
 * @param msg Message
 * @param cp  Char-pointer
 * @return Void
 */
void
print_and_free(const char *msg, char *cp)
{
    struct printtext_context ptext_ctx = {
	.window	    = g_active_window,
	.spec_type  = TYPE_SPEC1_FAILURE,
	.include_ts = true,
    };

    printtext(&ptext_ctx, "%s", msg);
    free(cp);
}

/**
 * Swirc messenger
 *
 * @param ctx Context structure
 * @param fmt Format control
 * @return Void
 */
void
printtext(struct printtext_context *ctx, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprinttext(ctx, fmt, ap);
    va_end(ap);
}

/**
 * Create mutex "g_puts_mutex".
 */
static void
puts_mutex_init(void)
{
    mutex_new(&g_puts_mutex);
}

/**
 * WIN32 specific: attempt to convert multibyte character string to
 * wide-character string by using UTF-8. The storage is obtained with
 * xcalloc().
 *
 * @param buf Buffer to convert.
 * @return A wide-character string, or NULL on error.
 */
#if WIN32
static wchar_t *
windows_convert_to_utf8(const char *buf)
{
    const int sz = (int) (strlen(buf) + 1);
    wchar_t *out = xcalloc(sz, sizeof (wchar_t));

    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,buf,-1,out,sz) > 0)
	return out;
    free(out);
    return NULL;
}
#endif

/**
 * Attempt convert multibyte character string to wide-character string
 * by using a specific codeset. The storage is dynamically allocated.
 *
 * @param buf     Buffer to convert
 * @param codeset Codeset to use
 * @return A wide-character string, or NULL on error.
 */
static wchar_t *
try_convert_buf_with_cs(const char *buf, const char *codeset)
{
    char		*original_locale = NULL;
    char		*tmp_locale	 = NULL;
    const size_t	 CONVERT_FAILED	 = (size_t) -1;
    const size_t	 sz		 = strlen(buf) + 1;
    size_t		 bytes_convert	 = 0;
    struct locale_info	*li		 = get_locale_info(LC_CTYPE);
    wchar_t		*out		 = NULL;

    if (li->lang_and_territory == NULL || li->codeset == NULL)
	goto err;

    original_locale = Strdup_printf("%s.%s",
	li->lang_and_territory, li->codeset);
    tmp_locale      = Strdup_printf("%s.%s", li->lang_and_territory, codeset);
    out             = xcalloc(sz, sizeof (wchar_t));

    if (setlocale(LC_CTYPE, tmp_locale) == NULL ||
	(bytes_convert = xmbstowcs(out, buf, sz - 1)) == CONVERT_FAILED) {
	if (setlocale(LC_CTYPE, original_locale) == NULL)
	    err_log(EPERM, "In try_convert_buf_with_cs: "
		"cannot restore original locale (%s)", original_locale);
	goto err;
    }

    if (bytes_convert == sz - 1)
	out[sz - 1] = 0L;

    if (setlocale(LC_CTYPE, original_locale) == NULL)
	err_log(EPERM, "In try_convert_buf_with_cs: "
	    "cannot restore original locale (%s)", original_locale);
    free_locale_info(li);
    free_not_null(original_locale);
    free_not_null(tmp_locale);
    return out;

  err:
    free_locale_info(li);
    free_not_null(original_locale);
    free_not_null(tmp_locale);
    free_not_null(out);
    return NULL;
}

/**
 * Convert multibyte character string to wide-character string, using
 * different encodings. The storage is dynamically allocated.
 *
 * @param in_buf In buffer.
 * @return A wide-character string.
 */
static wchar_t *
perform_convert_buffer(const char **in_buf)
{
    bool chars_lost = false;
    const char *ar[] = {
#if defined(UNIX)
	"UTF-8",       "utf8",
	"ISO-8859-1",  "ISO8859-1",  "iso88591",
	"ISO-8859-15", "ISO8859-15", "iso885915",
#elif defined(WIN32)
	"65001", /* UTF-8 */
	"28591", /* ISO 8859-1 Latin 1 */
	"28605", /* ISO 8859-15 Latin 9 */
#endif
    };
    const size_t	 CONVERT_FAILED = (size_t) -1;
    const size_t	 ar_sz		= ARRAY_SIZE(ar);
    mbstate_t		 ps;
    size_t		 sz		= 0;
    wchar_t		*out		= NULL;

#if WIN32
    if ((out = windows_convert_to_utf8(*in_buf)) != NULL)
	return (out);
#endif

    for (const char **ar_p = &ar[0]; ar_p < &ar[ar_sz]; ar_p++) {
	if ((out = try_convert_buf_with_cs(*in_buf, *ar_p)) != NULL) /*success*/
	    return (out);
    }

    /* fallback solution... */
    sz  = strlen(*in_buf) + 1;
    out = xcalloc(sz, sizeof (wchar_t));

    BZERO(&ps, sizeof (mbstate_t));

    while (errno = 0, true) {
	if (mbsrtowcs(&out[wcslen(out)], in_buf, (sz - wcslen(out)) - 1, &ps) ==
	    CONVERT_FAILED && errno == EILSEQ) {
	    chars_lost = true;
	    (*in_buf)++;
	} else
	    break;
    }

    out[sz - 1] = 0L;
    if (chars_lost)
	err_log(EILSEQ, "In perform_convert_buffer: characters lost");
    return (out);
}

static void
append_newline(wchar_t **wc_buf)
{
    const size_t newsize = size_product(wcslen(*wc_buf) + sizeof "\n",
					sizeof (wchar_t));

    *wc_buf = xrealloc(*wc_buf, newsize);

    if ((errno = sw_wcscat(*wc_buf, L"\n", newsize)) != 0)
	err_sys("printtext: append_newline");
}

static void
replace_characters_with_space(wchar_t *wc_buf, const wchar_t *set)
{
    wchar_t *wcp = NULL;

    while ((wcp = wcspbrk(wc_buf, set)) != NULL)
	*wcp = L' ';
}

/**
 * Toggle blink ON/OFF. Don't actually use A_BLINK because it's
 * annoying.
 *
 * @param[in]     win      Target window.
 * @param[in,out] is_blink Is blink state.
 * @return Void
 */
static void
case_blink(WINDOW *win, bool *is_blink)
{
    if (!*is_blink) {
	WATTR_ON(win, A_REVERSE);
	*is_blink = true;
    } else {
	WATTR_OFF(win, A_REVERSE);
	*is_blink = false;
    }
}

/**
 * Toggle bold ON/OFF
 *
 * @param[in]     win     Target window
 * @param[in,out] is_bold Is bold state
 * @return Void
 */
static void
case_bold(WINDOW *win, bool *is_bold)
{
    if (!*is_bold) {
	WATTR_ON(win, A_BOLD);
	*is_bold = true;
    } else {
	WATTR_OFF(win, A_BOLD);
	*is_bold = false;
    }
}

/**
 * Convert a wide-character to a multibyte sequence. The storage for
 * the multibyte sequence is allocated on the heap and must be
 * free()'d.
 *
 * @param wc Wide-character
 * @return The result
 */
static unsigned char *
convert_wc(wchar_t wc)
{
    mbstate_t ps;
#ifdef HAVE_BCI
    size_t bytes_written; /* not used */
#endif
    const size_t size = MB_LEN_MAX + 1;
    unsigned char *mbs = xcalloc(size, 1);

    BZERO(&ps, sizeof (mbstate_t));

#ifdef HAVE_BCI
    if ((errno = wcrtomb_s(&bytes_written, (char *) mbs, size, wc, &ps)) != 0) {
	/* temporary error handling */
	err_log(errno, "In convert_wc: wcrtomb_s");
	return (mbs);
    }
#else
    if (wcrtomb((char *) mbs, wc, &ps) == ((size_t) -1)) {
	err_log(EILSEQ, "In convert_wc: wcrtomb");
	return (mbs);
    }
#endif

    return (mbs);
}

typedef enum {
    BUF_EOF,
    GO_ON,
    STOP_INTERPRETING
} cc_check_t;

/**
 * check for ^CN
 */
static cc_check_t
check_for_part1(wchar_t **bufp, char *fg)
{
    unsigned char *mbs = NULL;

    if (!*++(*bufp))
	return BUF_EOF;
    mbs = convert_wc(**bufp);
    if (STRLEN_CAST(mbs) != 1 || !sw_isdigit(*mbs)) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    }
    sw_snprintf(fg, 2, "%c", *mbs);
    free(mbs);
    return GO_ON;
}

/**
 * check for ^CNN or ^CN,
 */
static cc_check_t
check_for_part2(wchar_t **bufp, char *fg, bool *has_comma)
{
    unsigned char *mbs = NULL;

    if (!*++(*bufp))
	return BUF_EOF;
    mbs = convert_wc(**bufp);
    if (STRLEN_CAST(mbs) != 1 || (!sw_isdigit(*mbs) && *mbs != ',')) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    }
    if (sw_isdigit(*mbs))
	sw_snprintf(fg, 2, "%c", *mbs);
    else if (*mbs == ',')
	*has_comma = true;
    else
	sw_assert_not_reached();
    free(mbs);
    return GO_ON;
}

/**
 * check for ^CNN, or ^CN,N
 */
static cc_check_t
check_for_part3(wchar_t **bufp, bool *has_comma, bool fg_complete, char *bg)
{
    unsigned char *mbs = NULL;

    if (!*++(*bufp))
	return BUF_EOF;
    mbs = convert_wc(**bufp);
    if (STRLEN_CAST(mbs) != 1 || (*mbs != ',' && !sw_isdigit(*mbs))) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    } else if (*mbs == ',' && *has_comma) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    } else if (*mbs != ',' && fg_complete) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    }

    if (*mbs == ',')
	*has_comma = true;
    else if (sw_isdigit(*mbs))
	sw_snprintf(bg, 2, "%c", *mbs);
    else
	sw_assert_not_reached();

    free(mbs);
    return GO_ON;
}

/**
 * check for ^CNN,N or ^CN,NN
 */
static cc_check_t
check_for_part4(wchar_t **bufp, bool got_digit_bg, char *bg)
{
    unsigned char *mbs = NULL;

    if (!*++(*bufp))
	return BUF_EOF;
    mbs = convert_wc(**bufp);
    if (STRLEN_CAST(mbs) != 1 || !sw_isdigit(*mbs)) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    } else if (got_digit_bg) {
	sw_snprintf(++bg, 2, "%c", *mbs);
	free(mbs);
	return STOP_INTERPRETING;
    }
    sw_snprintf(bg, 2, "%c", *mbs);
    free(mbs);
    return GO_ON;
}

/**
 * check for ^CNN,NN
 */
static cc_check_t
check_for_part5(wchar_t **bufp, char *bg)
{
    unsigned char *mbs = NULL;

    if (!*++(*bufp))
	return BUF_EOF;
    mbs = convert_wc(**bufp);
    if (STRLEN_CAST(mbs) != 1 || !sw_isdigit(*mbs)) {
	(*bufp)--;
	free(mbs);
	return STOP_INTERPRETING;
    }
    sw_snprintf(bg, 2, "%c", *mbs);
    free(mbs);
    return GO_ON;
}

/**
 * Set color for output in a window.
 *
 * @param[in]  win      Window
 * @param[out] is_color Is color state
 * @param[in]  num1     Number for foreground
 * @param[in]  num2     Number for background
 * @return Void
 */
static void
printtext_set_color(WINDOW *win, bool *is_color, short int num1, short int num2)
{
    const short int num_colorMap_entries =
	(short int) ARRAY_SIZE(ptext_colorMap);
    short int fg, bg, resolved_pair;
#if defined(UNIX)
    attr_t attr;
#elif defined(WIN32)
    chtype attr;
#endif

    /* num1 shouldn't under any circumstances appear negative */
    sw_assert(num1 >= 0);

    fg = ptext_colorMap[num1 % num_colorMap_entries].color;
    bg = (num2 < 0 ? -1 : ptext_colorMap[num2 % num_colorMap_entries].color);

    if ((resolved_pair = color_pair_find(fg, bg)) == -1) {
	WCOLOR_SET(win, 0);
	*is_color = false;
	return;
    }

    attr = ptext_colorMap[num1 % num_colorMap_entries].at; /*attributes of fg*/
    attr |= COLOR_PAIR(resolved_pair);
    term_set_attr(win, attr);
    *is_color = true;
}

/**
 * Handle and interpret color codes.
 *
 * @param win      Window
 * @param is_color Is color state
 * @param bufp     Buffer pointer
 * @return Void
 */
static void
case_color(WINDOW *win, bool *is_color, wchar_t **bufp)
{
    bool           has_comma = false;
    char           bg[10]    = { 0 };
    char           fg[10]    = { 0 };
    short int      num1      = -1;
    short int      num2      = -1;
    struct integer_unparse_context unparse_ctx = {
	.setting_name	  = "term_background",
	.fallback_default = 1,	/* black */
	.lo_limit	  = 0,
	.hi_limit	  = 15,
    };

    if (*is_color) {
	WCOLOR_SET(win, 0);
	*is_color = false;
    }

/***************************************************
 *
 * check for ^CN
 *
 ***************************************************/
    switch (check_for_part1(bufp, &fg[0])) {
    case BUF_EOF:
    case STOP_INTERPRETING:
	return;
    case GO_ON:
    default:
	break;
    }

/***************************************************
 *
 * check for ^CNN or ^CN,
 *
 ***************************************************/
    switch (check_for_part2(bufp, &fg[1], &has_comma)) {
    case BUF_EOF:
	return;
    case STOP_INTERPRETING:
	goto out;
    case GO_ON:
    default:
	break;
    }

/***************************************************
 *
 * check for ^CNN, or ^CN,N
 *
 ***************************************************/
    switch (check_for_part3(bufp, &has_comma, fg[1] != '\0', &bg[0])) {
    case BUF_EOF:
	return;
    case STOP_INTERPRETING:
	goto out;
    case GO_ON:
    default:
	break;
    }

/***************************************************
 *
 * check for ^CNN,N or ^CN,NN
 *
 ***************************************************/
    switch (check_for_part4(bufp, bg[0] != '\0', &bg[0])) {
    case BUF_EOF:
	return;
    case STOP_INTERPRETING:
	goto out;
    case GO_ON:
    default:
	break;
    }

/***************************************************
 *
 * check for ^CNN,NN
 *
 ***************************************************/
    switch (check_for_part5(bufp, &bg[1])) {
    case BUF_EOF:
	return;
    case STOP_INTERPRETING:
	goto out;
    case GO_ON:
    default:
	break;
    }

  out:
    num1 = (short int) atoi(fg);
    if (!isEmpty(bg)) {
	num2 = (short int) atoi(bg);
    } else if (isEmpty(bg) &&
	       theme_bool_unparse("term_use_default_colors", true)) {
	num2 = -1;
    } else {
	num2 = (short int) theme_integer_unparse(&unparse_ctx);
    }

    printtext_set_color(win, is_color, num1, num2);

    if (has_comma && !(bg[0]))
	(*bufp)--;
}

/**
 * Reset text-decoration bools
 *
 * @param booleans Context structure
 * @return Void
 */
static void
text_decoration_bools_reset(struct text_decoration_bools *booleans)
{
    booleans->is_blink     = false;
    booleans->is_bold      = false;
    booleans->is_color     = false;
    booleans->is_reverse   = false;
    booleans->is_underline = false;
}

/**
 * Toggle reverse ON/OFF
 *
 * @param[in]     win        Target window
 * @param[in,out] is_reverse Is reverse state
 * @return Void
 */
static void
case_reverse(WINDOW *win, bool *is_reverse)
{
    if (!*is_reverse) {
	WATTR_ON(win, A_REVERSE);
	*is_reverse = true;
    } else {
	WATTR_OFF(win, A_REVERSE);
	*is_reverse = false;
    }
}

/**
 * Toggle underline ON/OFF
 *
 * @param[in]     win          Target window
 * @param[in,out] is_underline Is underline state
 * @return Void
 */
static void
case_underline(WINDOW *win, bool *is_underline)
{
    if (!*is_underline) {
	WATTR_ON(win, A_UNDERLINE);
	*is_underline = true;
    } else {
	WATTR_OFF(win, A_UNDERLINE);
	*is_underline = false;
    }
}

/**
 * Do indent
 */
static void
do_indent(WINDOW *win, const int indent, int *insert_count)
{
#if defined(UNIX)
    attr_t attrs;
#elif defined(WIN32)
    chtype attrs;
#endif
    const chtype blank = ' ';
    int counter = 0;

    /* turn off all attributes during indentation */
    attrs = win->_attrs;
    term_set_attr(win, A_NORMAL);

    while (counter++ != indent) {
	WADDCH(win, blank);
	(*insert_count)++;
    }

    /* restore attributes after indenting */
    term_set_attr(win, attrs);
}

/**
 * Start on a new row?
 */
static bool
start_on_a_new_row(const long int sum)
{
    return (sum < (COLS - 1) ? false : true);
}

/**
 * Handles switch default in printtext_puts()
 *
 * @param[in]     ctx          Context structure
 * @param[in,out] rep_count    "Represent" count
 * @param[out]    line_count   Line count
 * @param[out]    insert_count Insert count
 * @return Void
 */
static void
case_default(struct case_default_context *ctx,
	     int *rep_count, int *line_count, int *insert_count)
{
    chtype c;
    unsigned char *mbs, *p;

    if (!iswprint(ctx->wc) && ctx->wc != L'\n') {
	return;
    }

    mbs = convert_wc(ctx->wc);
    p = &mbs[0];

    if (is_scrollok(ctx->win)) {
	const chtype new_line = '\n';

	if (ctx->wc == L'\n') {
	    WADDCH(ctx->win, new_line);
	    *insert_count = 0;

	    if (rep_count != NULL) {
		(*rep_count)++;
	    }

	    if (ctx->max_lines > 0) {
		if (!( ++(*line_count) < ctx->max_lines )) {
		    free(mbs);
		    return;
		}
	    }

	    if (!ctx->nextchar_empty && ctx->indent > 0) {
		do_indent(ctx->win, ctx->indent, insert_count);
	    }
	} else if (!start_on_a_new_row((*insert_count) + ctx->diff + 1)) {
	    while ((c = *p++) != '\0') {
		WADDCH(ctx->win, c);
	    }

	    (*insert_count)++;
	} else {
	    WADDCH(ctx->win, new_line);
	    *insert_count = 0;

	    if (rep_count != NULL) {
		(*rep_count)++;
	    }

	    if (ctx->max_lines > 0) {
		if (!( ++(*line_count) < ctx->max_lines )) {
		    free(mbs);
		    return;
		}
	    }

	    if (ctx->indent > 0) {
		do_indent(ctx->win, ctx->indent, insert_count);
	    }

#if 1
	    if (ctx->diff && ctx->wc == L' ') {
		free(mbs);
		return;
	    }
#endif

	    while ((c = *p++) != '\0') {
		WADDCH(ctx->win, c);
	    }

	    (*insert_count)++;
	}
    } else { /* not scrollok */
	while ((c = *p++) != '\0') {
	    WADDCH(ctx->win, c);
	}
    }

    free(mbs);
}

/**
 * Output data to window
 *
 * @param[in]  pwin      Panel window where the output is to be displayed.
 * @param[in]  buf       A buffer that should contain the data to be written to
 *                       'pwin'.
 * @param[in]  indent    If >0 indent text with this number of blanks.
 * @param[in]  max_lines If >0 write at most this number of lines.
 * @param[out] rep_count "Represent count". How many actual lines does this
 *                       contribution represent in the output window?
 *                       (Passing NULL is ok.)
 * @return Void
 */
void
printtext_puts(WINDOW *pwin, const char *buf, int indent, int max_lines,
	       int *rep_count)
{
    const bool pwin_scrollable = is_scrollok(pwin);
    int insert_count = 0;
    int line_count = 0;
    int max_lines_flagged = 0;
    struct text_decoration_bools booleans = {
	.is_blink     = false,
	.is_bold      = false,
	.is_color     = false,
	.is_reverse   = false,
	.is_underline = false,
    };
    wchar_t *wc_buf = NULL, *wc_bufp = NULL;

#if defined(UNIX)
    if ((errno = pthread_once(&puts_init_done, puts_mutex_init)) != 0) {
	err_sys("pthread_once error");
    }
#elif defined(WIN32)
    if ((errno = init_once(&puts_init_done, puts_mutex_init)) != 0) {
	err_sys("init_once error");
    }
#endif

    if (rep_count) {
	*rep_count = 0;
    }

    if (!buf) {
	err_exit(EINVAL, "printtext_puts error");
    } else if (*buf == '\0') {
	return;
    }

    mutex_lock(&g_puts_mutex);
    wc_buf = perform_convert_buffer(&buf);
    if (pwin_scrollable)
	append_newline(&wc_buf);
    replace_characters_with_space(wc_buf, L"\f\t\v");

    for (wc_bufp = &wc_buf[0], max_lines_flagged = 0;
	 *wc_bufp && !max_lines_flagged;
	 wc_bufp++) {
	wchar_t wc = *wc_bufp;

	switch (wc) {
	case BLINK:
	    case_blink(pwin, &booleans.is_blink);
	    break;
	case BOLD:
	    case_bold(pwin, &booleans.is_bold);
	    break;
	case COLOR:
	    case_color(pwin, &booleans.is_color, &wc_bufp);
	    break;
	case NORMAL:
	    text_decoration_bools_reset(&booleans);
	    term_set_attr(pwin, A_NORMAL);
	    break;
	case REVERSE:
	    case_reverse(pwin, &booleans.is_reverse);
	    break;
	case UNDERLINE:
	    case_underline(pwin, &booleans.is_underline);
	    break;
	default:
	{
	    ptrdiff_t diff = 0;
	    wchar_t *wcp = NULL;

	    if (wc == L' ' && (wcp = wcschr(wc_bufp + 1, L' ')) != NULL) {
		diff = wcp - wc_bufp;
	    }

	    struct case_default_context def_ctx = {
		.win		= pwin,
		.wc		= wc,
		.nextchar_empty = !wcscmp(wc_bufp + 1, L""),
		.indent		= indent,
		.max_lines	= max_lines,
		.diff		= diff,
	    };

	    case_default(&def_ctx, rep_count, &line_count, &insert_count);
	    break;
	} /* case default */
	} /* switch block */
	if (pwin_scrollable && max_lines > 0) {
	    max_lines_flagged = line_count >= max_lines;
	}
    }

    free(wc_buf);
    term_set_attr(pwin, A_NORMAL);
    update_panels();
    doupdate();
    mutex_unlock(&g_puts_mutex);
}

/**
 * Print formatted output in Curses windows
 *
 * @param win Window
 * @param fmt Format control
 * @return Void
 */
void
swirc_wprintw(WINDOW *win, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vwprintw(win, fmt, ap);
    va_end(ap);
}

/**
 * Create mutex "vprinttext_mutex".
 */
static void
vprinttext_mutex_init(void)
{
    mutex_new(&vprinttext_mutex);
}

/**
 * Get multibyte string length
 */
static size_t
get_mb_strlen(const char *s)
{
    const size_t ERR_CASE1 = (size_t) -1;
    const size_t ERR_CASE2 = (size_t) -2;
    size_t idx = 0;
    size_t len = 0;

    while (true) {
	const size_t ret = mbrlen(&s[idx], MB_CUR_MAX, NULL);

	if (ret == ERR_CASE1 || ret == ERR_CASE2) {
	    return (strlen(s));
	} else if (ret == 0) {
	    break;
	} else {
	    idx += ret;
	    len += 1;
	}
    }

    return (len);
}

/**
 * Get message components
 *
 * @param unproc_msg Unprocessed message
 * @param spec_type  "Specifier"
 * @param include_ts Include timestamp?
 * @return Message components
 */
static struct message_components *
get_processed_out_message(const char *unproc_msg,
			  enum message_specifier_type spec_type,
			  bool include_ts)
{
#define STRLEN_SQUEEZE(string) ((int) get_mb_strlen(squeeze_text_deco(string)))
    struct message_components *pout = xcalloc(sizeof *pout, 1);
    char *tmp = NULL;

    pout->text = NULL;
    pout->indent = 0;

    if (include_ts) {
	char *ts = sw_strdup(current_time(Theme("time_format")));

	switch (spec_type) {
	case TYPE_SPEC1:
	    pout->text	 = Strdup_printf("%s %s %s", ts, THE_SPEC1, unproc_msg);
	    tmp		 = Strdup_printf("%s %s ", ts, THE_SPEC1);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC2:
	    pout->text	 = Strdup_printf("%s %s %s", ts, THE_SPEC2, unproc_msg);
	    tmp		 = Strdup_printf("%s %s ", ts, THE_SPEC2);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC3:
	    pout->text	 = Strdup_printf("%s %s %s", ts, THE_SPEC3, unproc_msg);
	    tmp		 = Strdup_printf("%s %s ", ts, THE_SPEC3);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_SPEC2:
	    pout->text = Strdup_printf("%s %s %s %s", ts, THE_SPEC1,
				       THE_SPEC2, unproc_msg);
	    tmp = Strdup_printf("%s %s %s ", ts, THE_SPEC1, THE_SPEC2);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_FAILURE:
	    pout->text = Strdup_printf("%s %s %s %s", ts, THE_SPEC1,
				       GFX_FAILURE, unproc_msg);
	    tmp = Strdup_printf("%s %s %s ", ts, THE_SPEC1, GFX_FAILURE);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_SUCCESS:
	    pout->text = Strdup_printf("%s %s %s %s", ts, THE_SPEC1,
				       GFX_SUCCESS, unproc_msg);
	    tmp = Strdup_printf("%s %s %s ", ts, THE_SPEC1, GFX_SUCCESS);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_WARN:
	    pout->text = Strdup_printf("%s %s %s %s", ts, THE_SPEC1,
				       GFX_WARN, unproc_msg);
	    tmp = Strdup_printf("%s %s %s ", ts, THE_SPEC1, GFX_WARN);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC_NONE: default:
	    pout->text	 = Strdup_printf("%s %s", ts, unproc_msg);
	    tmp		 = Strdup_printf("%s ", ts);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	}

	free(ts);
    } else if (!include_ts) { /* the same but no timestamp */
	switch (spec_type) {
	case TYPE_SPEC1:
	    pout->text   = Strdup_printf("%s %s", THE_SPEC1, unproc_msg);
	    tmp          = Strdup_printf("%s ", THE_SPEC1);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC2:
	    pout->text   = Strdup_printf("%s %s", THE_SPEC2, unproc_msg);
	    tmp          = Strdup_printf("%s ", THE_SPEC2);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC3:
	    pout->text   = Strdup_printf("%s %s", THE_SPEC3, unproc_msg);
	    tmp          = Strdup_printf("%s ", THE_SPEC3);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_SPEC2:
	    pout->text   = Strdup_printf("%s %s %s", THE_SPEC1, THE_SPEC2,
					 unproc_msg);
	    tmp          = Strdup_printf("%s %s ", THE_SPEC1, THE_SPEC2);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_FAILURE:
	    pout->text   = Strdup_printf("%s %s %s", THE_SPEC1, GFX_FAILURE,
					 unproc_msg);
	    tmp          = Strdup_printf("%s %s ", THE_SPEC1, GFX_FAILURE);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_SUCCESS:
	    pout->text   = Strdup_printf("%s %s %s", THE_SPEC1, GFX_SUCCESS,
					 unproc_msg);
	    tmp          = Strdup_printf("%s %s ", THE_SPEC1, GFX_SUCCESS);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC1_WARN:
	    pout->text   = Strdup_printf("%s %s %s", THE_SPEC1, GFX_WARN,
					 unproc_msg);
	    tmp          = Strdup_printf("%s %s ", THE_SPEC1, GFX_WARN);
	    pout->indent = STRLEN_SQUEEZE(tmp);
	    break;
	case TYPE_SPEC_NONE: default:
	    pout->text   = sw_strdup(unproc_msg);
	    pout->indent = 0;
	    break;
	}
    } else {
	sw_assert_not_reached();
    }

    free_not_null(tmp);
    sw_assert(pout->text != NULL);

    if (g_no_colors) {
	pout->text = squeeze_text_deco(pout->text);
    }

    return (pout);
}

/**
 * Variable argument list version of Swirc messenger
 *
 * @param ctx Context structure
 * @param fmt Format
 * @param ap  va_list object
 * @return Void
 */
void
vprinttext(struct printtext_context *ctx, const char *fmt, va_list ap)
{
    char *fmt_copy = NULL;
    const int tbszp1 = textBuf_size(ctx->window->buf) + 1;
    struct integer_unparse_context unparse_ctx = {
	.setting_name     = "textbuffer_size_absolute",
	.fallback_default = 1000,
	.lo_limit         = 350,
	.hi_limit         = 4700,
    };
    struct message_components *pout = NULL;

#if defined(UNIX)
    errno = pthread_once(&vprinttext_init_done, vprinttext_mutex_init);
    if (errno)
	err_sys("pthread_once");
#elif defined(WIN32)
    errno = init_once(&vprinttext_init_done, vprinttext_mutex_init);
    if (errno)
	err_sys("init_once");
#endif

    mutex_lock(&vprinttext_mutex);

    fmt_copy = Strdup_vprintf(fmt, ap);
    pout     = get_processed_out_message(fmt_copy, ctx->spec_type,
					 ctx->include_ts);

    if (tbszp1 > config_integer_unparse(&unparse_ctx)) {
	/* Buffer full. Remove head... */

	if ((errno = textBuf_remove(ctx->window->buf,
	    textBuf_head(ctx->window->buf))) != 0)
	    err_sys("textBuf_remove");
    }

    if (textBuf_size(ctx->window->buf) == 0) {
	if ((errno = textBuf_ins_next(ctx->window->buf, NULL, pout->text,
	    pout->indent)) != 0)
	    err_sys("textBuf_ins_next");
    } else {
	if ((errno = textBuf_ins_next(ctx->window->buf,
	    textBuf_tail(ctx->window->buf), pout->text, pout->indent)) != 0)
	    err_sys("textBuf_ins_next");
    }

    if (! (ctx->window->scroll_mode))
	printtext_puts(panel_window(ctx->window->pan), pout->text, pout->indent,
		       -1, NULL);

    free_not_null(fmt_copy);
    free_not_null(pout->text);
    free_not_null(pout);

    mutex_unlock(&vprinttext_mutex);
}
