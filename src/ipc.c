#include "ipc.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stddef.h>
#include <string.h>

/*
 * Send a File Descriptor via UNIX Domain Socket
 * Purpose: Transmits an open file descriptor from the current process to another
 * process. This is required because file descriptors are process-local integers;
 * simply passing the integer value (e.g., '5') to another process is invalid.
 * We must use SCM_RIGHTS (Socket Control Message) to instruct the kernel to
 * duplicate the FD into the receiving process's file table.
 *
 * Parameters:
 * - socket: The UNIX domain socket (IPC channel) to send through.
 * - fd_to_send: The file descriptor to transfer.
 *
 * Return:
 * - Number of bytes sent on success (usually 1).
 * - -1 on failure.
 */
int send_fd(int socket, int fd_to_send)
{
    struct msghdr msg = {0};

    /* Dummy data buffer. sendmsg requires at least one byte of real data
     * to be sent along with the ancillary (control) data.
     */
    char buf[1] = {0}; 
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    /* Union ensures proper memory alignment for the control message buffer */
    union
    {
        char buf[CMSG_SPACE(sizeof(int))]; /* Buffer big enough for one int FD */
        struct cmsghdr align;              /* Enforce alignment */
    } u;
    
    /* Zero out the control buffer to prevent garbage data */
    memset(&u, 0, sizeof(u)); 

    /* Setup message header */
    msg.msg_iov = &io;          /* Point to dummy data */
    msg.msg_iovlen = 1;         /* Number of I/O vectors */
    msg.msg_control = u.buf;    /* Point to control buffer */
    msg.msg_controllen = sizeof(u.buf); /* Size of control buffer */

    /* Configure the Ancillary Data (Control Message) Header */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;  /* Socket-level protocol */
    cmsg->cmsg_type = SCM_RIGHTS;   /* We are sending access rights (FDs) */
    cmsg->cmsg_len = CMSG_LEN(sizeof(int)); /* Length of data + header */

    /* Copy the file descriptor into the data portion of the control message */
    *((int *)CMSG_DATA(cmsg)) = fd_to_send;

    return sendmsg(socket, &msg, 0);
}

/*
 * Receive a File Descriptor via UNIX Domain Socket
 * Purpose: Receives a file descriptor sent by another process. The kernel
 * will automatically add the FD to this process's file table and return
 * its new integer value via the ancillary data.
 *
 * Parameters:
 * - socket: The UNIX domain socket to receive from.
 *
 * Return:
 * - The new valid file descriptor on success.
 * - -1 on failure (recvmsg error or no FD received).
 */
int recv_fd(int socket)
{
    struct msghdr msg = {0};

    /* Prepare buffer for the dummy byte */
    char buf[1] = {0};
    struct iovec io = {.iov_base = buf, .iov_len = 1};

    /* Union for alignment of receiving buffer */
    union
    {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } u;

    memset(&u, 0, sizeof(u));

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = u.buf;
    msg.msg_controllen = sizeof(u.buf);

    /* Perform the receive operation */
    if (recvmsg(socket, &msg, 0) < 0)
        return -1;

    /* Extract the FD from the ancillary data */
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    
    /* Verify we received the expected type of message (SCM_RIGHTS) */
    if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
    {
        /* Return the file descriptor integer */
        return *((int *)CMSG_DATA(cmsg));
    }
    
    return -1; /* Failed to receive a valid FD */
}