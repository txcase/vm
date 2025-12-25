#ifndef VM_TERM_H
#define VM_TERM_H

#include <termios.h>

#define ASCII_CLS           "\033[0m\033[H\033[2J"
#define ASCII_CURS_HIDE     "\e[?25l"
#define ASCII_CURS_PRINT    "\e[?25h"

enum 
{
  STATUS_FMT,
  INCORRECT_LINE,
  SNOT_FOUND,
  SNO_TAGS,
  EXEC_ENOENT,
  MSGCOUNT,
};

extern const char *messages[];

void mvcurs(unsigned int y, unsigned int x);
int  getraw(int fd, struct termios *old_term);
int  setterm(int fd, const struct termios *term);

#endif

