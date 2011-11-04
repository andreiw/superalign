CC	:= $(CROSS_COMPILE)gcc
CFLAGS	:= -O2 -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -g2
LDFLAGS := -lrt -lm

all: sa

sa: sa.o dev.o stats.o
sa.o: sa.c dev.h
dev.o: dev.c dev.h
stats.o: stats.c stats.h

clean:
	rm -f sa sa.o dev.o stats.o
