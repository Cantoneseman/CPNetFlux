#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/errqueue.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "tcp_send_ownership.h"
#include "cpnetflux/core/io/persistent_data_session.h"
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/storage/mapped_file_window.h"
#include "cpnetflux/storage/posix_file.h"

namespace {
namespace io=cpnetflux::core::io;
namespace protocol=cpnetflux::core::protocol;
namespace checksum=cpnetflux::checksum;
using cpnetflux::diagnostic::SendOwnership;
using Clock=std::chrono::steady_clock;
constexpr std::size_t kStride=69632, kData=65532, kChunk=1048576;
void check(bool valid, const char* error) { if (!valid) throw std::runtime_error(error); }
void require(const cpnetflux::common::Status& s) { if (!s.isOk()) throw std::runtime_error(s.message()); }
std::uint64_t number(const char* s,std::uint64_t max) {
    std::uint64_t n=0;const auto end=s+std::strlen(s);auto p=std::from_chars(s,end,n);
    check(p.ec==std::errc{} && p.ptr==end && n && n<=max,"invalid numeric argument");return n;
}
bool same(const struct stat& a,const struct stat& b) {
    return S_ISREG(b.st_mode) && a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_size==b.st_size &&
        a.st_mtim.tv_sec==b.st_mtim.tv_sec && a.st_mtim.tv_nsec==b.st_mtim.tv_nsec &&
        a.st_ctim.tv_sec==b.st_ctim.tv_sec && a.st_ctim.tv_nsec==b.st_ctim.tv_nsec;
}
struct Pool {
    std::array<std::uint8_t*,SendOwnership::kSlots> slots{};
    bool drained=false;
    Pool() {
        for (auto& p:slots) { void* raw=nullptr;check(::posix_memalign(&raw,4096,kStride)==0,"pool allocation failed");
            p=static_cast<std::uint8_t*>(raw);std::memset(p,0,kStride); }
    }
    ~Pool() {
        // Terminal failures retain all slots until process exit, including teardown.
        if (drained) for (auto p:slots) std::free(p);
    }
};
struct Lane {
    std::uint64_t bytes=0,frames=0,calls=0,shortWrites=0,retries=0,notifications=0;
    std::uint64_t copied=0,unmarked=0,copiedBytes=0,unmarkedBytes=0,peakHeld=0;
    double drain=0;std::string error;
};

void completions(int fd,SendOwnership& owner,Lane& result) {
    for (;;) {
        alignas(cmsghdr) std::array<char,512> control{};
        char byte; iovec iov{&byte,1};msghdr msg{};
        msg.msg_iov=&iov;msg.msg_iovlen=1;msg.msg_control=control.data();msg.msg_controllen=control.size();
        const auto n=::recvmsg(fd,&msg,MSG_ERRQUEUE|MSG_DONTWAIT);
        if (n<0 && errno==EINTR) continue;
        if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) return;
        check(n>=0,"error-queue receive failed");
        check(!(msg.msg_flags&MSG_CTRUNC),"truncated error-queue control");
        bool found=false;
        for (auto* c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
            const bool type=(c->cmsg_level==SOL_IP && c->cmsg_type==IP_RECVERR) ||
                (c->cmsg_level==SOL_IPV6 && c->cmsg_type==IPV6_RECVERR);
            check(type && c->cmsg_len>=CMSG_LEN(sizeof(sock_extended_err)),"unrelated error-queue control");
            sock_extended_err error{};std::memcpy(&error,CMSG_DATA(c),sizeof(error));
            check(error.ee_origin==SO_EE_ORIGIN_ZEROCOPY && error.ee_errno==0 &&
                (error.ee_code==0 || error.ee_code==SO_EE_CODE_ZEROCOPY_COPIED),"unrelated socket error");
            check(owner.complete(error.ee_info,error.ee_data,error.ee_code==SO_EE_CODE_ZEROCOPY_COPIED),
                  "unknown or duplicate completion range");
            ++result.notifications;found=true;
        }
        check(found,"missing zero-copy completion");
    }
}

