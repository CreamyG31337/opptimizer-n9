/* oppldr.c
 * Copyright (c) 2012 itsnotabigtruck.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this package. If not, see http://www.gnu.org/licenses/.
 */

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/creds.h>
#include <sys/wait.h>

/*
 * Local definitions
 */

#define OPP_DEVNULL         "/dev/null"
#define OPP_MODPROBE        "/sbin/modprobe"
#define OPP_SHA1_LENGTH     20
#define OPP_TARGET_MODULE   "opptimizer"
#define OPP_WHITELIST_PATH  "/sys/kernel/security/validator/modlist"
#include "modhash.inc"

/*
 * Declarations
 */

static int opp_confine_to_sys_module(void);
static int opp_invoke(const char *tgt, char *args[]);
static int opp_whitelist_module(const void *hash);

/*
 * Support functions
 */

static int opp_confine_to_sys_module(void)
{
    creds_t cr;
    long rv;
    creds_value_t val;

    /* Remove all credentials except CAP::sys_module */
    cr = creds_init();
    rv = creds_str2creds("CAP::sys_module", &val);
    if (rv == CREDS_BAD)
        return -EPERM;
    if (creds_add(&cr, (creds_type_t)rv, val) != 0)
        return -ENOMEM;
    if (creds_set(cr) < 0)
        return -errno;
    return 0;
}

static int opp_invoke(const char *tgt, char *args[])
{
    posix_spawn_file_actions_t act;
    pid_t pid;
    int st;
    int rv;

    /* Invoke the specified process */
    rv = posix_spawn_file_actions_init(&act);
    if (rv != 0)
        return -rv;
    rv = -posix_spawn_file_actions_addopen(&act, STDOUT_FILENO,
        OPP_DEVNULL, O_WRONLY, 0);
    if (rv != 0)
        goto fault;
    rv = -posix_spawn_file_actions_adddup2(&act, STDOUT_FILENO,
        STDERR_FILENO);
    if (rv != 0)
        goto fault;
    rv = -posix_spawn(&pid, tgt, &act, NULL, args, environ);
    posix_spawn_file_actions_destroy(&act);
    if (rv != 0)
        goto fault;

    /* Wait for the process to complete */
    for (; ; ) {
        if (waitpid(pid, &st, 0) == -1) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        return WEXITSTATUS(st);
    }

    /* Handle errors */
fault:
    posix_spawn_file_actions_destroy(&act);
    return rv;
}

static int opp_whitelist_module(const void *hash)
{
    int fd = -1;
    ssize_t wres;
    int rv = 0;

    /* Append the specified hash to the kernel module whitelist */
    while (fd == -1) {
        fd = open(OPP_WHITELIST_PATH, O_WRONLY);
        if (fd == -1 && errno != EINTR)
            return -errno;
    }
    wres = write(fd, hash, OPP_SHA1_LENGTH);
    if (wres < 0)
        rv = -errno;
    else if (wres != OPP_SHA1_LENGTH)
        rv = -EINTR;
    close(fd);
    return rv;
}

/*
 * Entry point
 */

int main(int argc, char *argv[])
{
    const char *appName = program_invocation_short_name;
    char modName[] = OPP_TARGET_MODULE;
    char *args[3];
    int rv;
    struct stat st;

    /* Test if we need to do anything at all */
    if (stat("/proc/opptimizer", &st) == 0)
        return 0;
    rv = -errno;
    if (rv != -ENOENT)
        goto fault;

    /* Add the modules of interest to the list */
    rv = opp_whitelist_module(OPP_HASH_SYMSEARCH);
    if (rv >= 0)
        rv = opp_whitelist_module(OPP_HASH_OPPTIMIZER);
    if (rv < 0)
        goto fault;

    /* Drop credentials so it's safe to exec things */
    rv = opp_confine_to_sys_module();
    if (rv < 0)
        goto fault;

    /* Load the required module */
    args[0] = OPP_MODPROBE;
    args[1] = modName;
    args[2] = NULL;
    rv = opp_invoke(args[0], args);
    if (rv < 0)
        goto fault;
    if (rv > 0) {
        fprintf(stderr, "%s: modprobe returned error %i\n", appName, rv);
        return 1;
    }

    /* Handle errors */
    return 0;
fault:
    fprintf(stderr, "%s: %s\n", appName, strerror(-rv));
    return 1;
}
