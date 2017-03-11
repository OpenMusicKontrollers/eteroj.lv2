all: test.chunk test.fast

netatom_test_fast: netatom_test.c ../netatom.lv2/netatom.h
	clang -O3 -o $@ $< -I../netatom.lv2 $(shell pkg-config --cflags lv2) $(shell pkg-config --cflags --libs sratom-0)

netatom_test_assert: netatom_test.c ../netatom.lv2/netatom.h
	clang -g -o $@ $< -I../netatom.lv2 $(shell pkg-config --cflags lv2) $(shell pkg-config --cflags --libs sratom-0)

test.chunk: netatom_test_assert
	./$< 1 > $@

test.fast: netatom_test_fast
	./$< 100000
