#ifndef MOSHIOSBRIDGE_HPP
#define MOSHIOSBRIDGE_HPP

#include <stdio.h>
#include <sys/ioctl.h>

#if __cplusplus
extern "C"
#endif
int mosh_main(FILE *f_in, FILE *f_out, struct winsize *window_size,
	      const char *ip, const char *port, const char *key, const char *predict_mode);

#endif
