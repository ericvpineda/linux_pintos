# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(wait-gchild) begin
(child-spawn) run
load: child-simple: open failed
child-spawn: exit(100)
(wait-gchild) wait(exec()) = 100
(wait-gchild) end
wait-gchild: exit(0)
EOF
pass;