/*
 * Copyright (C) 2006 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
/*
 *
 * Description : Will grab locks and hold them to add some stress to
 *               the OCFS2 DLM.
 * Author      : Joel Becker
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <getopt.h>

#include <o2dlm/o2dlm.h>

#define PROGNAME "lock_grab"
#define DEFAULT_DLMFS_PATH "/dlm"
#define DEFAULT_DLMFS_DOMAIN PROGNAME

/* How many EX trylocks before we go back to the main loop */
#define LOCK_GRAB_MAX_EX_TRIES 10


/* What direction is our next operation */
enum lg_direction {
    LG_DIRECTION_UNLOCK,
    LG_DIRECTION_LOCK,
};

struct lg_context
{
    int lg_num_locks;           /* How many locks in the lockspace.
                                   So, if this number is 1000, the
                                   names of the locks are "1" through
                                   "999" */
    int lg_min_held;            /* If we hold fewer than min_held, we
                                   must take locks on the next pass */
    int lg_max_held;            /* If we hold more than max_held, we
                                   must release locks on the next
                                   pass */
    int lg_cur_held;            /* How many locks we currently hold */
    FILE *lg_urandom;           /* FILE for /dev/urandom */
    char **lg_held;             /* Names of the currently held locks */
    char **lg_free;             /* Names of currently unheld locks */
    struct o2dlm_ctxt *lg_dlm;  /* DLM context */
};

sig_atomic_t caught_sig = 0;


void handler(int signum)
{
    caught_sig = signum;
}

static int setup_signals()
{
    int rc = 0;
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = NULL;
    sigemptyset(&act.sa_mask);
    act.sa_handler = handler;
#ifdef SA_INTERRUPT
    act.sa_flags = SA_INTERRUPT;
#endif

    rc += sigaction(SIGHUP, &act, NULL);
    rc += sigaction(SIGTERM, &act, NULL);
    rc += sigaction(SIGINT, &act, NULL);
    act.sa_handler = SIG_IGN;
    rc += sigaction(SIGPIPE, &act, NULL);  /* Get EPIPE instead */
    
    return rc;
}

/*
 * The random byte is used in two ways.  First, as a heads/tails choice
 * for things like "lock or unlock".  Second, it chooses how many locks
 * we can lock or unlock at a time.  While the character size bounds
 * this at 255, do we really care to lock more than 255 locks at once?
 * I didn't think so.  If we happen to return 0, that's fine, it's a
 * no-op pass that allows other nodes to do something.
 */
static int get_random_byte(struct lg_context *lgc)
{
    int c;

    c = fgetc(lgc->lg_urandom);
    if (c == EOF){
        /* EOF is an error for us */
        fprintf(stderr, "%s: Error reading from /dev/urandom\n",
                PROGNAME);
        return -1;
    }

    return c;
}

static int choose_mode(struct lg_context *lgc)
{
    int rval;

    /*
     * EXMODE always locks then unlocks.  You can't lock if there
     * are no free locks!
     */
    if (lgc->lg_cur_held == lgc->lg_num_locks)
        return O2DLM_LEVEL_PRMODE;

    rval = get_random_byte(lgc);
    if (rval < 0)
        return rval;

    /*
     * Basically, split the random space in two.  Heads you EX,
     * tails you PR.  But let's bias towards PR.
     */
    return (rval > 200) ? O2DLM_LEVEL_EXMODE : O2DLM_LEVEL_PRMODE;
}

static int choose_direction(struct lg_context *lgc)
{
    int rval;

    if (lgc->lg_cur_held >= lgc->lg_max_held)
        return LG_DIRECTION_UNLOCK;
    if (lgc->lg_cur_held <= lgc->lg_min_held)
        return LG_DIRECTION_LOCK;

    rval = get_random_byte(lgc);
    if (rval < 0)
        return rval;

    /*
     * Basically, split the random space in two.  Heads you lock,
     * tails you unlock.
     */
    return (rval > 127) ? LG_DIRECTION_UNLOCK : LG_DIRECTION_LOCK;
}

static int locks_this_pass(struct lg_context *lgc, int direction)
{
    int rval;

    rval = get_random_byte(lgc);
    if (rval < 0)
        return rval;

    if (direction == LG_DIRECTION_LOCK)
    {
        /* We can't take more locks than are left in the lock space */
        if ((lgc->lg_num_locks - lgc->lg_cur_held) < rval)
            rval = rval % (lgc->lg_num_locks - lgc->lg_cur_held);
    }
    else if (direction == LG_DIRECTION_UNLOCK)
    {
        /* We can't release more locks than we hold */
        if (rval > lgc->lg_cur_held)
            rval = rval % lgc->lg_cur_held;
    }
    else
        abort();

    return rval;
}

