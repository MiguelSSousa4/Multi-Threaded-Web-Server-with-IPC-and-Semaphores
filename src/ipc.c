#include "ipc.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stddef.h>
#include <string.h>

int send_fd(int socket, int fd_to_send)
{
    struct msghdr msg = {0};
    // FIX 1: Initialize this buffer to 0
    char buf[1] = {0}; 
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    union
    {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } u;
    
    // FIX 2: Zero out the entire union memory
    memset(&u, 0, sizeof(u)); 

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *)CMSG_DATA(cmsg)) = fd_to_send;

    return sendmsg(socket, &msg, 0);
}

int recv_fd(int socket)
{
    struct msghdr msg = {0};
    // FIX 3: Initialize here as well for safety
    char buf[1] = {0};
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    union
    {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } u;

    // FIX 4: Zero out memory here too
    memset(&u, 0, sizeof(u));

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    if (recvmsg(socket, &msg, 0) < 0)
        return -1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        return *((int *)CMSG_DATA(cmsg));
    }
    return -1;
}