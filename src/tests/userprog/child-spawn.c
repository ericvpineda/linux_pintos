/* Child process run by exec-multiple, exec-one, wait-simple, and
   wait-twice tests.
   Just prints a single message and terminates. */

#include <stdio.h>
#include "tests/lib.h"
#include "tests/main.h"

int main(void) {
  test_name = "child-spawn";
  msg("run");

  pid_t child1 = exec("child-simple");
  wait(child1);
  return 100;
}
