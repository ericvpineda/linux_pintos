#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include "tests/userprog/sample.inc"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  test_name = "child-write";

  msg("begin");

  int handle, byte_cnt;
  CHECK((handle = open("test.txt")) > 1, "open \"test.txt\"");

  byte_cnt = write(handle, sample, sizeof sample - 1);
  if (byte_cnt != sizeof sample - 1) {
    fail("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
  }

  msg("end");

  return 0;
}
