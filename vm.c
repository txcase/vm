#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/limits.h>

#include "term.h"

#define VM_VERSION     "0.05"

#define UCHAR          unsigned char
#define UINT           unsigned int

#define DEVTTY         "/dev/tty"
#define CHUNK          50
#define DECIMICAL      10

#define K_BACKSPACE    127
#define K_ENTER        10
#define K_ESC          27

#ifndef CTRL
#define CTRL(C)        ( (C) & 037 )
#endif

#define IOCTL_WINSIZE  TIOCGWINSZ
#define INPUT_MAX      (1 << 10)

typedef struct
{
  const char  *pattern;
  const char  *pos;
  UINT        current;
} SEARCH;

typedef struct 
{
  struct winsize   cwsize;    /* current winsize */ 
  SEARCH           search;    /* for search */
  FILE *           tty;
  const char *     filename;
  UINT             last;      /* last line */
  UINT             y;         /* y position */
  int              run;       /* is running? */
  int              c;         /* last key */
} context;

static int     vm_init(context *ctx, const char *src);
static void    vm(const char *src, context *ctx, int recursive);
static void    prange(const char *txt, const size_t ln_from, const size_t ln_to, context *ctx);
static void    status(UINT rows, const char *msg_format, ...);
static long    xstrtol(const char *str, int base);
static char *  xstrcasestr(const char *haystack, const char *needle);
static int     xstrncasecmp(const char *str1, const char *str2, const size_t n);
static char *  input(context *ctx, const char *prompt);
static char *  fview(const char *path);
static UINT    lncount(const char *src);

static struct termios old_term;

/* you can also create a file and paste this into it and then open it */
static const char *vm_usage =
"vm - text viewer\n\
usage: vm -[hHv] file\n\
   -v     print version\n\
   -h/H   print usage\n\n\
interactive mode usage:\n\
\'^\' is Control\n\n\
q     quit\n\
j     y position += 1\n\
k     y position -= 1\n\
^D    y position += win_height\n\
^U    y position -= win_height\n\
g     go to last line\n\
G     go to first line\n\
h     help message\n\
/     search (the search is not case sensitive)\n\
  n     next\n\
t     to N line\n\
";

static int
getwsz(struct winsize *wsz)
{
  if (!wsz)
    return 0;
  return ioctl(STDOUT_FILENO, IOCTL_WINSIZE, wsz);
}

static int 
xstrncasecmp(const char *str1, const char *str2, const size_t n)
{
  UCHAR c1;
  UCHAR c2;

  size_t i;
  for (i = 0; i < n; i++)
  {
    c1 = (UCHAR) tolower( (UCHAR)str1[i] );
    c2 = (UCHAR) tolower( (UCHAR) str2[i]);

    if (!c1 || !c2 || c1 != c2)
      break;
  }
  return c1 - c2;
}

static char *
xstrcasestr(const char *haystack, const char *needle)
{
  const UCHAR *h;
  const UCHAR *n;

  h = NULL;
  n = NULL;

  for (; *haystack; haystack++)
  {
    h = (const UCHAR *) haystack;
    n = (const UCHAR *) needle;

    while (*h && *n && (UCHAR) tolower(*n) == (UCHAR) tolower(*h))
    {
      h++;
      n++;
    }

    if (*n == '\0')
      return (char *) haystack;
  }

  return NULL;
}

static long int
xstrtol(const char *str, const int base)
{
  long int num;
  char *end;

  num = strtol(str, &end, base);

  if (str == end || *end != '\0')
    return 0;

  return num;
}

static char *
input(context *ctx, const char *prompt)
{
  struct    termios input_term;
  char      *buf;
  char      *tmp;
  size_t    cap;
  ssize_t   xpos;
  int       c;

  buf = NULL;
  tmp = NULL;
  xpos = 0;
  cap = 0;
  c = 0;

  if (ctx->tty && isatty(fileno(ctx->tty)))
  {
    if (getraw(fileno(ctx->tty), &input_term) == -1)
      return NULL;
  }

  mvcurs(ctx->cwsize.ws_row, 1);

  fputs("\033[2K", stdout); 
  fputs(prompt, stdout);

  while (xpos < MAX_INPUT - 1)
  {
    if (cap == xpos)
    {
      if (!cap)
        cap = 1;

      cap *= 2;

      tmp = realloc(buf, cap);

      if (!tmp)
      {
        free(buf);
        return NULL;
      }
      buf = tmp;
    }

    c = fgetc(ctx->tty);

    if (c == '\n')
    {
      if (xpos > 0)
        break;
      continue;
    }

    if (c == EOF)
      break;

    if (c == K_BACKSPACE && xpos > 0)
    {
      xpos--;
      fputs("\033[D \033[D", stdout);
    }
    else
    {
      buf[xpos] = c;
      fputc(c, stdout);
      xpos++;
    }
    mvcurs(ctx->cwsize.ws_row, xpos + strlen(prompt) + 1);
  }

  if (setterm(fileno(ctx->tty), &input_term) == -1)
  {
    free(buf);
    return NULL;
  }

  buf[xpos] = 0;
  return buf;
}

