#ifndef __XEXEC_PLUGIN_H
#define __XEXEC_PLUGIN_H

extern struct xexec *xexec_set;

extern void xexec_plugin_init(struct module *module);
extern void xexec_plugin_deinit(void);

#endif
