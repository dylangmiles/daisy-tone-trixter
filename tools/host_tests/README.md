# Host tests

Engine logic that is cheap to prove on the Mac and expensive to debug underfoot.

```
cd tools/host_tests
c++ -std=c++14 -O1 -I../.. -I. looper_test.cpp ../../audio/looper.cpp -o looper_test && ./looper_test
```

`dev/sdram.h` stubs `DSY_SDRAM_BSS` so `looper.cpp` compiles unchanged. Covers: first pass → play,
overdub auto-commit after one loop, undo stack, an overdub cut short (extent tracking), stop/play
from the top, tempo lock (bpm + bars), bar snap (bpm only), the 60 s cap, and the free-loop bpm guess.
