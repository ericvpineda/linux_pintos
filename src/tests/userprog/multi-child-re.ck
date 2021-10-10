# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(multi-child-re) begin
(multi-child-re) create "test.txt"
(multi-child-re) open "test.txt"
(child-write) begin
(child-write) open "test.txt"
(child-write) end
child-write: exit(0)
(multi-child-re) wait(exec()) = 0
(multi-child-re) verified contents of "test.txt"
(multi-child-re) end
multi-child-re: exit(0)
EOF
pass;