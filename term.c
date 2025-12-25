#include <stdio.h>
#include <unistd.h>
#include "term.h"

const char *messages[] = 
{
#ifndef ASCII
  [STATUS_FMT] = "f:\"%s\" p:%u/%u k:%c m:%u", /* File ; Y/Bot; LastKey ; CurrentMatch*/
  [INCORRECT_LINE] = "e:incorect line!",
  [SNOT_FOUND] = "\"%s\": not found!",
  [SNO_TAGS] = "no tags!",
#else
  [STATUS_FMT] = "f:\033[1;34m%s\033[0m p:%u/%u k:%c m:%u",
  [INCORRECT_LINE] = "\033[1;31me:incorect line!\033[0m",
  [SNOT_FOUND] = "\033[1;44m\"%s\": not found!\033[0m",
  [SNO_TAGS] = "\033[1;44mno tags!\033[0m",
#endif /* #ifdef ASCII */
};

void
mvcurs(const unsigned int y,
       const unsigned int x)
{
  if (x < 1)
    return;
  fprintf(stdout, "\033[%u;%uH", y, x);
}

int
setterm(int fd, const struct termios *term)
{
  int res;
  res = tcsetattr(fd, TCSANOW, term);
  if (res < 0)
  {
    perror("termios setattr");
    return res;
  }
  return 0;
}

int 
getraw(int fd, struct termios *old_term)
{
  struct termios new;

  if (tcgetattr(fd, old_term) < 0)
  {
    perror("termios getattr");
    return -1;
  }

  new = *old_term;

  new.c_lflag &= ~(ECHO | ICANON | IEXTEN);

  new.c_cc[VMIN]  = 1;
  new.c_cc[VTIME] = 0;

  return setterm(fd, &new);
}
