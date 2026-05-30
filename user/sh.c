// Shell.

#include "types.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);

#define HIST_MAX 16
#define KEY_UP 0xE2
#define KEY_DN 0xE3

static char history[HIST_MAX][100];
static int hist_start;
static int hist_count;

static int is_ws(char c);
static int is_sym(char c);
static int starts_with(const char *s, const char *prefix);
static void trim_newline(char *s);
static int is_blank_line(const char *s);
static int streq(const char *a, const char *b);
static char* history_get(int idx);
static void history_add(const char *line);
static void set_line(char *dst, int nbuf, int *len, const char *src);
static void redraw_line(char *buf, int len, int *oldlen);
static int collect_matches(char *prefix, char matches[][DIRSIZ+1], int maxm);
static void apply_history(char *buf, int nbuf, int *len, int *oldlen, int pos);
static void try_tab_complete(char *buf, int nbuf, int *len, int *oldlen);
static int readline(char *buf, int nbuf);

static int
is_ws(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v';
}

static int
is_sym(char c)
{
  return strchr("<|>&;()", c) != 0;
}

static int
starts_with(const char *s, const char *prefix)
{
  int i;

  for(i = 0; prefix[i]; i++){
    if(s[i] != prefix[i])
      return 0;
  }
  return 1;
}

static int
streq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
}

static void
trim_newline(char *s)
{
  int n;

  n = strlen(s);
  if(n > 0 && s[n-1] == '\n')
    s[n-1] = 0;
}

static int
is_blank_line(const char *s)
{
  int i;

  for(i = 0; s[i]; i++){
    if(!is_ws(s[i]))
      return 0;
  }
  return 1;
}

static char*
history_get(int idx)
{
  return history[(hist_start + idx) % HIST_MAX];
}

static void
history_add(const char *line)
{
  int pos;
  int n;
  char tmp[100];
  char *last;

  n = strlen(line);
  if(n > (int)sizeof(tmp) - 1)
    n = sizeof(tmp) - 1;
  memmove(tmp, line, n);
  tmp[n] = 0;
  trim_newline(tmp);
  if(is_blank_line(tmp))
    return;

  if(hist_count > 0){
    last = history_get(hist_count - 1);
    if(streq(last, tmp))
      return;
  }

  if(hist_count < HIST_MAX){
    pos = (hist_start + hist_count) % HIST_MAX;
    hist_count++;
  } else {
    pos = hist_start;
    hist_start = (hist_start + 1) % HIST_MAX;
  }

  n = strlen(tmp);
  if(n > (int)sizeof(history[pos]) - 1)
    n = sizeof(history[pos]) - 1;
  memmove(history[pos], tmp, n);
  history[pos][n] = 0;
}

static void
set_line(char *dst, int nbuf, int *len, const char *src)
{
  int n;

  n = strlen(src);
  if(n > nbuf - 1)
    n = nbuf - 1;
  memmove(dst, src, n);
  dst[n] = 0;
  *len = n;
}

static void
redraw_line(char *buf, int len, int *oldlen)
{
  int i;

  printf(2, "\r$ ");
  if(len > 0)
    write(2, buf, len);
  for(i = len; i < *oldlen; i++)
    write(2, " ", 1);
  printf(2, "\r$ ");
  if(len > 0)
    write(2, buf, len);
  *oldlen = len;
}

static int
collect_matches(char *prefix, char matches[][DIRSIZ+1], int maxm)
{
  int i, j, fd, n, mcount, dup;
  struct dirent de;
  char name[DIRSIZ+1];
  char *builtins[2];

  mcount = 0;
  builtins[0] = "cd";
  builtins[1] = "exit";

  for(i = 0; i < 2; i++){
    if(starts_with(builtins[i], prefix)){
      if(mcount < maxm){
        n = strlen(builtins[i]);
        if(n > DIRSIZ)
          n = DIRSIZ;
        memmove(matches[mcount], builtins[i], n);
        matches[mcount][n] = 0;
        mcount++;
      }
    }
  }

  fd = open(".", O_RDONLY);
  if(fd < 0)
    return mcount;

  while((n = read(fd, &de, sizeof(de))) == sizeof(de)){
    if(de.inum == 0)
      continue;
    memmove(name, de.name, DIRSIZ);
    name[DIRSIZ] = 0;
    for(i = DIRSIZ - 1; i >= 0; i--){
      if(name[i] == 0)
        continue;
      break;
    }
    name[i+1] = 0;
    if(!starts_with(name, prefix))
      continue;

    dup = 0;
    for(j = 0; j < mcount; j++){
      if(streq(matches[j], name)){
        dup = 1;
        break;
      }
    }
    if(!dup && mcount < maxm){
      n = strlen(name);
      if(n > DIRSIZ)
        n = DIRSIZ;
      memmove(matches[mcount], name, n);
      matches[mcount][n] = 0;
      mcount++;
    }
  }
  close(fd);
  return mcount;
}

static void
apply_history(char *buf, int nbuf, int *len, int *oldlen, int pos)
{
  if(pos < 0 || pos >= hist_count)
    return;
  set_line(buf, nbuf, len, history_get(pos));
  redraw_line(buf, *len, oldlen);
}

