/* **********************************************************
 * Copyright (c) 2015 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "named_pipe.h"

// FIXME i#1703: add support for DR-private fd's by passing in a function for ::open?
// Need to add O_WRONLY support via DR_FILE_WRITE_ONLY to dr_open_file() first.
// Xref i#498 where we'd want to add named pipe routines directly to DR.
// We could try to turn this code into a DR extension, maybe add to drx?

// XXX: should read from /proc/sys/fs/pipe-max-size instead of hardcoding here.
// This is the max size an unprivileged process can request.
#define PIPE_BUF_MAX_SIZE 1048576

#define PIPE_PERMS 0666

named_pipe_t::~named_pipe_t()
{
    close();
}

static const char *
pipe_dir()
{
    // FIXME i#1703: check TMPDIR, TEMP, and TMP env vars first.
    return "/tmp";
}

named_pipe_t::named_pipe_t() :
    fd(-1)
{
    // empty
}

// We avoid extra string copies by constructing with the name where possible.
named_pipe_t::named_pipe_t(const char *name) :
    fd(-1)
{
    set_name(name); // guaranteed to succeed
}

// We provide this model for global vars and other instances.
bool
named_pipe_t::set_name(const char *name)
{
    if (fd == -1) {
        pipe_name = std::string(std::string(pipe_dir()) + "/" + name);
        return true;
    }
    return false;
}

bool
named_pipe_t::create()
{
    umask(0);
    if (mkfifo(pipe_name.c_str(), PIPE_PERMS) != 0)
        return false;
    return true;
}

bool
named_pipe_t::destroy()
{
    close();
    return (unlink(pipe_name.c_str()) == 0);
}

bool
named_pipe_t::open_for_write()
{
    // This should block until a reader connects
    fd = ::open(pipe_name.c_str(), O_WRONLY);
    if (fd < 0)
        return false;
    return true;
}

bool
named_pipe_t::open_for_read()
{
    // XXX: we may want to add optional nonblocking support via O_NONBLOCK here,
    // or maybe better via fcntl to keep separate from swapping in dr_open_file().
    fd = ::open(pipe_name.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    return true;
}

bool
named_pipe_t::close()
{
    if (fd != -1)
        ::close(fd);
    fd = -1;
    return true;
}

bool
named_pipe_t::maximize_buffer()
{
    return fcntl(fd, F_SETPIPE_SZ, PIPE_BUF_MAX_SIZE) == PIPE_BUF_MAX_SIZE;
}

const std::string &
named_pipe_t::get_pipe_path() const
{
    return pipe_name;
}

bool
named_pipe_t::set_fd(int fd_)
{
    // Not allowed to clobber an existing fd!
    if (fd == -1) {
        fd = fd_;
        return true;
    }
    return false;
}

ssize_t
named_pipe_t::read(void *buf OUT, size_t sz)
{
    int res = -1;
    while (true) {
        res = ::read(fd, buf, sz);
        if (res == -1 && errno == EINTR)
            continue;
        break;
    }
    // XXX: if we add nonblocking support we'll need to distinguish 0 (EOF)
    // from no data (-1 w/ EAGAIN).
    // Seems cleanest for a portable interface to swap them: 0 means no data
    // but pipe is still there, negative means EOF or something is wrong.
    if (res == 0)
        return -1;
    return res;
}

ssize_t
named_pipe_t::write(const void *buf IN, size_t sz)
{
    int res = -1;
    while (true) {
        res = ::write(fd, buf, sz);
        if (res == -1 && errno == EINTR)
            continue;
        break;
    }
    return res;
}
