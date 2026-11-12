#include "cpnetflux/core/io/tcp_source_port_plan.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <charconv>
#include <cerrno>
#include <set>
#include <sstream>
#include "cpnetflux/core/io/socket_utils.h"
namespace cpnetflux::core::io {
namespace {
constexpr std::size_t kMaxBytes=2*1024*1024;
common::Status bad(const char* reason) { return common::Status::invalidArgument(
    std::string("TCP source port plan: ")+reason); }
bool ipv4(std::string_view text) {
    in_addr address{}; return ::inet_pton(AF_INET,std::string(text).c_str(),&address)==1;
}
bool number(const std::string& token,std::uint32_t* value) {
    auto r=std::from_chars(token.data(),token.data()+token.size(),*value);
    return !token.empty() && r.ec==std::errc{} && r.ptr==token.data()+token.size();
}
}
common::Result<TcpSourcePortPlan> TcpSourcePortPlan::parse(std::string_view text,
    std::string_view local,std::string_view peer,std::uint32_t channels) {
    if (text.empty() || text.size()>kMaxBytes || text.find('\0')!=text.npos ||
        !ipv4(local) || !ipv4(peer) || !channels || channels>16) return bad("invalid size, IPv4 endpoints or channels");
    std::istringstream input{std::string(text)};std::string line;
    if (!std::getline(input,line)) return bad("missing header");
    std::istringstream header(line);std::string magic,l,p,count,sha,extra;std::uint32_t declared=0;
    if (!(header>>magic>>l>>p>>count>>sha) || (header>>extra) ||
        magic!="CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1" || l!=local || p!=peer ||
        !number(count,&declared) || declared!=channels || sha.size()!=64 ||
        sha.find_first_not_of("0123456789abcdef")!=sha.npos) return bad("invalid header or endpoint mismatch");
    TcpSourcePortPlan plan;plan.snapshotSha_=sha;
    std::map<std::uint32_t,std::uint32_t> queues;std::set<std::uint32_t> usedQueues;
    std::size_t total=0;
    while (std::getline(input,line)) {
        if (line.empty()) return bad("empty row");
        std::istringstream row(line);std::string a,b,c,token;std::uint32_t channel=0,port=0,queue=0;
        if (!(row>>a>>b>>c) || !number(a,&channel) || !number(b,&port) || !number(c,&queue) ||
            channel>=channels || !port || port>65535 || queue>4095) return bad("invalid row key");
        auto q=queues.find(channel);
        if (q==queues.end()) {
            if (!usedQueues.insert(queue).second) return bad("channels share a target queue");
            queues.emplace(channel,queue);
        } else if (q->second!=queue) return bad("channel target queue changed");
        TcpSourcePortEntry entry;entry.targetQueue=queue;std::set<std::uint32_t> ports;
        while (row>>token) {
            std::uint32_t source=0;
            if (!number(token,&source) || source<1024 || source>65535 ||
                !ports.insert(source).second || entry.candidates.size()>=128 || ++total>262144)
                return bad("invalid, duplicate or excessive source ports");
            entry.candidates.push_back(static_cast<std::uint16_t>(source));
        }
        if (entry.candidates.empty() || plan.entries_.size()>=2048 ||
            !plan.entries_.emplace(std::make_pair(channel,static_cast<std::uint16_t>(port)),std::move(entry)).second)
            return bad("empty, duplicate or excessive rows");
    }
    if (queues.size()!=channels) return bad("missing channel");
    return plan;
}
common::Result<TcpSourcePortPlan> TcpSourcePortPlan::load(const std::string& path,
    std::string_view local,std::string_view peer,std::uint32_t channels) {
    UniqueFd fd(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    if (!fd.isValid()) return common::Status::systemError("open TCP source port plan",errno);
    struct stat st{};
    if (::fstat(fd.get(),&st) || !S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>static_cast<off_t>(kMaxBytes))
        return bad("plan must be a bounded regular file");
    std::string data(static_cast<std::size_t>(st.st_size),'\0');std::size_t offset=0;
    while (offset<data.size()) {
        auto n=::read(fd.get(),data.data()+offset,data.size()-offset);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return bad("truncated or unreadable file");
        offset+=static_cast<std::size_t>(n);
    }
    char tail=0;ssize_t n;
    do {n=::read(fd.get(),&tail,1);} while (n<0 && errno==EINTR);
    if (n!=0) return bad("file changed during read");
    return parse(data,local,peer,channels);
}
const TcpSourcePortEntry* TcpSourcePortPlan::find(std::uint32_t channel,std::uint16_t peerPort) const {
    auto it=entries_.find({channel,peerPort});return it==entries_.end()?nullptr:&it->second;
}
} // namespace cpnetflux::core::io
