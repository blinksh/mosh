#ifndef TERMINALBRIDGE_H
#define TERMINALBRIDGE_H

#include <sys/ioctl.h>

int mosh_main(FILE *f_in, FILE *f_out, struct winsize *window_size,
	      const char *ip, const char *port, const char *key, const char *predict_mode)

#endif
