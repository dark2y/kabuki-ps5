/* The System tab's readings, gathered by the parent (an ordinary payload process) once a second
   while the panel is open and written to /data/custom-overlays/system.json for the panel.
   CPU follows drakmor/ps5-hwinfo (each core's idle thread, found by name); the frame rate is the
   display controller's flip counter as Common FPS for PS5 / SimpleFPS read it.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include <sys/ioctl.h>
extern "C" {
int sceKernelGetCpuTemperature(int* temp);
int sceKernelGetSocSensorTemperature(int sensor,int* temp);
int get_page_table_stats(int vm,int type,int* total,int* free);
int sceKernelGetCpuUsage(void* threads,std::int32_t* count);
int sceKernelGetThreadName(std::uint32_t tid,char* name);
}
class SystemStats {
public:
    void tick(const State& s,std::uint32_t sdk,bool librespot) {
        const auto now=sceKernelGetProcessTime();
        if(!s.input_active||now<next) return;
        next=now+1000000;
        sample(now);
        char firmware[16]; std::snprintf(firmware,sizeof firmware,"%x.%02x",sdk>>24,(sdk>>16)&0xffU);
        timespec up{}; clock_gettime(CLOCK_MONOTONIC,&up);
        char json[768];
        std::snprintf(json,sizeof json,
            "{\"cpu\":%.1f,\"cpu_temp\":%d,\"soc_temp\":%d,\"mem_used_gb\":%.2f,\"mem_total_gb\":%.2f,"
            "\"fps\":%.1f,\"fps_min\":%.1f,\"uptime_s\":%lld,\"firmware\":\"%s\",\"address\":\"%s\",\"librespot\":\"%s\"}",
            cpu,cpu_temp,soc_temp,mem_used/1024.0,mem_total/1024.0,fps_average(),fps_lowest(),(long long)up.tv_sec,firmware,
            address().c_str(),librespot?"Connected":"Not running");
        const int fd=open("/data/custom-overlays/system.json.partial",O_WRONLY|O_CREAT|O_TRUNC,0644);
        if(fd<0) return;
        const auto length=(ssize_t)std::strlen(json);
        const bool ok=write(fd,json,length)==length;
        close(fd);
        if(ok) rename("/data/custom-overlays/system.json.partial","/data/custom-overlays/system.json");
        if(samples<=3) note("SYSTEM stats %s",json);
    }
private:
    // sceKernelGetCpuUsage's entry (ps5-hwinfo's proc_stats_t, 40 bytes).
    struct Thread { std::uint32_t pid,tid; std::int64_t user_s,user_us,system_s,system_us; };
    static_assert(sizeof(Thread)==40);
    static constexpr int kThreads=3072;
    // Twice the entries asked for: room in case the kernel writes more than it is told. On the heap:
    // as static data they made the payload too large to load (over 2 MiB).
    Thread* threads[2]={nullptr,nullptr};
    int counts[2]={0,0},bank=0;
    std::uint64_t taken[2]={0,0};
    std::uint32_t idle_tid[16]{}; int idle_found=0;
    std::uint64_t next=0;
    float cpu=-1; int cpu_temp=-1,soc_temp=-1,mem_used=-1,mem_total=-1;
    int dce=-2; std::uint64_t flips=0,flips_at=0;
    float fps_seen[5]={-1,-1,-1,-1,-1}; int fps_at=0,samples=0;

    void step(const char* name) { if(samples==0) note("SYSTEM step %s",name); }
    static double seconds(const Thread& t) { return t.user_s+t.user_us*1e-6+t.system_s+t.system_us*1e-6; }
    void sample(std::uint64_t now) {
        int t=0;
        step("temperature");
        cpu_temp=sceKernelGetCpuTemperature(&t)==0?t:-1;
        soc_temp=sceKernelGetSocSensorTemperature(0,&t)==0?t:-1;
        step("memory");
        memory();
        step("cpu");
        cpu_load(now);
        step("frame rate");
        fps_seen[fps_at++%5]=frame_rate(now);
        step("done");
        ++samples;
    }
    // System RAM: the kernel's page counters (free, inactive and cache pages are available), as
    // ps5-hwinfo's fallback; the page-table figure (constant 16384/10226 MB here) only without them.
    void memory() {
        unsigned page=0,pages=0,free=0,inactive=0,cache=0; std::size_t n=sizeof page;
        bool counted=!sysctlbyname("hw.pagesize",&page,&n,nullptr,0)&&page;
        if(counted) { n=sizeof pages; counted=!sysctlbyname("vm.stats.vm.v_page_count",&pages,&n,nullptr,0)&&pages; }
        if(counted) { n=sizeof free; counted=!sysctlbyname("vm.stats.vm.v_free_count",&free,&n,nullptr,0); }
        if(counted) {
            n=sizeof inactive; (void)sysctlbyname("vm.stats.vm.v_inactive_count",&inactive,&n,nullptr,0);
            n=sizeof cache; (void)sysctlbyname("vm.stats.vm.v_cache_count",&cache,&n,nullptr,0);
            const double mb=page/1048576.0;
            mem_total=int(pages*mb); mem_used=mem_total-int((double(free)+inactive+cache)*mb);
        }
        int total=0,free_mb=0;
        const bool table=get_page_table_stats(1,1,&total,&free_mb)==0&&total>0;
        if(!counted&&table) { mem_total=total; mem_used=total-free_mb; }
        if(samples<3) note("SYSTEM raw memory pages=%u page=%u free=%u inactive=%u cache=%u table_total=%d table_free=%d using=%s",
                           pages,page,free,inactive,cache,total,free_mb,counted?"counters":"table");
    }
    // CPU: 1 - idle share of each core's idle thread (SceIdleCpu0..15, SceIdleCpuRv for core 13),
    // averaged. Summing every thread counted the idle threads as busy: always 100%.
    void cpu_load(std::uint64_t now) {
        for(auto*& t:threads) if(!t) t=static_cast<Thread*>(std::calloc(kThreads*2,sizeof(Thread)));
        if(!threads[0]||!threads[1]) return;
        auto* list=threads[bank];
        std::int32_t count=kThreads;
        if(sceKernelGetCpuUsage(list,&count)!=0||count<=0||count>kThreads) {
            if(samples<3) note("SYSTEM raw cpu usage failed count=%d",count);
            return;
        }
        counts[bank]=count; taken[bank]=now;
        if(idle_found<8) {
            idle_found=0;
            for(int i=0;i<count;++i) {
                char name[64]{};
                if(sceKernelGetThreadName(list[i].tid,name)!=0) continue;
                int lane=-1;
                if(!std::strcmp(name,"SceIdleCpuRv")) lane=13;
                else if(std::sscanf(name,"SceIdleCpu%d",&lane)!=1) lane=-1;
                if(lane>=0&&lane<16) { idle_tid[lane]=list[i].tid; ++idle_found; }
            }
            if(samples<3) note("SYSTEM raw cpu threads=%d idle_threads=%d",count,idle_found);
        }
        const int other=bank^1;
        if(taken[other]&&now>taken[other]&&idle_found) {
            const double wall=(now-taken[other])/1e6;
            double busy=0; int lanes=0;
            for(int lane=0;lane<16;++lane) {
                if(!idle_tid[lane]) continue;
                const Thread *a=nullptr,*b=nullptr;
                for(int i=0;i<count;++i) if(list[i].tid==idle_tid[lane]) { a=&list[i]; break; }
                for(int i=0;i<counts[other];++i) if(threads[other][i].tid==idle_tid[lane]) { b=&threads[other][i]; break; }
                if(!a||!b) continue;
                const double idle=std::clamp((seconds(*a)-seconds(*b))/wall,0.0,1.0);
                busy+=1-idle; ++lanes;
            }
            if(lanes) cpu=float(busy/lanes*100);
        }
        bank=other;
    }
    // Pictures shown a second, from the display controller's flip counter (Common FPS for PS5's
    // fallback, SimpleFPS on 13.60). The home screen itself draws at 60; a game shows its own rate.
    float frame_rate(std::uint64_t now) {
        const int sample=open("/system_tmp/fps_sample",O_RDONLY);
        if(sample>=0) {
            unsigned char b[64]{}; const auto n=read(sample,b,sizeof b); close(sample);
            std::uint32_t magic=0; float value=0; std::memcpy(&magic,b,4); std::memcpy(&value,b+16,4);
            if(n>=48&&magic==0x4F465053u&&b[12]&&value>0&&value<=245) return value;
        }
        if(dce==-2) dce=open("/dev/dce",O_RDWR);
        if(dce<0) return -1;
        struct { std::uint64_t selector,mask,output,reserved[3]; } arg{};
        unsigned char out[0x60]{};
        arg.selector=0x10000000AULL; arg.mask=0x8000000000ULL; arg.output=(std::uint64_t)out;
        if(ioctl(dce,0x80308217UL,&arg)<0) { close(dce); dce=-1; return -1; }
        std::uint64_t count=0; std::memcpy(&count,out+8,8);
        float rate=-1;
        if(flips_at&&now>flips_at&&count>=flips) rate=float((count-flips)/((now-flips_at)/1e6));
        flips=count; flips_at=now;
        return rate>=1&&rate<=245?rate:-1;
    }
    float fps_average() const {
        float sum=0; int n=0;
        for(float f:fps_seen) if(f>=0) { sum+=f; ++n; }
        return n?sum/n:-1;
    }
    float fps_lowest() const {
        float low=-1;
        for(float f:fps_seen) if(f>=0&&(low<0||f<low)) low=f;
        return low;
    }
    // This console's address on the network: the source a UDP socket would use (nothing is sent).
    static std::string address() {
        const int fd=socket(AF_INET,SOCK_DGRAM,0); if(fd<0) return "";
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(53); a.sin_addr.s_addr=htonl(0x08080808);
        std::string text;
        sockaddr_in me{}; socklen_t size=sizeof me;
        if(!connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)&&!getsockname(fd,reinterpret_cast<sockaddr*>(&me),&size)) {
            char ip[32]; const auto v=ntohl(me.sin_addr.s_addr);
            std::snprintf(ip,sizeof ip,"%u.%u.%u.%u",v>>24,(v>>16)&255,(v>>8)&255,v&255); text=ip;
        }
        close(fd); return text;
    }
};
