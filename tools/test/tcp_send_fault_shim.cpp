#include <sys/socket.h>
#include <linux/errqueue.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>

// Loaded only by the diagnostic smoke client; never a production link dependency.
extern "C" ssize_t sendmsg(int fd,const msghdr* message,int flags) {
    using Function=ssize_t(*)(int,const msghdr*,int);
    static auto real=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"sendmsg"));
    static std::atomic<unsigned> calls{0};
    const auto* fault=std::getenv("CPNETFLUX_TCP_SEND_TEST_FAULT");
    if (!fault || !(flags&MSG_ZEROCOPY)) return real(fd,message,flags);
    const auto call=++calls;
    if (!std::strcmp(fault,"retry")) {
        if (call==1) { errno=EINTR;return -1; }
        if (call==2) { errno=EAGAIN;return -1; }
        if (call==3) {
            auto copy=*message;std::array<iovec,3> spans{};std::size_t left=4093,count=0;
            for (std::size_t i=0;i<message->msg_iovlen && left;++i) {
                spans[count]=message->msg_iov[i];spans[count].iov_len=std::min(left,spans[count].iov_len);
                left-=spans[count++].iov_len;
            }
            copy.msg_iov=spans.data();copy.msg_iovlen=count;return real(fd,&copy,flags);
        }
    }
    if (call==1) {
        if (!std::strcmp(fault,"zero")) return 0;
        if (!std::strcmp(fault,"enobufs")) { errno=ENOBUFS;return -1; }
        if (!std::strcmp(fault,"peer")) { errno=ECONNRESET;return -1; }
        if (!std::strcmp(fault,"corrupt") && message->msg_iovlen==3 && message->msg_iov[2].iov_len)
            static_cast<unsigned char*>(message->msg_iov[2].iov_base)[0]^=1;
    }
    return real(fd,message,flags);
}

extern "C" ssize_t recvmsg(int fd,msghdr* message,int flags) {
    using Function=ssize_t(*)(int,msghdr*,int);
    static auto real=reinterpret_cast<Function>(dlsym(RTLD_NEXT,"recvmsg"));
    const auto result=real(fd,message,flags);
    const auto* fault=std::getenv("CPNETFLUX_TCP_SEND_TEST_FAULT");
    if (result>=0 && (flags&MSG_ERRQUEUE) && fault && !std::strcmp(fault,"drop")) {
        errno=EAGAIN;return -1;
    }
    if (result>=0 && (flags&MSG_ERRQUEUE) && fault) {
        if (!std::strcmp(fault,"ctrunc")) message->msg_flags|=MSG_CTRUNC;
        if (!std::strcmp(fault,"unrelated")) {
            for (auto* c=CMSG_FIRSTHDR(message);c;c=CMSG_NXTHDR(message,c))
                if (c->cmsg_len>=CMSG_LEN(sizeof(sock_extended_err))) {
                    auto* error=reinterpret_cast<sock_extended_err*>(CMSG_DATA(c));
                    error->ee_origin=SO_EE_ORIGIN_LOCAL;error->ee_errno=EIO;
                }
        }
    }
    return result;
}