void waitSocket(int fd,short events,Clock::time_point deadline,SendOwnership* owner,Lane& result) {
    if (owner) completions(fd,*owner,result);
    check(Clock::now()<deadline,"send/completion deadline expired");
    pollfd p{fd,events,0};const int rc=::poll(&p,1,2);
    check(rc>=0 || errno==EINTR,"socket poll failed");
    check(!(p.revents&POLLNVAL),"invalid polled socket");
    if (owner) completions(fd,*owner,result);
    int error=0;socklen_t size=sizeof(error);
    check(::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0 && error==0,"socket terminal error");
    check(!(p.revents&POLLHUP),"peer closed during send");
}

void send(io::FramedDataSocket& socket,io::PersistentFileIdentity identity,
          cpnetflux::storage::PosixFile& source,std::uint64_t base,bool zero,
          Clock::time_point deadline,Lane& result) {
    Pool pool;SendOwnership owner;
    if (zero) { int one=1;check(::setsockopt(socket.fd(),SOL_SOCKET,SO_ZEROCOPY,&one,sizeof(one))==0,
                              "SO_ZEROCOPY unavailable"); }
    require(io::PersistentDataSession::writeBegin(&socket,identity));
    checksum::ChecksumComputer whole(checksum::ChecksumAlgorithm::Crc32c);
    std::size_t selected=0;
    while (result.bytes<identity.totalSize) {
        if (zero) {
            // Amortize empty error-queue reads; a full pool still drains immediately.
            if (result.frames%8==0) completions(socket.fd(),owner,result);
            while (!owner.available(selected)) {
                bool found=false;
                for (std::size_t i=0;i<pool.slots.size();++i) if (owner.available(i)) {
                    selected=i;found=true;break;
                }
                if (!found) waitSocket(socket.fd(),0,deadline,&owner,result);
            }
        }
        auto* slot=pool.slots[selected];
        const auto length=std::min<std::uint64_t>(kData,std::min(identity.totalSize-result.bytes,
                                                              kChunk-result.bytes%kChunk));
        require(source.readAtAll(base+result.bytes,slot+68,length));
        checksum::ChecksumComputer crc(checksum::ChecksumAlgorithm::Crc32c);crc.update(slot+68,length);
        const auto value=crc.finalize();check(whole.combine(value,length),"CRC combine failed");
        protocol::FrameHeader header;header.type=protocol::FrameType::Data;
        header.streamId=identity.fileId;header.chunkId=identity.generation;
        header.offset=result.bytes;header.totalSize=identity.totalSize;
        header.flags=protocol::kDataChecksum;header.payloadSize=length+4;
        const auto encoded=protocol::encodeFrameHeader(header);std::memcpy(slot,encoded.data(),encoded.size());
        for (unsigned i=0;i<4;++i) slot[64+i]=static_cast<std::uint8_t>(value.value>>(24-8*i));
        const std::size_t total=68+length;std::size_t offset=0;
        if (zero) check(owner.begin(selected,total),"busy zero-copy slot");
        while (offset<total) {
            check(Clock::now()<deadline,"send/completion deadline expired");
            if (zero && !owner.canSend()) { waitSocket(socket.fd(),0,deadline,&owner,result);continue; }
            iovec spans[3]{};std::size_t count=0;
            for (auto bounds:{std::pair<std::size_t,std::size_t>{0,64},{64,68},{68,total}}) {
                const auto begin=std::max(bounds.first,offset);
                if (begin<bounds.second) spans[count++]={slot+begin,bounds.second-begin};
            }
            msghdr msg{};msg.msg_iov=spans;msg.msg_iovlen=count;
            const auto n=::sendmsg(socket.fd(),&msg,MSG_NOSIGNAL|MSG_DONTWAIT|(zero?MSG_ZEROCOPY:0));
            if (n<0 && errno==EINTR) { ++result.retries;continue; }
            if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
                ++result.retries;waitSocket(socket.fd(),POLLOUT,deadline,zero?&owner:nullptr,result);continue;
            }
            if (n<0 && errno==ENOBUFS) throw std::runtime_error("zero-copy ENOBUFS; no fallback");
            check(n>0,"sendmsg failed or returned zero");
            check(static_cast<std::size_t>(n)<=total-offset,"sendmsg exceeded frame");
            if (zero) check(owner.accepted(selected,n),"accepted send missing ownership");
            if (static_cast<std::size_t>(n)<total-offset) ++result.shortWrites;
            offset+=n;++result.calls;
        }
        result.bytes+=length;++result.frames;selected=(selected+1)%pool.slots.size();
    }
    const auto drainStart=Clock::now();
    while (zero && owner.pending()) waitSocket(socket.fd(),0,deadline,&owner,result);
    result.drain=std::chrono::duration<double>(Clock::now()-drainStart).count();
    check(!owner.heldBytes,"undrained payload references");pool.drained=true;
    result.copied=owner.copiedCalls;result.unmarked=owner.unmarkedCalls;
    result.copiedBytes=owner.copiedBytes;result.unmarkedBytes=owner.unmarkedBytes;result.peakHeld=owner.peakHeldBytes;
    require(io::PersistentDataSession::writeEnd(&socket,identity,whole.finalize()));
    auto reply=io::PersistentDataSession::readResult(&socket,identity);
    check(reply.isOk() && reply.value()==protocol::FrameStatusCode::Ok,"remote V3 result not successful");
}

