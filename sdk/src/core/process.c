#include <dino/core/process.h>

#include <stdlib.h>
#include <string.h>

#include <dino/core/time.h>

#include "error_internal.h"
#include "pipe_internal.h"
#include "process_internal.h"
#include "thread_internal.h"

/* ---- Run to completion ---- */

// One thread per output pipe and one for the input: the event loop can't wait
// on a pipe's write end, and reading everything at once is what keeps a child that fills one pipe
// while the parent waits on another from deadlocking.
//
// Every thread stops at the run's deadline (0: none), not only the child: a
// process the child started can hold the pipes open long after it exits.

// Milliseconds until deadline_ns, 0 once past, -1 without a deadline.
static int left_ms(uint64_t deadline_ns)
{
    if (!deadline_ns)
        return -1;
    uint64_t now = dino_time_monotonic_ns();
    return now >= deadline_ns ? 0 : (int)((deadline_ns - now + 999999) / 1000000);
}

struct collector
{
    dino_pipe_t pipe;
    uint64_t    deadline;
    size_t      max; // 0: no limit
    char       *data;
    size_t      len, cap;
    int         error; // why it failed, from this thread's dino_error()
    int         timed_out;
    int         truncated;
};

// At the deadline, take what is already waiting, up to one pipe's worth.
#define FINAL_DRAIN 65536

static int collect(void *arg)
{
    struct collector *c = arg;
    char              chunk[4096];
    size_t            drained = 0;
    for (;;)
    {
        if (c->deadline)
        {
            int rc = dino_pipe_wait(c->pipe, c->timed_out ? 0 : left_ms(c->deadline));
            if (rc == DINO_TIMEOUT && !c->timed_out)
            {
                c->timed_out = 1;
                rc           = dino_pipe_wait(c->pipe, 0);
            }
            if (rc == -1)
                goto broken;
            if (rc != 0 || (c->timed_out && drained >= FINAL_DRAIN))
                break;
        }
        int n = dino_pipe_read(c->pipe, chunk, sizeof(chunk));
        if (n < 0)
            goto broken;
        if (n == 0)
            break;
        if (c->timed_out)
            drained += (size_t)n;
        // Past the limit, or out of memory, it keeps draining, so the child
        // isn't blocked on a full pipe.
        if (c->error)
            continue;
        if (c->max && c->len + (size_t)n > c->max)
        {
            n            = (int)(c->max - c->len);
            c->truncated = 1;
        }
        if (n == 0)
            continue;
        if (c->len + (size_t)n + 1 > c->cap)
        {
            size_t cap = (c->len + (size_t)n + 1) * 2;
            if (c->max && cap > c->max + 1)
                cap = c->max + 1;
            char *data = realloc(c->data, cap);
            if (!data)
            {
                c->error = DINO_ENOMEM;
                continue;
            }
            c->data = data;
            c->cap  = cap;
        }
        memcpy(c->data + c->len, chunk, (size_t)n);
        c->len += (size_t)n;
        c->data[c->len] = '\0';
    }
    return 0;

broken:
    // Nothing reads this pipe any more: closing it makes the child's writes
    // fail instead of blocking it forever on a full pipe.
    c->error = dino_error();
    dino_pipe_close(c->pipe);
    c->pipe = DINO_INVALID_PIPE;
    return 0;
}

struct feeder
{
    dino_pipe_t pipe;
    uint64_t    deadline;
    const char *data;
    size_t      len;
    int         timed_out;
};

static int feed(void *arg)
{
    struct feeder *f = arg;
    // Writes fail instead of raising SIGPIPE when the child has stopped reading.
    for (size_t sent = 0; sent < f->len;)
    {
        size_t chunk = f->len - sent < 65536 ? f->len - sent : 65536;
        int    n     = 0;
        int rc = dino__pipe_write_timeout(f->pipe, f->data + sent, chunk, left_ms(f->deadline), &n);
        if (rc == DINO_TIMEOUT)
            f->timed_out = 1;
        if (rc != 0 || n <= 0)
            break;
        sent += (size_t)n;
    }
    dino_pipe_close(f->pipe);
    return 0;
}