static long int 
dinput(context *ctx)
{
  long int   n;
  char      *str;

  str = input(ctx, "to line:");

  if (!str)
    return 0;

  n = xstrtol(str, DECIMICAL);
  free(str);

  return n;
}

static void
status(UINT rows, const char *msg_format, ...)
{
  printf("\033[%d;1H", rows); 
  fputs("\033[2K", stdout); 

  va_list args;
  va_start(args, msg_format);

  vprintf(msg_format, args);

  va_end(args);

  fflush(stdout); 
}

static UINT
calcpos(const context *ctx, const UINT line, const long offset)
{
  long    tag;
  long    new_pos;
  UINT    bot;

  bot = (ctx->last < ctx->cwsize.ws_row) ? 0 : ctx->last - ctx->cwsize.ws_row;

  tag = (long) line;

  new_pos = tag + offset;

  if (new_pos < 0)
    new_pos = 0;

  if (new_pos > bot)
      new_pos = bot;

  return (UINT ) new_pos;
}

static void
search_next(context *ctx, const char *src)
{
  const char   *ptr;

  ptr = xstrcasestr(src, ctx->search.pattern);

  if (!ptr)
    return;

  ctx->y = calcpos(ctx, ctx->last - lncount(ptr), -(ctx->cwsize.ws_row / 2));
  ctx->search.pos = ptr + strlen(ctx->search.pattern);
  ctx->search.current++;
}

static UINT 
lncount(const char *src)
{
  if (!src)
    return 0;

  size_t c_ln;
 
  c_ln = 0;

  for (size_t i = 0; src[i]; i++)
    if (src[i] == '\n') c_ln++;

  return c_ln;
}

static char * 
fview(const char *path)
{
  FILE      *fd; 
  int       c;
  char      *tmp,    *buffer;
  size_t    cap,      bt_read;
  
  fd = path ? fopen(path, "r") : stdin;

  if (!fd)
  {
    perror(path);
    return NULL;
  }

  c = 0;
  cap = 0;
  bt_read = 0;
  tmp = NULL;
  buffer = NULL;

  while ( (c = fgetc(fd)) != EOF )
  {
    if (bt_read == cap)
    {
      cap += CHUNK;
      tmp = realloc(buffer, cap);

      if (!tmp)
      {
        if (fd != stdin)
          fclose(fd);
        free(buffer);
        perror("allocate error");
        return NULL;
      }
      buffer = tmp;
    }

    buffer[bt_read] = c;

    bt_read++;
  }

  cap = bt_read + 1;

  tmp = realloc(buffer, cap);

  if (!tmp) 
  {
    free(buffer);
    if (fd != stdin)
      fclose(fd);
    perror("reallocate error");
    return NULL;
  }

  buffer = tmp;
  buffer[bt_read] = '\0';

  if (fd != stdin)
    fclose(fd);

  return buffer;
}

static void
prange(const char *txt, const size_t ln_from, const size_t ln_to, context *ctx)
{
  const char *pat;
  size_t line;
  size_t wlen;
  size_t i;

  pat = NULL;
  line = 1;
  wlen = 0;

  if (!txt || !ctx)
    return;

  if (ctx->search.pattern)
    {
      pat = ctx->search.pattern;
    wlen = strlen(pat);
  }

  for (i = 0; txt[i]; i++)
  {
    if (line >= ln_to)
      break;

    if (wlen > 0 &&
      line >= ln_from &&
      line < ln_to &&
      !xstrncasecmp(&txt[i], pat, wlen))
    {
      fprintf(stdout, "\033[1;44m%.*s\033[0m", (int)wlen, pat);

      i += wlen - 1;

      continue;
    }

    if (line >= ln_from && line < ln_to)
    {
      /* prevent the use of escape sequences */
      if (txt[i] == '\e')
        fputc('\\', stdout);
      fputc(txt[i], stdout);
    }

    if (txt[i] == '\n')
      line++;
  }
}


static int
vm_init(context *ctx, const char *src)
{
  ctx->c = '?';
  ctx->run = 1;
  ctx->last = lncount(src);
  ctx->tty = fopen(DEVTTY, "r");

  if (!ctx->filename)
    ctx->filename = "STDIN";

  if (!ctx->tty)
  {
    fprintf(stderr, "vm: error opening tty: \""DEVTTY"\":\n\033[1;31m%s\033[0m\n", strerror(errno));
    return -1;
  }

  if (getraw(fileno(ctx->tty), &old_term) == -1)
  {
    fprintf(stderr, "vm: termios error:\n\033[1;31m%s\033[0m\n", strerror(errno));
    return -1;
  }

  return 0;
}

