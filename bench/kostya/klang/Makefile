# K compiles the whole program to one C file, which clang compiles.

prod:
	mkdir -p target
	k -O2 --standalone -o target/benchmark.c src/main.k
	clang -O2 -w target/benchmark.c -o target/benchmark -lm -pthread

test:
	mkdir -p target
	k -O2 --standalone -o target/test.c src/main.k
	clang -O1 -w target/test.c -o target/test -lm -pthread

.PHONY: prod test
