#ifndef __XEXEC_H
#define __XEXEC_H

#include "imap-client.h"

struct xexec {
	union imap_module_context module_ctx;

	pool_t pool;
	ARRAY(struct xexec_setup *) setups;
};

struct xexec_setup {
	struct xexec *xexec;

	const char *imap_command;
	const char *const *backend_command;
};

extern struct xexec *xexec_set;

extern bool cmd_xexec(struct client_command_context *cmd);

#endif
