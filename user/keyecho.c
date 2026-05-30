#include "types.h"
#include "stat.h"
#include "user.h"

int
main(void)
{
  char c;
  int n;

  printf(1, "keyecho: press keys, Ctrl-D to quit\n");
  while((n = read(0, &c, 1)) == 1){
    uchar u = (uchar)c;
    if(u == 4)
      break;

    if(u >= 32 && u <= 126)
      printf(1, "byte=0x%x char='%c'\n", u, c);
    else
      printf(1, "byte=0x%x\n", u);
  }

  if(n < 0)
    printf(1, "keyecho: read error\n");
  return 0;
}