static int build_locklist(struct lg_context *lgc)
{
    int strsize, i;
    char strbuf[21]; /* Big enough for 2^64, though we don't care */

    lgc->lg_held = malloc(lgc->lg_num_locks * sizeof(char *));
    if (!lgc->lg_held)
        return -ENOMEM;
    memset(lgc->lg_held, 0, lgc->lg_num_locks * sizeof(char *));


    lgc->lg_free = malloc(sizeof(char *) * lgc->lg_num_locks);
    if (!lgc->lg_free)
        return -ENOMEM;
    memset(lgc->lg_free, 0, lgc->lg_num_locks * sizeof(char *));

    /* We know strbuf can handle an int find, don't check bounds :-) */
    snprintf(strbuf, sizeof(strbuf), "%d", lgc->lg_num_locks - 1);
    strsize = strlen(strbuf) + 1;

    for (i = 0; i < lgc->lg_num_locks; i++)
    {
        lgc->lg_free[i] = malloc(strsize * sizeof(char));
        if (!lgc->lg_free[i])
            return -ENOMEM;
        snprintf(lgc->lg_free[i], strsize, "%0*d", strsize - 1, i);
    }
    
    return 0;
}

static void free_locklist(struct lg_context *lgc)
{
    int i;

    for (i = 0; i < lgc->lg_num_locks; i++)
    {
        if (lgc->lg_held && lgc->lg_held[i])
            free(lgc->lg_held[i]);
        if (lgc->lg_free && lgc->lg_free[i])
            free(lgc->lg_free[i]);
    }

    if (lgc->lg_held)
        free(lgc->lg_held);
    if (lgc->lg_free)
        free(lgc->lg_free);
}

static void shift_lock(struct lg_context *lgc, int direction, int num)
{
    int held_count = lgc->lg_cur_held;
    int free_count = lgc->lg_num_locks - held_count;
    int source_count, dest_count;
    char **source_list, **dest_list;

    if (direction == LG_DIRECTION_LOCK)
    {
        source_list = lgc->lg_free;
        dest_list = lgc->lg_held;
        source_count = free_count;
        dest_count = held_count;

        lgc->lg_cur_held += 1;
    }
    else if (direction == LG_DIRECTION_UNLOCK)
    {
        source_list = lgc->lg_held;
        dest_list = lgc->lg_free;
        source_count = held_count;
        dest_count = free_count;

        lgc->lg_cur_held -= 1;
    }
    else
        abort();

    /* The lists are the size of the total lock count; don't go over */
    if (dest_count == lgc->lg_num_locks)
        abort();

    /* First add the lock to the dest */
    dest_list[dest_count] = source_list[num];

    /* Now shift the source down */
    if (num < (source_count - 1))
#if 1
        memmove(source_list + num, source_list + num + 1,
                (source_count - num - 1) * sizeof(char *));
#else 
    {
        fprintf(stdout, "Shifting %p slot %d: %p, %p, %d\n",
                source_list, num, source_list + num,
                source_list + num + 1,
                (source_count - num - 1) * sizeof(char *));
        memmove(source_list + num, source_list + num + 1,
                (source_count - num - 1) * sizeof(char *));
    }
    else
        fprintf(stdout, "No shift, num = %d, source_count = %d\n", num,
                source_count);
#endif

    source_list[source_count - 1] = NULL;
}

static int pick_lock(struct lg_context *lgc, int direction)
{
    int count, ret;
    unsigned int randval;

    ret = fread(&randval, sizeof(randval), 1, lgc->lg_urandom);
    if (ret < 1)
        return -EIO;

    /*
     * Define the bounds of our random space
     * If we're locking, it's zero to the number of free locks.
     * If we're unlocking, it's zero to the number of held locks.
     */
    if (direction == LG_DIRECTION_LOCK)
        count = lgc->lg_num_locks - lgc->lg_cur_held;
    else if (direction == LG_DIRECTION_UNLOCK)
        count = lgc->lg_cur_held;
    else
        abort();

    return randval % count;
}