void receive(io::FramedDataSocket& socket,const io::PersistentFileIdentity& identity,int target,
             std::uint64_t base,std::uint64_t total,io::DynamicFileQueue& queue,
             unsigned index,Lane& result) {
    auto worker=queue.worker(index);check(worker.isOk(),"receiver worker missing");
    io::WorkExecutor executor(queue,worker.value());
    io::PersistentDataSession session;io::PersistentFrame frame;
    require(io::PersistentDataSession::readNext(&socket,session.kMaxPayload,&frame));
    auto begin=io::PersistentDataSession::decodeBegin(frame);
    check(begin.isOk() && io::samePersistentIdentity(begin.value(),identity) &&
          begin.value().rangeId==0,"receiver identity mismatch");
    require(session.begin(frame.header,begin.value()));
    auto claimed=executor.claimIncoming(io::WorkItem{identity,identity.transferId,false});
    check(claimed.isOk(),"receiver lease not claimed");const auto lease=claimed.value();require(executor.begin(lease));
    cpnetflux::storage::MappedFileWindow mapped(target,total);
    while (result.bytes<identity.totalSize) {
        require(io::PersistentDataSession::readNext(&socket,session.kMaxPayload,&frame));
        require(protocol::validateDataFramePayload(frame.header,frame.payload.data(),frame.payload.size(),session.kMaxPayload));
        const auto expected=std::min<std::uint64_t>(kData,std::min(identity.totalSize-result.bytes,kChunk-result.bytes%kChunk));
        check(frame.header.type==protocol::FrameType::Data && frame.header.flags==protocol::kDataChecksum &&
            frame.payload.size()==expected+4 && frame.header.offset==result.bytes,"DATA shape mismatch");
        checksum::ChecksumComputer crc(checksum::ChecksumAlgorithm::Crc32c);crc.update(frame.payload.data()+4,expected);
        const auto value=crc.finalize();const auto* p=frame.payload.data();
        const auto sent=(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
        check(value.value==sent,"DATA CRC mismatch");
        auto header=frame.header;header.flags=0;header.payloadSize-=4;
        require(session.dataWithChecksum(header,expected,value));
        require(executor.startIo(lease,expected));
        require(mapped.write(base+result.bytes,{frame.payload.data()+4,static_cast<std::size_t>(expected)}));
        require(executor.finishIo(lease,expected));
        result.bytes+=expected;++result.frames;
    }
    require(mapped.close());
    require(io::PersistentDataSession::readNext(&socket,session.kMaxPayload,&frame));
    auto end=session.end(frame.header,frame.payload.data(),frame.payload.size());check(end.isOk(),"FILE_END invalid");
    require(executor.awaitResult(lease));require(executor.commit(lease));
    require(io::PersistentDataSession::writeResult(&socket,identity,protocol::FrameStatusCode::Ok));
}
}  // namespace

