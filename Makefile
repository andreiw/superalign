CC	:= gcc
CFLAGS	:= -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -g2
LDFLAGS := -lrt -lm

all: sa

sa: sa.o dev.o
sa.o: sa.c dev.h
dev.o: dev.c dev.h

clean:
	rm sa sa.o dev.o vm.o
