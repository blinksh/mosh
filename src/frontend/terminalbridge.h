#ifndef TERMINALBRIDGE_H
#define TERMINALBRIDGE_H

int mosh_main(FILE *f_in, FILE *f_out,
	      const char *ip, const char *port, const char *key, const char *predict_mode);

#endif
