#include <stdio.h>
#include <syscall.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void test_main(void) {

  char child_cmd[128];
  int handle;
  CHECK(create("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK((handle = open("sample.txt")) > 1, "open \"test.txt\"");

  snprintf(child_cmd, sizeof child_cmd, "child-write");
  msg("wait(exec()) = %d", wait(exec(child_cmd)));
  check_file_handle(handle, "test.txt", sample, sizeof sample - 1);
}