static char *take(struct collector *c, size_t *len)
{
    char *data = c->data ? c->data : calloc(1, 1);
    *len       = c->data ? c->len : 0;
    c->data    = NULL;
    return data;
}

int dino_process_run(
    const char *const argv[], const dino_process_run_opts_t *opts, dino_process_result_t *result)
{
    if (!result)
        return dino__fail(DINO_EINVAL);
    memset(result, 0, sizeof(*result));
    result->status.code = -1;

    dino_process_spawn_opts_t spawn = { 0 };
    spawn.cwd                       = opts ? opts->cwd : NULL;
    spawn.env                       = opts ? opts->env : NULL;
    spawn.stdin_mode                = opts && opts->input ? DINO_STDIO_PIPE : DINO_STDIO_NULL;
    spawn.stdout_mode               = DINO_STDIO_PIPE;
    spawn.stderr_mode               = DINO_STDIO_PIPE;

    dino_process_t       process;
    dino_process_pipes_t pipes;
    if (dino_process_spawn(&process, argv, &spawn, &pipes) != 0)
        return -1;

    int      timeout     = opts && opts->timeout_ms > 0 ? opts->timeout_ms : -1;
    uint64_t deadline    = timeout > 0 ? dino_time_monotonic_ns() + (uint64_t)timeout * 1000000 : 0;
    size_t   max         = opts ? opts->max_output : 0;
    struct collector out = { pipes.out, deadline, max, NULL, 0, 0, 0, 0, 0 };
    struct collector err = { pipes.err, deadline, max, NULL, 0, 0, 0, 0, 0 };
    struct feeder    in
        = { pipes.in, deadline, opts ? opts->input : NULL, opts ? opts->input_len : 0, 0 };
    dino__thread_t threads[3]
        = { DINO__INVALID_THREAD, DINO__INVALID_THREAD, DINO__INVALID_THREAD };
    int started = dino__thread_create(&threads[0], collect, &out) == 0
        && dino__thread_create(&threads[1], collect, &err) == 0
        && (in.pipe == DINO_INVALID_PIPE || dino__thread_create(&threads[2], feed, &in) == 0);

    // Why the run failed, kept from the call that failed: the calls that
    // follow it here succeed or fail for reasons of their own, and the reader
    // threads' failures happen in those threads.
    int status = 0, reason = 0;
    if (!started)
    {
        reason = dino_error();
        dino_process_kill(process);
        status = -1;
    }
    int rc = dino_process_wait_timeout(process, timeout, &result->status);
    if (rc == DINO_TIMEOUT)
    {
        dino_process_kill(process);
        rc     = dino_process_wait(process, &result->status);
        status = status ? status : DINO_TIMEOUT;
    }
    if (rc != 0)
    {
        reason = reason ? reason : dino_error();
        status = -1;
    }

    // The child is gone, so the readers reach end of file and the writer's next
    // write fails, unless something the child started still holds the pipes:
    // then they stop at the deadline.
    for (int i = 0; i < 3; i++)
        if (threads[i] != DINO__INVALID_THREAD)
            dino__thread_join(threads[i], NULL);
    if (status == 0 && (out.timed_out || err.timed_out || in.timed_out))
        status = DINO_TIMEOUT;
    if (in.pipe != DINO_INVALID_PIPE && threads[2] == DINO__INVALID_THREAD)
        dino_pipe_close(in.pipe);
    if (out.pipe != DINO_INVALID_PIPE)
        dino_pipe_close(out.pipe);
    if (err.pipe != DINO_INVALID_PIPE)
        dino_pipe_close(err.pipe);

    result->out       = take(&out, &result->out_len);
    result->err       = take(&err, &result->err_len);
    result->truncated = out.truncated || err.truncated;
    int lost          = out.error ? out.error : err.error;
    if (lost || !result->out || !result->err)
    {
        reason = reason ? reason : lost ? lost : DINO_ENOMEM;
        status = -1;
    }
    return status == -1 ? dino__fail(reason) : status;
}

void dino_process_result_free(dino_process_result_t *result)
{
    if (!result)
        return;
    free(result->out);
    free(result->err);
    memset(result, 0, sizeof(*result));
}
