// Local mechanism diagnostic, not a V3 transfer or persistence benchmark.
#include "mapped_window_collapse.h"
#include "cpnetflux/storage/mapped_file_window.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>

namespace cpnetflux::diagnostic {
namespace {
using Clock=std::chrono::steady_clock;
using storage::MappedFileWindow;
constexpr std::uint64_t MiB=1024ULL*1024, window=MappedFileWindow::kWindowBytes;
void require(bool b,const std::string& s) {if(!b) throw std::runtime_error(s);}
double seconds(const timeval& t) {return t.tv_sec+t.tv_usec*1e-6;}
double cpu(const rusage& u) {return seconds(u.ru_utime)+seconds(u.ru_stime);}
rusage usage(int who) {rusage u{};require(::getrusage(who,&u)==0,"getrusage");return u;}
struct Work {
    double wall=0,cpu=0;
    std::uint64_t minor=0,major=0;
    void add(const rusage& a,const rusage& b,Clock::time_point start) {
        wall+=std::chrono::duration<double>(Clock::now()-start).count();
        cpu+=diagnostic::cpu(b)-diagnostic::cpu(a);
        minor+=b.ru_minflt-a.ru_minflt;major+=b.ru_majflt-a.ru_majflt;
    }
    void merge(const Work& b) {wall+=b.wall;cpu+=b.cpu;minor+=b.minor;major+=b.major;}
    void json() const {std::cout<<"{\"worker_wall_seconds_sum\":"<<wall
        <<",\"worker_cpu_seconds_sum\":"<<cpu<<",\"minor_faults\":"<<minor
        <<",\"major_faults\":"<<major<<"}";}
};
struct File {
    int fd=-1;std::string path;
    explicit File(const std::string& root) {
        auto name=(std::filesystem::path(root)/"cpnetflux-collapse-XXXXXX").string();
        std::vector<char> chars(name.begin(),name.end());chars.push_back(0);
        fd=::mkstemp(chars.data());require(fd>=0,"mkstemp");path=chars.data();
    }
    void closeAndRemove() {
        require(::close(fd)==0,"close owned file");fd=-1;
        require(::unlink(path.c_str())==0,"unlink owned file");path.clear();
    }
    ~File() {if(fd>=0) ::close(fd);if(!path.empty()) ::unlink(path.c_str());}
};
std::uint8_t initialValue(std::uint64_t offset) {return 1+(offset/window*13)%97;}
std::uint8_t outputValue(std::uint64_t offset) {return 101+(offset/window*7)%151;}
void checkContent(int fd,std::uint64_t offset,std::uint64_t length,std::uint8_t value,
                  std::vector<std::uint8_t>& actual) {
    for(std::uint64_t n=0;n<length;n+=actual.size()) {
        const auto count=std::min<std::uint64_t>(actual.size(),length-n);
        std::size_t got=0;
        while(got<count) {
            auto r=::pread(fd,actual.data()+got,count-got,offset+n+got);
            if(r<0 && errno==EINTR) continue;
            require(r>0,"pread verify");got+=r;
        }
        require(std::all_of(actual.begin(),actual.begin()+count,[&](auto b){return b==value;}),
                "independent byte verification failed");
    }
}
std::uint64_t observedPmdBytes(std::span<std::uint8_t> span) {
    // A same-file VMA can merge with a concurrently prepared sibling. Isolate
    // this window for observation only, without reading or allocating its pages.
    // Serialize these temporary read-only states so two neighbors cannot merge.
    static std::mutex observationMutex;
    std::lock_guard lock(observationMutex);
    struct ReadOnly {
        std::span<std::uint8_t> bytes;
        explicit ReadOnly(std::span<std::uint8_t> b):bytes(b) {
            require(::mprotect(bytes.data(),bytes.size(),PROT_READ)==0,"smaps read protection");
        }
        ~ReadOnly() {if(!bytes.empty()) (void)::mprotect(bytes.data(),bytes.size(),PROT_READ|PROT_WRITE);}
        void restore() {
            require(::mprotect(bytes.data(),bytes.size(),PROT_READ|PROT_WRITE)==0,"restore write protection");
            bytes={};
        }
    } readOnly(span);
    std::ifstream smaps("/proc/self/smaps");require(smaps.good(),"read smaps");
    const auto address=reinterpret_cast<std::uintptr_t>(span.data());
    bool found=false;std::uint64_t vmaBytes=0;std::string line;
    while(std::getline(smaps,line)) {
        unsigned long start=0,end=0;
        if(std::sscanf(line.c_str(),"%lx-%lx",&start,&end)==2) {
            if(found) break;
            if(address>=start && address<end) {
                require(end>=address+span.size(),"smaps incomplete VMA");
                require(start==address && end==address+span.size(),"smaps isolation failed");
                vmaBytes=end-start;found=true;
            }
        } else if(found && line.starts_with("ShmemPmdMapped:")) {
            const auto n=std::stoull(line.substr(std::strlen("ShmemPmdMapped:")));
            // Adjacent same-file windows may merge. Subtract the maximum PMD
            // contribution of all bytes outside this span: only a lower bound is
            // attributable to this window, never count a sibling's pages twice.
            const auto outside=vmaBytes-span.size();
            readOnly.restore();
            return n*1024>outside?n*1024-outside:0;
        }
    }
    throw std::runtime_error("smaps ShmemPmdMapped missing");
}
struct Row {
    Work work,prep,copy,verify;
    std::uint64_t maps=0,unmaps=0,peak=0,calls=0,written=0,eligible=0,edges=0,seeds=0;
    std::uint64_t observed=0,observations=0,preparedChecks=0;
    std::string error;
};
}
int runMappedLifecycleBench(const std::string& directory,std::uint64_t bytes,unsigned workers,
    std::uint64_t frameBytes,const std::string& mode,const std::string& initial,std::uint64_t cancelAfter) {
    const bool collapse=mode=="collapse";
    if(collapse) {
        std::ifstream size("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size");
        std::uint64_t actual=0;size>>actual;
        require(actual==pmdBytes,"unsupported PMD size, diagnostic requires 2MiB");
    }
    const auto before=usage(RUSAGE_SELF);const auto started=Clock::now();
    File file(directory);require(::ftruncate(file.fd,bytes)==0,"ftruncate");
    const auto initialize=Clock::now();
    if(initial=="pattern") {
        std::vector<std::uint8_t> data(MiB);
        for(std::uint64_t offset=0;offset<bytes;offset+=MiB) {
            std::fill(data.begin(),data.end(),initialValue(offset));
            std::size_t done=0;
            while(done<data.size()) {
                auto n=::pwrite(file.fd,data.data()+done,data.size()-done,offset+done);
                if(n<0 && errno==EINTR) continue;
                require(n>0,"initialize pattern");done+=n;
            }
        }
    }
    const double initializeSeconds=std::chrono::duration<double>(Clock::now()-initialize).count();
    std::vector<Row> rows(workers);std::vector<std::thread> threads;
    try {
        for(unsigned id=0;id<workers;++id) threads.emplace_back([&,id] {
            auto& row=rows[id];
            try {
                std::vector<std::uint8_t> data(MiB),actual(MiB);
                MappedFileWindow writer(file.fd,bytes,false,collapse?
                    storage::MappedWindowAlignment::HugePage2MiB:storage::MappedWindowAlignment::Default);
                const auto begin=id*(bytes/workers),end=begin+bytes/workers;
                std::uint64_t cancelChecks=0;
                for(auto offset=begin;offset<end;offset+=window) {
                    std::fill(data.begin(),data.end(),outputValue(offset));
                    const auto workUsage=usage(RUSAGE_THREAD);const auto workStarted=Clock::now();
                    auto borrowed=writer.writableSpan(offset,window);
                    require(borrowed.isOk(),"writableSpan: "+borrowed.status().message());
                    require(borrowed.value().size()==window,"incomplete owned window");
                    if(collapse) {
                        const auto prepUsage=usage(RUSAGE_THREAD);const auto prepStarted=Clock::now();
                        auto report=prepareCollapse(borrowed.value(),
                            [](void* p,std::size_t n,int kind){return ::madvise(p,n,kind);},
                            [&] {return cancelAfter && cancelChecks++>=cancelAfter+1;});
                        row.prep.add(prepUsage,usage(RUSAGE_THREAD),prepStarted);
                        row.eligible+=report.eligibleBytes;row.edges+=report.edgeBytes;row.seeds+=report.seeds;
                    }
                    const auto observed=observedPmdBytes(borrowed.value());
                    row.observed+=observed;++row.observations;
                    if(collapse) require(observed==window,"MADV_COLLAPSE did not produce full ShmemPmdMapped window");
                    // Existing-file correctness only; fresh files are never pre-read here.
                    // Charged to worker/whole accounting, never claimed to be cold allocation.
                    if(initial=="pattern") {
                        checkContent(file.fd,offset,window,initialValue(offset),actual);++row.preparedChecks;
                    }
                    const auto copyUsage=usage(RUSAGE_THREAD);const auto copyStarted=Clock::now();
                    for(std::uint64_t chunk=0;chunk<window;chunk+=MiB) {
                        for(std::size_t done=0;done<MiB;) {
                            const auto count=std::min<std::size_t>(frameBytes,MiB-done);
                            require(writer.write(offset+chunk+done,
                                {data.data()+done,count}).isOk(),"writer.write");done+=count;
                        }
                    }
                    row.copy.add(copyUsage,usage(RUSAGE_THREAD),copyStarted);
                    require(writer.close().isOk(),"close mapping");
                    row.work.add(workUsage,usage(RUSAGE_THREAD),workStarted);
                    const auto verifyUsage=usage(RUSAGE_THREAD);const auto verifyStarted=Clock::now();
                    checkContent(file.fd,offset,window,outputValue(offset),actual);
                    row.verify.add(verifyUsage,usage(RUSAGE_THREAD),verifyStarted);
                }
                const auto& stats=writer.stats();row.maps=stats.mapCalls;row.unmaps=stats.unmapCalls;
                row.peak=stats.peakMappedBytes;row.calls=stats.writeCalls;row.written=stats.writtenBytes;
                require(stats.activeMappedBytes==0 && stats.mapFailures==0 && row.maps==row.unmaps,
                        "mapping lifecycle mismatch");
            } catch(const std::exception& e) {row.error=e.what();}
        });
    } catch(...) {for(auto& t:threads) t.join();throw;}
    for(auto& t:threads) t.join();
    Row total;
    for(const auto& row:rows) {
        require(row.error.empty(),row.error);
        total.work.merge(row.work);total.prep.merge(row.prep);total.copy.merge(row.copy);total.verify.merge(row.verify);
#define SUM(f) total.f+=row.f
        SUM(maps);SUM(unmaps);SUM(peak);SUM(calls);SUM(written);SUM(eligible);SUM(edges);SUM(seeds);
        SUM(observed);SUM(observations);SUM(preparedChecks);
#undef SUM
    }
    const auto cleanupUsage=usage(RUSAGE_SELF);const auto cleanupStarted=Clock::now();
    file.closeAndRemove();
    const auto cleanupAfter=usage(RUSAGE_SELF);
    const auto cleanupSeconds=std::chrono::duration<double>(Clock::now()-cleanupStarted).count();
    const auto after=usage(RUSAGE_SELF);
    const auto elapsed=std::chrono::duration<double>(Clock::now()-started).count();
    std::cout.precision(12);
    std::cout<<"{\"result\":\"pass\",\"schema\":\"cpnetflux_mapped_lifecycle_v1\","
      "\"scope\":\"local actual MappedFileWindow; not transfer or persistence; whole includes create/resize, threads/buffers, mapping/alignment retry, seed/collapse, serialized mprotect/smaps/restore observation, copy, unmap, pread verification, close/unlink; pattern initialization also charged\","
      "\"work_scope\":\"summed per-worker window brackets from mapping to close, including prep/smaps and pattern preservation check, excluding output verification; worker wall intervals overlap\","
      "\"page_mode\":\""<<mode<<"\",\"initial_state\":\""<<initial<<"\",\"bytes\":"<<bytes
      <<",\"workers\":"<<workers<<",\"frame_bytes\":"<<frameBytes<<",\"profile_enabled\":false"
      <<",\"map_calls\":"<<total.maps<<",\"unmap_calls\":"<<total.unmaps
      <<",\"sum_worker_peak_mapping_bytes\":"<<total.peak<<",\"write_calls\":"<<total.calls
      <<",\"written_bytes\":"<<total.written<<",\"eligible_bytes_sum\":"<<total.eligible
      <<",\"edge_bytes_sum\":"<<total.edges<<",\"seed_writes\":"<<total.seeds
      <<",\"shmem_pmd_mapped_bytes_lower_bound_sum\":"<<total.observed
      <<",\"mapping_observations\":"<<total.observations<<",\"preserved_windows_verified\":"<<total.preparedChecks
      <<",\"initialize_seconds\":"<<initializeSeconds<<",\"whole_diagnostic_wall_seconds\":"<<elapsed
      <<",\"whole_process_cpu_seconds\":"<<cpu(after)-cpu(before)
      <<",\"whole_process_minor_faults\":"<<after.ru_minflt-before.ru_minflt
      <<",\"whole_process_major_faults\":"<<after.ru_majflt-before.ru_majflt
      <<",\"payload_removed\":true,\"output_verified_bytes\":"<<bytes<<",\"worker_work\":";
    total.work.json();std::cout<<",\"prepare_work\":";total.prep.json();
    std::cout<<",\"copy_work\":";total.copy.json();
    std::cout<<",\"output_verification_work\":";total.verify.json();
    std::cout<<",\"cleanup_wall_seconds\":"<<cleanupSeconds
        <<",\"cleanup_process_cpu_seconds\":"<<cpu(cleanupAfter)-cpu(cleanupUsage)<<"}\n";
    return 0;
}
}