/* pg function */ 
static void
vm(const char *src, context *ctx, int recursive)
{
  if (!src) return;

  size_t         bot;

  if (vm_init(ctx, src) != 0)
    return;

  if (getwsz(&ctx->cwsize) == -1)
    return;

  while (ctx->run)
  {
    fputs(ASCII_CLS, stdout); 
    fflush(stdout);

    bot = (ctx->last < ctx->cwsize.ws_row) ? 0 : ctx->last - ctx->cwsize.ws_row;

    prange(src, ctx->y, ctx->y + ctx->cwsize.ws_row, ctx);

    status(ctx->cwsize.ws_row, messages[STATUS_FMT],
        ctx->filename,                       /* filename */
        ctx->y,                              /* current position */ 
        bot,                                 /* bottom */
        ctx->c,                              /* last action */
        ctx->search.current                  /* current search match */
        );

    ctx->c = fgetc(ctx->tty);
    
    if (ctx->c == EOF)
      break;

    switch (ctx->c)
    {
      case 'q':
        ctx->run = 0;
        goto cleanup;

      case 'k':
        if (ctx->y > 0)
          ctx->y--;
        break;

      case 'j':
        if (ctx->y + ctx->cwsize.ws_row < ctx->last)
          ctx->y++;
        break;

      /* go to first line */
      case 'g':
        ctx->y = 0;
        break;

      /* go to end */
      case 'G':
        if (ctx->last > ctx->cwsize.ws_row)
          ctx->y = ctx->last - ctx->cwsize.ws_row;
        else 
          ctx->y = 0;
        break;

      case '/':
        {
          if (ctx->search.pattern)
          {
            free( (void *)ctx->search.pattern);
            ctx->search.pattern = 0;
          }

          ctx->search.pattern = input(ctx, "/");

          ctx->search.pos = src;
          ctx->search.current = 0;
          
          search_next(ctx, ctx->search.pos);

          if (src == ctx->search.pos)
          {
            status(ctx->cwsize.ws_row, messages[SNOT_FOUND], ctx->search.pattern);
            fgetc(ctx->tty);
          }

          break;
        }

      case 'n':
      {
        if (!ctx->search.pattern)
          break;

        const char *current;
        current = ctx->search.pos;

        search_next(ctx, ctx->search.pos);

        if (current == ctx->search.pos)
        {
          ctx->search.pos = src;
          ctx->search.current = 0;
          search_next(ctx, ctx->search.pos);
        }

        break;
      }

      
      /* to x line */
      case 't':
        {
          int next;
          long int nline;

          next = fgetc(ctx->tty);
          switch (next)
          
          {
            case 't':
             nline = dinput(ctx);

              if ((size_t)nline + ctx->cwsize.ws_row <= ctx->last)
                {
                  ctx->y = nline;
                }
              else
                {
                  status(ctx->cwsize.ws_row, messages[INCORRECT_LINE]);
                  fgetc(ctx->tty);
                }
              break;
          }

          break;
        }

      /* page down */
      case CTRL('d'):
      case ' ':
        ctx->c = 'D';
        if ( ctx->y + (2 * ctx->cwsize.ws_row) < ctx->last )
          ctx->y += ctx->cwsize.ws_row;
        else
          ctx->y = ctx->last - ctx->cwsize.ws_row;
        break;
      
      /* page up */
      case CTRL('u'):

        ctx->c = 'U';
        if (ctx->y >= ctx->cwsize.ws_row)
        {
          ctx->y -= ctx->cwsize.ws_row;
          break;
        }

        ctx->y = 0;

        break;

      case 'h':
      {
        if (recursive) /* if this is not the case, recursion is possible. */
        {
          ctx->c = '?';
          break;
        } 

        fputs(ASCII_CLS, stdout);
        fflush(stdout);

        context help_ctx = {0};

        help_ctx.filename = "HELP";

        vm(vm_usage,  &help_ctx, 1);

        break;
      }
    
      default:
        ctx->c = '?';
        break;
    }
  }

  cleanup:
    if (ctx->tty)
    {
      setterm(fileno(ctx->tty), &old_term);
      fclose(ctx->tty);
      ctx->tty = 0;
    }

    if (ctx->search.pattern)
    {
      free( (void *) ctx->search.pattern);
      ctx->search.pattern = 0;
    }

    fputs(ASCII_CLS, stdout);
    fflush(stdout);
}


int
main(int ac, char **av)
{
  int               c;
  char              *file;
  context           ctx;

  memset(&ctx, 0, sizeof(ctx));
  memset(&old_term, 0, sizeof(old_term));

  while ( (c = getopt(ac, av, "Hhv")) != -1)
  {
    switch (c)
    {
      case 'v':
        fputs("vm: version - "VM_VERSION"\n", stdout);
	      return 0;
      case 'h':
        fputs(vm_usage, stdout);
        exit(EXIT_SUCCESS);
      case 'H':
        vm(vm_usage, &ctx, 1);
        exit(EXIT_SUCCESS);
      default:
        fputs(vm_usage,stderr);
        exit(EXIT_FAILURE);
    }
  }

  if (optind == ac)
    {
      file = fview(NULL);
      ctx.filename = NULL;
    }
  else
    {
      file = fview(av[optind]);
      ctx.filename = av[optind];
    }

  if (!file)
  {
    fprintf(stderr, "file opening error\n");
    return -1;
  }

  vm(file, &ctx, 0);
  free(file);

  return 0;
}