static void
try_tab_complete(char *buf, int nbuf, int *len, int *oldlen)
{
  int start, plen, mcount, i, mlen, newlen;
  char prefix[100];
  char matches[32][DIRSIZ+1];

  start = *len;
  while(start > 0 && !is_ws(buf[start-1]) && !is_sym(buf[start-1]))
    start--;

  plen = *len - start;
  if(plen <= 0)
    return;
  if(plen >= (int)sizeof(prefix))
    return;

  memmove(prefix, buf + start, plen);
  prefix[plen] = 0;

  mcount = collect_matches(prefix, matches, 32);
  if(mcount == 0)
    return;

  if(mcount == 1){
    mlen = strlen(matches[0]);
    newlen = start + mlen;
    if(newlen + 1 >= nbuf)
      return;
    memmove(buf + start, matches[0], mlen);
    buf[newlen] = ' ';
    newlen++;
    buf[newlen] = 0;
    *len = newlen;
    redraw_line(buf, *len, oldlen);
    return;
  }

  printf(2, "\n");
  for(i = 0; i < mcount; i++)
    printf(2, "%s%s", matches[i], (i + 1 == mcount) ? "\n" : "  ");
  redraw_line(buf, *len, oldlen);
}

static int
readline(char *buf, int nbuf)
{
  int len, oldlen, n;
  uchar c, c1, c2;
  int hist_pos;
  char scratch[100];
  int scratch_len;

  memset(buf, 0, nbuf);
  len = 0;
  oldlen = 0;
  hist_pos = -1;
  scratch[0] = 0;
  scratch_len = 0;

  printf(2, "$ ");
  for(;;){
    n = read(0, &c, 1);
    if(n < 1){
      if(len == 0)
        return -1;
      break;
    }

    if(c == '\n' || c == '\r'){
      if(len < nbuf - 1)
        buf[len++] = '\n';
      buf[len] = 0;
      history_add(buf);
      return 0;
    }

    if(c == 127 || c == '\b'){
      if(len > 0){
        len--;
        buf[len] = 0;
        redraw_line(buf, len, &oldlen);
      }
      continue;
    }

    if(c == '\t'){
      try_tab_complete(buf, nbuf, &len, &oldlen);
      continue;
    }

    if(c == KEY_UP){
      if(hist_count == 0)
        continue;
      if(hist_pos == -1){
        memmove(scratch, buf, len);
        scratch[len] = 0;
        scratch_len = len;
        hist_pos = hist_count - 1;
      } else if(hist_pos > 0){
        hist_pos--;
      }
      apply_history(buf, nbuf, &len, &oldlen, hist_pos);
      continue;
    }

    if(c == KEY_DN){
      if(hist_pos == -1)
        continue;
      if(hist_pos < hist_count - 1){
        hist_pos++;
        apply_history(buf, nbuf, &len, &oldlen, hist_pos);
      } else {
        hist_pos = -1;
        if(scratch_len > nbuf - 1)
          scratch_len = nbuf - 1;
        memmove(buf, scratch, scratch_len);
        len = scratch_len;
        buf[len] = 0;
        redraw_line(buf, len, &oldlen);
      }
      continue;
    }

    if(c == 27){
      if(read(0, &c1, 1) != 1)
        continue;
      if(read(0, &c2, 1) != 1)
        continue;
      if(c1 == '[' && c2 == 'A'){
        if(hist_count == 0)
          continue;
        if(hist_pos == -1){
          memmove(scratch, buf, len);
          scratch[len] = 0;
          scratch_len = len;
          hist_pos = hist_count - 1;
        } else if(hist_pos > 0){
          hist_pos--;
        }
        apply_history(buf, nbuf, &len, &oldlen, hist_pos);
      } else if(c1 == '[' && c2 == 'B'){
        if(hist_pos == -1)
          continue;
        if(hist_pos < hist_count - 1){
          hist_pos++;
          apply_history(buf, nbuf, &len, &oldlen, hist_pos);
        } else {
          hist_pos = -1;
          if(scratch_len > nbuf - 1)
            scratch_len = nbuf - 1;
          memmove(buf, scratch, scratch_len);
          len = scratch_len;
          buf[len] = 0;
          redraw_line(buf, len, &oldlen);
        }
      }
      continue;
    }

    if(c >= 32 && c < 127){
      if(len < nbuf - 1){
        buf[len++] = c;
        buf[len] = 0;
        if(hist_pos != -1){
          hist_pos = -1;
          memmove(scratch, buf, len);
          scratch[len] = 0;
          scratch_len = len;
        }
      }
      continue;
    }
  }
  return -1;
}

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit();

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit();
    exec(ecmd->argv[0], ecmd->argv);
    printf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      printf(2, "open %s failed\n", rcmd->file);
      exit();
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait();
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait();
    wait();
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit();
}

int
getcmd(char *buf, int nbuf)
{
  if(readline(buf, nbuf) < 0)
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Chdir must be called by the parent, not the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        printf(2, "cannot cd %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait();
  }
  exit();
}

void
panic(char *s)
{
  printf(2, "%s\n", s);
  exit();
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    printf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