static int do_one_ex_lock(struct lg_context *lgc)
{
    int num, stime, tries = 0;
    errcode_t err;
    char *lock_name;

    fprintf(stdout, "Trying to EX lock lockid ");
    do
    {
        num = pick_lock(lgc, LG_DIRECTION_LOCK);
        if (num < 0)
            return num;

        lock_name = lgc->lg_free[num];
        fprintf(stdout, "%s ", lock_name);
        fflush(stdout);
        err = o2dlm_lock(lgc->lg_dlm, lock_name, O2DLM_TRYLOCK,
                         O2DLM_LEVEL_EXMODE);
        tries++;
    } while ((err == O2DLM_ET_TRYLOCK_FAILED) &&
             (tries < LOCK_GRAB_MAX_EX_TRIES));

    if (err)
    {
        if (err != O2DLM_ET_TRYLOCK_FAILED)
        {
            fprintf(stdout, "failed\n");
            com_err(PROGNAME, err, "while trying to EX lock lockid %s",
                    lock_name);
            return -EIO;
        }
        else if (tries >= LOCK_GRAB_MAX_EX_TRIES) 
        {
            fprintf(stdout, "giving up\n");
            return 0;
        }
        else
            abort();
    }

    fprintf(stdout, "taken... ");
    fflush(stdout);

    stime = get_random_byte(lgc);
    if (stime > 0)
        sleep(stime * 30 / 1000);

    err = o2dlm_unlock(lgc->lg_dlm, lock_name);
    fprintf(stdout, "%s\n", err ? "failed" : "dropped");
    if (err)
    {
        com_err(PROGNAME, err,
                "while trying to drop EX lock lockid %s",
                lock_name);
        return -EIO;
    }

    return 0;
}

static int do_one_pr_lock(struct lg_context *lgc, int direction)
{
    int num;
    errcode_t err;
    char *lock_name;

    num = pick_lock(lgc, direction);
    if (num < 0)
        return num;

    if (direction == LG_DIRECTION_LOCK)
    {
        lock_name = lgc->lg_free[num];
        fprintf(stdout, "%s ", lock_name);
        fflush(stdout);
        err = o2dlm_lock(lgc->lg_dlm, lock_name, 0, O2DLM_LEVEL_PRMODE);
        if (err)
            fprintf(stdout, "failed\n");
    }
    else if (direction == LG_DIRECTION_UNLOCK)
    {
        lock_name = lgc->lg_held[num];
        fprintf(stdout, "%s ", lock_name);
        fflush(stdout);
        err = o2dlm_unlock(lgc->lg_dlm, lock_name);
        if (err)
            fprintf(stdout, "failed\n");
    }
    else
        abort();

    if (err)
    {
        com_err(PROGNAME, err,
                "while trying to %sPR lock lockid %s",
                (direction == LG_DIRECTION_UNLOCK) ? "drop " : "",
                lock_name);
        return -EIO;
    }

    shift_lock(lgc, direction, num);

    return 0;
}

static int run(struct lg_context *lgc)
{
    int ret, mode, direction, this_pass, i;
    
    ret = setup_signals();
    if (ret)
        return ret;

    while (!caught_sig && !ret)
    {
        mode = choose_mode(lgc);
        if (mode < 0)
        {
            ret = mode;
            break;
        }

        if (mode == O2DLM_LEVEL_EXMODE)
        {
            /* We only take EX locks one at a time */
            ret = do_one_ex_lock(lgc);
            continue;
        }

        /* We're taking PR locks */

        direction = choose_direction(lgc);
        if (direction < 0)
        {
            ret = direction;
            break;
        }

        this_pass = locks_this_pass(lgc, direction);
        if (this_pass < 0)
        {
            ret = this_pass;
            break;
        }

        fprintf(stdout,
                (direction == LG_DIRECTION_LOCK) ?
                "PR locking %d lockid(s): " :
                "Dropping PR lock for %d lockid(s): ",
                this_pass);
        fflush(stdout);
        for (i = 0; i < this_pass; i++)
        {
            ret = do_one_pr_lock(lgc, direction);
            if (ret)
                break;
            if (caught_sig)
                break;
        }
        if (!ret)
            fprintf(stdout, "done\n");
    }

    return ret;
}

static void print_usage(int rc)
{
    FILE *output = rc ? stderr : stdout;

    fprintf(output,
            "Usage: %s [-c <num_locks>] [-L <min_held>] [-H <max_held>]\n",
            PROGNAME);

    exit(rc);
}

