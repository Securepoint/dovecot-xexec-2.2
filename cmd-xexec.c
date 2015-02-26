/*
 *
 * XEXEC command for dovecot IMAP server
 * Copyright (C) 2007 Nicolas Boullis
 *
 * Parts of this file were copied from dovecot 1.0.0's
 * src/imap/cmd-append.c file which is
 * Copyright (C) 2002 Timo Sirainen
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
#include "istream.h"
#include "ostream.h"
#include "array.h"
#include "execv-const.h"
#include "xexec.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

struct cmd_xexec_context {
	struct client *client;
	struct client_command_context *cmd;
	pid_t pid;
	struct ostream *in;
	struct istream *out;
	struct istream *err;
	struct ioloop *loop;
	struct io *io_client;
	struct io *io_out;
	struct io *io_err;
	unsigned requested_lines;
};

static void cmd_xexec_client(struct cmd_xexec_context *context)
{
	struct cmd_xexec_context *ctx;
	char *line;

	ctx = (struct cmd_xexec_context *)(context);

	i_stream_read(ctx->client->input);
#if 1
	while (ctx->requested_lines
	       && (line = i_stream_next_line(ctx->client->input))) {
		o_stream_send_str(ctx->in, t_strconcat(line, "\n", NULL));
		ctx->requested_lines--;
	}
#else
	while ((line = i_stream_next_line(ctx->client->input)))
		o_stream_send_str(ctx->in, t_strconcat(line, "\n", NULL));
#endif
	o_stream_flush(ctx->in);
#if 1
	if (!ctx->requested_lines)
		io_remove(&ctx->io_client);
#endif
}

static void cmd_xexec_stdout(struct cmd_xexec_context *context)
{
	struct cmd_xexec_context *ctx;
	char *line;

	ctx = (struct cmd_xexec_context *)(context);

	if (i_stream_read(ctx->out) == -1) {
		io_remove(&ctx->io_out);
		io_loop_stop(ctx->loop);
	}
	while ((line = i_stream_next_line(ctx->out))) {
		if (strcmp(line, "\5") == 0) {
			client_send_line(ctx->client, "+ OK");
			o_stream_flush(ctx->client->output);
#if 1
			if (ctx->requested_lines++ == 0)
				ctx->io_client = io_add(
					i_stream_get_fd(ctx->client->input),
					IO_READ, cmd_xexec_client, ctx);
#endif
		}
		else {
			client_send_line(ctx->client,
					 t_strconcat("* OK ", line, NULL));
			o_stream_flush(ctx->client->output);

		}
	}
}

static void cmd_xexec_stderr(struct cmd_xexec_context *context)
{
	struct cmd_xexec_context *ctx;
	char *line;

	ctx = (struct cmd_xexec_context *)(context);

	if (i_stream_read(ctx->err) == -1) {
		io_remove(&ctx->io_err);
		io_loop_stop(ctx->loop);
	}
	while ((line = i_stream_next_line(ctx->err))) {
		client_send_line(ctx->client, t_strconcat("* NO ", line, NULL));
	}
	o_stream_flush(ctx->client->output);
}

bool cmd_xexec(struct client_command_context *cmd)
{
	const struct imap_arg * imap_args;
	const char *imap_command;
	const char *const *backend_command;
	ARRAY(const char *) command;
	pid_t pid;
	int status;
	int pipe_in[2];
	int pipe_out[2];
	int pipe_err[2];
	struct cmd_xexec_context *ctx;
	struct xexec_setup *const *setups;
	unsigned int i, count;

	if (!client_read_args(cmd, 0, 0, &imap_args))
		return FALSE;

	if (imap_args[0].type == IMAP_ARG_EOL) {
		client_send_command_error(cmd, "Missing subcommand.");
		return FALSE;
	}
	if (!imap_arg_get_atom(&imap_args[0], &imap_command)) {
		client_send_command_error(cmd, "Invalid subcommand.");
		return FALSE;
	}

	backend_command = NULL;
	setups = array_get(&xexec_set->setups, &count);
	for (i = 0; i < count; i++) {
		if (!strcasecmp(imap_command, setups[i]->imap_command)) {
			backend_command = setups[i]->backend_command;
			break;
		}
	}

	if (!backend_command) {
		const char * msg;
		msg = t_strconcat("Unknown ",
				  t_str_ucase(imap_command),
				  " subcommand.",
				  NULL);
		client_send_command_error(cmd, msg);
		return FALSE;
	}

	t_array_init(&command, 8);
	array_append(&command, backend_command, str_array_length(backend_command));

	for (i = 1; imap_args[i].type != IMAP_ARG_EOL; i++) {
		const char *str;

		if (!imap_arg_get_atom(&imap_args[i], &str)) {
			client_send_command_error(cmd, "Invalid arguments.");
			return FALSE;
		}
		array_append(&command, &str, 1);
	}
	(void)array_append_space(&command);

	i_stream_read_next_line(cmd->client->input);
	cmd->client->input_skip_line = FALSE;

	ctx = p_new(cmd->pool, struct cmd_xexec_context, 1);
	ctx->cmd = cmd;
	ctx->client = cmd->client;
	ctx->requested_lines = 0;

	if (pipe(pipe_in) < 0 ||
	    pipe(pipe_out) < 0 ||
	    pipe(pipe_err) < 0) {
		/* FIXME: memory leak */
		i_error("pipe() failed: %m");
		client_send_tagline(cmd, "NO Internal failure");
		return TRUE;
	}

	pid = fork();
	if (pid < 0) {
		i_error("fork() failed: %m");
		client_send_tagline(cmd, "NO Internal failure");
		return TRUE;
	}

	if (!pid) {
		close(0); close(1); close(2);
		close(pipe_in[1]);
		close(pipe_out[0]);
		close(pipe_err[0]);

		if (dup2(pipe_in[0], 0) != 0)
			i_fatal("dup2() failed: %m");
		close(pipe_in[0]);

		if (dup2(pipe_out[1], 1) != 1)
			i_fatal("dup2() failed: %m");
		close(pipe_out[1]);

		if (dup2(pipe_err[1], 2) != 2)
			i_fatal("dup2() failed: %m");
		close(pipe_err[1]);

		backend_command = array_idx(&command, 0);
		execvp_const(backend_command[0], backend_command);
	}

	close(pipe_in[0]);
	close(pipe_out[1]);
	close(pipe_err[1]);

	ctx->in = o_stream_create_fd(pipe_in[1], 0, TRUE);
	ctx->out = i_stream_create_fd(pipe_out[0], 1024, TRUE);
	ctx->err = i_stream_create_fd(pipe_err[0], 1024, TRUE);

	ctx->loop = io_loop_create();
	ctx->io_out = io_add(i_stream_get_fd(ctx->out), IO_READ,
			     cmd_xexec_stdout, ctx);
	ctx->io_err = io_add(i_stream_get_fd(ctx->err), IO_READ,
			     cmd_xexec_stderr, ctx);
#if 1
#else
	ctx->io_client = io_add(i_stream_get_fd(ctx->client->input),
				IO_READ, cmd_xexec_client, ctx);
#endif


	io_loop_run(ctx->loop);
	if (ctx->io_client)
		io_remove(&ctx->io_client);
	if (ctx->io_out)
		io_remove(&ctx->io_out);
	if (ctx->io_err)
		io_remove(&ctx->io_err);
	io_loop_destroy(&ctx->loop);

	o_stream_destroy(&ctx->in);
	i_stream_destroy(&ctx->out);
	i_stream_destroy(&ctx->err);

	waitpid(pid, &status, 0);
	if (!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
		client_send_tagline(cmd, "NO command failed");
		return TRUE;
	}

	client_send_tagline(cmd, "OK command exited successfully");
	return TRUE;
}