int main(int argc,const char* argv[]) {
    try {
        check(argc==10,"usage: cpnetflux-tcp-send-bench server|client local peer port lanes bytes copied|zerocopy path timeout-ms");
        const std::string role=argv[1],mode=argv[7];check(role=="server" || role=="client","invalid role");
        check(mode=="copied" || mode=="zerocopy","invalid send mode");
        const auto port=number(argv[4],65535),lanes=number(argv[5],4),bytes=number(argv[6],1ULL<<30);
        const auto timeout=number(argv[9],60000);check(bytes>=lanes && timeout>=100,"invalid byte partition or timeout");
        in_addr local{},peer{};check(::inet_pton(AF_INET,argv[2],&local)==1 && ::inet_pton(AF_INET,argv[3],&peer)==1,
                                   "explicit IPv4 addresses required");
        rlimit noCore{0,0};check(::setrlimit(RLIMIT_CORE,&noCore)==0,"disable payload cores failed");
        const bool server=role=="server",zero=mode=="zerocopy";const auto setup=Clock::now();
        io::UniqueFd file(::open(argv[8],server?O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW:
                                 O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW,0600));
        check(file.isValid(),"safe file open failed");struct stat before{};
        check(::fstat(file.get(),&before)==0 && S_ISREG(before.st_mode),"regular file required");
        if (server) check(::ftruncate(file.get(),bytes)==0,"target resize failed");
        else check(before.st_size>=0 && static_cast<std::uint64_t>(before.st_size)==bytes,"source size mismatch");
        cpnetflux::storage::PosixFile source(std::move(file));
        std::vector<io::FramedDataSocket> sockets;sockets.reserve(lanes);
        const auto deadline=Clock::now()+std::chrono::milliseconds(timeout);
        if (server) {
            auto listener=io::createListener(argv[2],port,lanes);check(listener.isOk(),"listen failed");
            auto listening=std::move(listener.value());std::cout<<"CPNETFLUX_TCP_SEND_READY"<<std::endl;
            while (sockets.size()<lanes) {
                pollfd p{listening.get(),POLLIN,0};check(Clock::now()<deadline,"accept deadline expired");
                const auto rc=::poll(&p,1,10);if (rc<0 && errno==EINTR) continue;check(rc>=0,"accept poll failed");
                if (!rc) continue;
                sockaddr_in remote{};socklen_t n=sizeof(remote);
                io::UniqueFd accepted(::accept4(listening.get(),reinterpret_cast<sockaddr*>(&remote),&n,SOCK_CLOEXEC));
                check(accepted.isValid() && remote.sin_addr.s_addr==peer.s_addr,"unexpected peer");
                sockets.emplace_back(std::move(accepted));
            }
        } else for (unsigned i=0;i<lanes;++i) {
            auto socket=io::connectFramedDataSocket(argv[3],port,io::DataTlsMode::Off,{},nullptr,deadline,argv[2]);
            check(socket.isOk(),"connect failed");sockets.emplace_back(std::move(socket.value()));
        }
        for (auto& socket:sockets) require(io::PersistentDataSession::setTimeout(&socket,std::max<std::uint64_t>(1,timeout/1000)));
        const auto setupSeconds=std::chrono::duration<double>(Clock::now()-setup).count();
        io::DynamicFileQueue queue(std::vector<io::PersistentFileIdentity>{},lanes,1,lanes);
        std::vector<Lane> results(lanes);std::vector<std::thread> workers;workers.reserve(lanes);
        rusage cpuBefore{},cpuAfter{};::getrusage(RUSAGE_SELF,&cpuBefore);const auto start=Clock::now();
        try {
            for (unsigned i=0;i<lanes;++i) workers.emplace_back([&,i] {
                try {
                    io::PersistentFileIdentity id;id.fileId=i+1;id.generation=1;
                    id.totalSize=bytes/lanes+(i<bytes%lanes);id.relativePath="lane-"+std::to_string(i)+".bin";
                    id.transferId="cpnetflux-tcp-send-32";id.checksumAlgorithm=checksum::ChecksumAlgorithm::Crc32c;
                    const auto base=i*(bytes/lanes)+std::min<std::uint64_t>(i,bytes%lanes);
                    if (server) receive(sockets[i],id,source.fd(),base,bytes,queue,i,results[i]);
                    else send(sockets[i],id,source,base,zero,deadline,results[i]);
                } catch (const std::exception& e) { results[i].error=e.what();for (auto& socket:sockets) socket.shutdown(); }
            });
        } catch (...) { for (auto& socket:sockets) socket.shutdown();for (auto& w:workers) w.join();throw; }
        for (auto& w:workers) w.join();
        const auto seconds=std::chrono::duration<double>(Clock::now()-start).count();::getrusage(RUSAGE_SELF,&cpuAfter);
        Lane sum;
        for (const auto& r:results) {
            if (!r.error.empty()) throw std::runtime_error(r.error);
            sum.bytes+=r.bytes;sum.frames+=r.frames;sum.calls+=r.calls;sum.shortWrites+=r.shortWrites;
            sum.retries+=r.retries;sum.notifications+=r.notifications;sum.copied+=r.copied;sum.unmarked+=r.unmarked;
            sum.copiedBytes+=r.copiedBytes;sum.unmarkedBytes+=r.unmarkedBytes;sum.peakHeld+=r.peakHeld;
            sum.drain=std::max(sum.drain,r.drain);
        }
        check(sum.bytes==bytes,"incomplete diagnostic");
        if (!server) {
            struct stat after{},path{};check(::fstat(source.fd(),&after)==0 && ::lstat(argv[8],&path)==0 &&
                same(before,after) && same(before,path),"source identity changed");
            check(!zero || sum.copied+sum.unmarked==sum.calls,"incomplete release accounting");
        }
        const auto cpu=[](const rusage& r) {return r.ru_utime.tv_sec+r.ru_stime.tv_sec+
            (r.ru_utime.tv_usec+r.ru_stime.tv_usec)/1e6;};
        std::cout<<std::setprecision(12)<<"{\"diagnostic_only\":true,\"checkpoint_publication\":false,"
            <<"\"role\":\""<<role<<"\",\"mode\":\""<<mode<<"\",\"v3\":true,\"receiver_acknowledged\":true,"
            <<"\"bytes\":"<<bytes<<",\"frames\":"<<sum.frames<<",\"service_seconds\":"<<seconds
            <<",\"setup_seconds\":"<<setupSeconds<<",\"cpu_seconds\":"<<cpu(cpuAfter)-cpu(cpuBefore)
            <<",\"minor_faults\":"<<cpuAfter.ru_minflt-cpuBefore.ru_minflt<<",\"successful_send_calls\":"<<sum.calls
            <<",\"short_writes\":"<<sum.shortWrites<<",\"retry_calls\":"<<sum.retries
            <<",\"completion_messages\":"<<sum.notifications<<",\"copied_release_calls\":"<<sum.copied
            <<",\"unmarked_release_calls\":"<<sum.unmarked<<",\"copied_release_bytes\":"<<sum.copiedBytes
            <<",\"unmarked_release_bytes\":"<<sum.unmarkedBytes<<",\"sum_lane_peak_held_bytes\":"<<sum.peakHeld
            <<",\"pool_allocated_bytes\":"<<(server?0:lanes*SendOwnership::kSlots*kStride)
            <<",\"final_drain_max_seconds\":"<<sum.drain<<"}"<<std::endl;
        return 0;
    } catch (const std::exception& e) { std::cerr<<"cpnetflux-tcp-send-bench: "<<e.what()<<'\n';return 1; }
}
