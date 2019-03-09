.PHONY: all
all:
	make -f makefile.posix all
	make -f makefile.mingw32 all

.PHONY: test
test:
	make -f makefile.posix test
	make -f makefile.mingw32 test

.PHONY: clean
clean:
	make -f makefile.posix clean
	make -f makefile.mingw32 clean
