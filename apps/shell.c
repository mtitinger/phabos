/*
 * Copyright (C) 2014-2015 Fabien Parent. All rights reserved.
 * Author: Fabien Parent <parent.f@gmail.com>
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>

#include <apps/shell.h>
#include <phabos/list.h>
#include <phabos/mm.h>
#include <phabos/scheduler.h>

#define BOLD_TEXT_ESCAPE "\033[1m"
#define NORMAL_TEXT_ESCAPE "\033[0m"

/* keep the nsh suffix in the prompt name, for
 * test scripts expecting nsh from the nuttx time
 */
static const char *const SHELL_PROMPT = "fabonsh> ";
#define COMMAND_LINE_MAX_SIZE   4096
#define ARGV_MAX_SIZE           32

static char buffer[COMMAND_LINE_MAX_SIZE];
static size_t history_cmd_count;
static const size_t history_size = 10;
static struct list_head history = LIST_INIT(history);
static struct list_head *history_iter;
struct shell_history_command {
    struct list_head list;
    char command[0];
};

struct task_context {
    int (*entry)(int argc, char **argv);
    int argc;
    char *argv[ARGV_MAX_SIZE];
};

static int hello_main(int argc, char **argv);
static int help_main(int argc, char **argv);

static struct shell_command *shell_get_commands(void)
{
    extern uint32_t _shell_command;
    return (struct shell_command*) &_shell_command;
}

static void shell_history_rewind(void)
{
    history_iter = &history;
}

static struct shell_history_command* shell_history_next()
{
    if (!history_iter)
        shell_history_rewind();

    history_iter = history_iter->prev;
    if (history_iter == &history) {
        history_iter = history_iter->next;
        return NULL;
    }
    return list_entry(history_iter, struct shell_history_command, list);
}

static struct shell_history_command* shell_history_prev()
{
    if (!history_iter || history_iter == &history)
        return NULL;

    history_iter = history_iter->next;
    return list_entry(history_iter, struct shell_history_command, list);
}

static void shell_history_add(char *command)
{
    struct shell_history_command *cmd;

    if (!history_size)
        return;

    if (history_cmd_count >= history_size) {
        cmd = list_last_entry(&history, struct shell_history_command, list);
        list_del(&cmd->list);
        kfree(cmd);
        history_cmd_count--;
    }

    cmd = kmalloc(sizeof(*cmd) + strlen(command) + 1, MM_KERNEL);
    list_init(&cmd->list);
    strcpy(cmd->command, command);
    list_add(&history, &cmd->list);
    history_cmd_count++;
}

static size_t shell_get_command_count(void)
{
    extern uint32_t _shell_command;
    extern uint32_t _eshell_command;
    return ((uint32_t)&_eshell_command - (uint32_t)&_shell_command) /
           sizeof(struct shell_command);
}

__shell_command__ struct shell_command commands[] = {
    {"help", "", help_main},
    {"hello", "", hello_main},
};

static int hello_main(int argc, char **argv)
{
    printf("Hello %s\n", argc > 1 ? argv[1] : "World");
    return 0;
}

static int help_main(int argc, char **argv)
{
    struct shell_command *cmd = shell_get_commands();
    size_t size = shell_get_command_count();

    printf("Commands available:\n");
    for (int i = 0; i < size; i++)
        printf("\t%s\t\t\t%s\n", cmd[i].name, cmd[i].description);
    return 0;
}

static inline void shell_putc(char c)
{
    putchar(c);
}

static inline char shell_getc(void)
{
    char c = getchar();
    return c;
}

static inline void shell_move_left(void)
{
    shell_putc(0x1B);
    shell_putc('[');
    shell_putc('D');
}

