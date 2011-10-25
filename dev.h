#ifndef SA_DEV_H
#define SA_DEV_H

#include <unistd.h>

struct device {
	void *readbuf;
	void *writebuf[3];
	int fd;
	off64_t size;
};

enum writebuf {
	WBUF_ZERO,
	WBUF_ONE,
	WBUF_RAND,
};

extern int setup_dev(struct device *dev, const char *filename, bool no_direct);

long long time_write(struct device *dev, off64_t pos, size_t size, enum writebuf which);

long long time_read(struct device *dev, off64_t pos, size_t size);

long long time_erase(struct device *dev, off64_t pos, size_t size);

#endif /* SA_DEV_H */