extern int optopt;
extern int opterr;
extern int optind;
static int parse_args(int argc, char *argv[], struct lg_context *lgc)
{
    int c, tmp;
    int count_set = 0, max_set = 0, min_set = 0;

    opterr = 0;
    while ((c = getopt(argc, argv, ":hc:L:H:-:")) != EOF)
    {
        switch (c)
        {
            case 'h':
                print_usage(0);
                break;

            case '-':
                if (!strcmp(optarg, "help"))
                    print_usage(0);
                else
                {
                    fprintf(stderr, "%s: Invalid option: \'--%s\'\n",
                            PROGNAME, optarg);
                    return -EINVAL;
                }
                break;

            case 'c':
                tmp = atoi(optarg);
                if (!tmp) {
                    fprintf(stderr, "%s: Invalid lock count: %s\n",
                            PROGNAME, optarg);
                    return -EINVAL;
                }
                lgc->lg_num_locks = tmp;
                count_set = 1;
                break;

            case 'L':
                tmp = atoi(optarg);
                if (!tmp) {
                    fprintf(stderr, "%s: Invalid min held: %s\n",
                            PROGNAME, optarg);
                    return -EINVAL;
                }
                lgc->lg_min_held = tmp;
                min_set = 1;
                break;

            case 'H':
                tmp = atoi(optarg);
                if (!tmp) {
                    fprintf(stderr, "%s: Invalid max held: %s\n",
                            PROGNAME, optarg);
                    return -EINVAL;
                }
                lgc->lg_max_held = tmp;
                max_set = 1;
                break;

            case '?':
                fprintf(stderr, "%s: Invalid option: \'-%c\'\n",
                        PROGNAME, optopt);
                return -EINVAL;
                break;

            case ':':
                fprintf(stderr, "%s: Option \'-%c\' requires an argument\n",
                        PROGNAME, optopt);
                return -EINVAL;
                break;

            default:
                fprintf(stderr, "%s: Shouldn't get here %c %c\n",
                        PROGNAME, optopt, c);
                break;
        }
    }

    /* First verify the stupid errors */

    if (optind < argc)
    {
        fprintf(stderr, "%s: Extraneous arguments\n", PROGNAME);
        return -EINVAL;
    }

    if (lgc->lg_num_locks < 1)
    {
        fprintf(stderr, "%s: Lock count must be greater than zero\n",
                PROGNAME);
        return -EINVAL;
    }

    if (lgc->lg_min_held < 0)
    {
        fprintf(stderr, "%s: Min held cannot be less than zero\n",
                PROGNAME);
        return -EINVAL;
    }

    if (lgc->lg_max_held < 1)
    {
        fprintf(stderr, "%s: Max held cannot be less than one\n",
                PROGNAME);
        return -EINVAL;
    }

    /* Now cleanup any defaults that don't match user input */

    if (min_set && !max_set &&
        (lgc->lg_min_held > lgc->lg_max_held))
        lgc->lg_max_held = lgc->lg_min_held + 1;

    if (count_set && !max_set &&
        (lgc->lg_max_held > lgc->lg_num_locks))
        lgc->lg_max_held = lgc->lg_num_locks;

    if (!count_set &&
        (lgc->lg_max_held > lgc->lg_num_locks))
        lgc->lg_num_locks = lgc->lg_max_held;

    
    /* Finally, verify that user input makes sense */

    if (lgc->lg_max_held <= lgc->lg_min_held)
    {
        fprintf(stderr, "%s: Max held must be bigger than min held\n",
                PROGNAME);
        return -EINVAL;
    }

    if (lgc->lg_min_held >= lgc->lg_num_locks)
    {
        fprintf(stderr,
                "%s: Min held must be less than the total lock count\n",
                PROGNAME);
        return -EINVAL;
    }

    if (lgc->lg_max_held > lgc->lg_num_locks)
    {
        fprintf(stderr,
                "%s: Max held cannot be greater than the total lock count\n",
                PROGNAME);
        return -EINVAL;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    errcode_t err;
    struct lg_context lgc = {
        .lg_num_locks   = 100,
        .lg_min_held    = 0,
        .lg_max_held    = 20,
    }; 

    ret = parse_args(argc, argv, &lgc);
    if (ret)
        print_usage(ret);

    fprintf(stdout,
            "%s: Lock space of %d locks.  High water is %d locks, low water is %d.\n",
            PROGNAME, lgc.lg_num_locks, lgc.lg_max_held,
            lgc.lg_min_held);

    ret = build_locklist(&lgc);
    if (ret)
        goto out_free;

    initialize_o2dl_error_table();

    err = o2dlm_initialize(DEFAULT_DLMFS_PATH, DEFAULT_DLMFS_DOMAIN,
                           &lgc.lg_dlm);
    if (err) {
        com_err(PROGNAME, err, "while initializing dlmfs domain %s",
                DEFAULT_DLMFS_DOMAIN);
        ret = -ENOSYS;
        goto out_free;
    }

    lgc.lg_urandom = fopen("/dev/urandom", "r");
    if (!lgc.lg_urandom)
    {
        fprintf(stderr, "%s: Error opening /dev/urandom: %s\n",
                PROGNAME, strerror(errno));
        ret = -EIO;
        goto out_destroy;
    }

    ret = run(&lgc);

    fclose(lgc.lg_urandom);

out_destroy:
    err = o2dlm_destroy(lgc.lg_dlm);
    if (err) {
        com_err(PROGNAME, err,
                "while disconnecting from dlmfs domain %s",
                DEFAULT_DLMFS_DOMAIN);
        if (!ret)
            ret = -EINVAL;
    }

out_free:
    free_locklist(&lgc);

    return ret;
}