static void shell_process_special_key(char *buffer, int *pos, int *cursor_pos,
                                      size_t size)
{
    char c;
    struct shell_history_command *cmd;

    c = shell_getc();
    if (c != '[')
        return;

    c = shell_getc();
    switch (c) {
    case 'A':
        cmd = shell_history_next();
        if (!cmd)
            return;

        for (int i = *cursor_pos; i < *pos; i++)
            shell_putc(' ');

        for (int i = 0; i < *pos; i++) {
            shell_move_left();
            shell_putc(' ');
           shell_move_left();
        }
        printf("%s", cmd->command);

        *pos = *cursor_pos = strlen(cmd->command);
        strcpy(buffer, cmd->command);
        break;

    case 'B':
        for (int i = *cursor_pos; i < *pos; i++)
            shell_putc(' ');

        for (int i = 0; i < *pos; i++) {
           shell_move_left();
            shell_putc(' ');
           shell_move_left();
        }
        *pos = 0;

        cmd = shell_history_prev();
        if (!cmd)
            return;

        printf("%s", cmd->command);
        *pos = *cursor_pos = strlen(cmd->command);
        strcpy(buffer, cmd->command);
        break;

    case 'C':
        if (*pos <= *cursor_pos)
            return;

        shell_putc(0x1B);
        shell_putc('[');
        shell_putc('C');
        (*cursor_pos)++;
        break;

    case 'D':
        if (*cursor_pos == 0)
            return;

        shell_putc(0x1B);
        shell_putc('[');
        shell_putc('D');
        (*cursor_pos)--;
        break;
    }
}

#if 0
static void shell_rewind_line(int cursor)
{
    while ((*cursor)--)
        shell_putc(0x7F);
}

static void shell_redraw_line(char *buffer, size_t size, restrict int *eol,
                              restrict int *cursor)
{
    shell_rewind_line();

    // Erase everything
    for (int i = 0; i < *eol; i++)
        shell_putc(' ');

    // Erase everything
    for (int i = 0; i < *eol; i++)
        shell_putc(' ');
}
#endif

static size_t shell_readline(char *buffer, size_t size)
{
    char c;
    size_t nread = 0;
    int eol;
    int cursor_pos = 0;

    for (eol = 0; eol < size - 1; eol++) {

	c = shell_getc();
	if (c == '\n' || c == '\r')
		break;

        switch (c) {
        case 0x7F:
            if (eol == 0 || cursor_pos == 0) {
                eol--;
                break;
            }

            cursor_pos--;
            shell_move_left();

            for (int i = cursor_pos; i < eol; i++)
                buffer[i] = buffer[i + 1];
            for (int i = cursor_pos; i > 0; i--)
                shell_move_left();
            for (int i = 0; i < eol; i++)
                shell_putc(' ');
            for (int i = eol; i > 0; i--)
                shell_move_left();
            for (int i = 0; i < eol - 1; i++)
                shell_putc(buffer[i]);
            for (int i = eol - 1; i > cursor_pos; i--)
                shell_move_left();

            eol -= 2;
            break;

        case 0x1B:
            shell_process_special_key(buffer, &eol, &cursor_pos, size);
            eol -= 1;
            break;

        case '\t':
            eol--;
            break;

        default:
            for (int i = eol; i >= cursor_pos; i--)
                buffer[i + 1] = buffer[i];
            buffer[cursor_pos] = c;
            for (int i = cursor_pos; i <= eol; i++)
                shell_putc(buffer[i]);
            for (int i = cursor_pos; i < eol; i++)
                shell_move_left();
            cursor_pos++;
            break;
        }
    }

    printf("\n");
    buffer[eol] = '\0';
    return nread;
}

static void exec_task(void *data)
{
    open("/dev/ttyS0", 0);
    open("/dev/ttyS0", 0);
    open("/dev/ttyS0", 0);

    struct task_context *context = data;
    context->entry(context->argc, context->argv);
}

static void shell_process_line(char *line)
{
    struct shell_command *cmd = shell_get_commands();
    size_t size = shell_get_command_count();
    struct task_context task_ctx = { .argc = 1 };
    char *saveptr;

    task_ctx.argv[0] = strtok_r(line, " ", &saveptr);
    do {
        task_ctx.argv[task_ctx.argc] = strtok_r(NULL, " ", &saveptr);
    } while (task_ctx.argv[task_ctx.argc++]);
    task_ctx.argc--;

    if (!task_ctx.argv[0])
        return;

    for (int i = 0; i < size; i++) {
        if (!strcmp(task_ctx.argv[0], cmd[i].name)) {
            task_ctx.entry = cmd[i].entry;
            struct task *task = task_run(cmd[i].name, exec_task, &task_ctx, 0);
            task_wait(task);
            return;
        }
    }

    printf("Command not found: %s\n", task_ctx.argv[0]);
}

int shell_main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    while (1) {
        printf("%s", SHELL_PROMPT);
        shell_readline(buffer, COMMAND_LINE_MAX_SIZE);
        shell_history_add(buffer);
        shell_history_rewind();
        shell_process_line(buffer);
    }
}
