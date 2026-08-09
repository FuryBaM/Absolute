# Fuzzer regression corpus

Every file here once crashed or hung a fuzz target. `run_fuzzer.py` replays all
of them before it starts fuzzing, so a fixed defect cannot come back unnoticed.

When a fuzz run reports a crash it prints the minimized input and the command
that reproduces it. Add that input here, named after what it broke, in the same
commit as the fix.
