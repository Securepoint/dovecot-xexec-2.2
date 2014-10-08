/*
 *
 * plugin to add the XEXEC command for dovecot IMAP server
 * Copyright (C) 2007 Nicolas Boullis
 *
 * Parts of this file were copied from dovecot 1.0.0's
 * src/plugins/quota/quota-plugin.c and src/plugins/quota/quota which are
 * Copyright (C) 2005 Timo Sirainen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "imap-common.h"
#include "array.h"
#include "str.h"
#include "module-context.h"
#include "imap-commands.h"
#include "xexec.h"
#include "xexec-plugin.h"

#include <stdlib.h>

const char *imap_xexec_plugin_version = DOVECOT_VERSION;

static struct module *imap_xexec_module;
static void (*next_hook_client_created)(struct client **client);

static MODULE_CONTEXT_DEFINE_INIT(imap_xexec_imap_module,
				  &imap_module_register);

struct xexec *xexec_set = NULL;

static struct xexec_setup *
xexec_setup_init(struct xexec *xexec, const char *data)
{
	struct xexec_setup *setup;
	const char *p;

	setup = p_new(xexec->pool, struct xexec_setup, 1);
	setup->xexec = xexec;

	p = strchr(data, ':');
	if (p == NULL)
		i_fatal("Malformed xexec setup: %s", data);

	setup->imap_command = p_strdup_until(xexec->pool, data, p);
	setup->backend_command = (void *)p_strsplit(xexec->pool, p+1, " ");

	array_append(&xexec->setups, &setup, 1);
	return setup;
}

static struct xexec *xexec_init(struct client *client)
{
	const char *env;
	unsigned int i;

	env = mail_user_plugin_getenv(client->user, "xexec");
	if (env == NULL)
		return NULL;

	xexec_set = p_new(client->pool, struct xexec, 1);
	xexec_set->pool = client->pool;
	p_array_init(&xexec_set->setups, client->pool, 4);

	if (xexec_setup_init(xexec_set, env) == NULL)
		i_fatal("Couldn't create xexec setup: %s", env);

	for (i = 2;; i++) {
		const char *setup_name;

		setup_name = t_strdup_printf("xexec%d", i);
		env = mail_user_plugin_getenv(client->user, setup_name);

		if (env == NULL)
			break;

		if (xexec_setup_init(xexec_set, env) == NULL)
			i_fatal("Couldn't create xexec setup: %s", env);
	}
	return xexec_set;
}

static void imap_xexec_client_created(struct client **client)
{
	struct xexec *xclient;

	if (mail_user_is_plugin_loaded((*client)->user, imap_xexec_module)) {
		xclient = xexec_init(*client);
		if (xclient != NULL) {
			MODULE_CONTEXT_SET(*client, imap_xexec_imap_module, xclient);
			str_append((*client)->capability_string, " XEXEC");
		}
	}

	if (next_hook_client_created != NULL)
		next_hook_client_created(client);
}

void xexec_plugin_init(struct module *module ATTR_UNUSED)
{
	command_register("XEXEC", cmd_xexec, 0);

	imap_xexec_module = module;
	next_hook_client_created =
		imap_client_created_hook_set(imap_xexec_client_created);
}

void xexec_plugin_deinit(void)
{
	command_unregister("XEXEC");

	imap_client_created_hook_set(next_hook_client_created);
}
