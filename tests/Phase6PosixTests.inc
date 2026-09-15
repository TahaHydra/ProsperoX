// Original fixtures exercising exact exported ABI identities and errno behavior.
#include "libs/errno.h"
namespace Libs { void InitLibKernel_1(Loader::SymbolDatabase*); }
namespace Phase6Posix {
int Run() {
    using Phase1::Check;
    namespace F = Libs::LibKernel::FileSystem;
    Loader::SymbolDatabase symbols;
    Libs::InitLibKernel_1(&symbols);
    auto lookup = [&](const char* nid) {
        Loader::SymbolResolve key{nid,"Posix",1,"libkernel",1,1,Loader::SymbolType::Func};
        const auto* found = symbols.Find(key);
        Check(found != nullptr,"qualified POSIX export resolves");
        const auto address = found->vaddr;
        key.library_version=99;
        Check(symbols.Find(key)==nullptr,"POSIX export does not bypass version matching");
        return address;
    };
    using Open = KYTY_SYSV_ABI int (*)(const char*,int,int);
    using Close = KYTY_SYSV_ABI int (*)(int);
    using Seek = KYTY_SYSV_ABI int64_t (*)(int,int64_t,int);
    using Stat = KYTY_SYSV_ABI int64_t (*)(int,F::FileStat*);
    const auto open = reinterpret_cast<Open>(lookup("wuCroIGjt2g"));
    const auto close = reinterpret_cast<Close>(lookup("bY-PO6JhzhQ"));
    using Write = KYTY_SYSV_ABI int64_t (*)(int,const char*,int64_t);
    const auto write = reinterpret_cast<Write>(lookup("FN4gaPmuFV8"));
    const auto seek = reinterpret_cast<Seek>(lookup("Oy6IpwgtYOk"));
    const auto stat = reinterpret_cast<Stat>(lookup("mqQMh1zPPT8"));
    using Unlink = KYTY_SYSV_ABI int (*)(const char*);
    const auto unlink = reinterpret_cast<Unlink>(lookup("VAzswvTOCzI"));
    using CondInit = KYTY_SYSV_ABI int (*)(Libs::LibKernel::PthreadCond*,const Libs::LibKernel::PthreadCondattr*);
    using CondDestroy = KYTY_SYSV_ABI int (*)(Libs::LibKernel::PthreadCond*);
    const auto cond_init = reinterpret_cast<CondInit>(lookup("0TyVk4MSLt0"));
    const auto cond_destroy = reinterpret_cast<CondDestroy>(lookup("RXXqi4CtF8w"));
    *Libs::Posix::GetErrorAddr()=123;
    Check(cond_destroy(nullptr)==Libs::Posix::POSIX_EINVAL && *Libs::Posix::GetErrorAddr()==123,
        "POSIX condition destroy returns positive error without changing errno");
    Libs::LibKernel::PthreadCond condition=nullptr;
    Check(cond_init(&condition,nullptr)==0 && condition!=nullptr,"create actual guest condition");
    Check(cond_destroy(&condition)==0 && condition==nullptr && *Libs::Posix::GetErrorAddr()==123,
        "POSIX condition destroy releases condition and preserves errno");
    Check(Libs::LibKernel::PthreadCondDestroy(nullptr)==Libs::LibKernel::KERNEL_ERROR_EINVAL,
        "kernel condition destroy preserves kernel error convention");
    Loader::SymbolResolve wait_key{"Hc4CaR6JBL0","libkernel_sync_on_address",1,"libkernel",1,1,Loader::SymbolType::Func};
    Loader::SymbolResolve wake_key{"q2y-wDIVWZA","libkernel_sync_on_address",1,"libkernel",1,1,Loader::SymbolType::Func};
    const auto* wait_export=symbols.Find(wait_key);
    const auto* wake_export=symbols.Find(wake_key);
    Check(wait_export && wake_export,"qualified synchronization exports resolve");
    using Wait = KYTY_SYSV_ABI int (*)(volatile uint32_t*,uint32_t,const uint32_t*);
    using Wake = KYTY_SYSV_ABI int (*)(volatile void*,int32_t);
    uint32_t word=1, timeout=0;
    const auto wait=reinterpret_cast<Wait>(wait_export->vaddr);
    const auto wake=reinterpret_cast<Wake>(wake_export->vaddr);
    Check(wait(&word,0,&timeout)==0,"qualified address wait observes mismatched value");
    Check(wait(&word,1,&timeout)==Libs::LibKernel::KERNEL_ERROR_ETIMEDOUT,"qualified address wait preserves kernel timeout");
    Check(wake(&word,0)==0,"qualified address wake supports zero waiters");
    Loader::SymbolResolve sched_key{"P41kTWUS3EI","libkernel",1,"libkernel",1,1,Loader::SymbolType::Func};
    const auto* sched_export=symbols.Find(sched_key);
    Check(sched_export!=nullptr,"qualified pthread scheduling query resolves");
    using GetSched = KYTY_SYSV_ABI int (*)(Libs::LibKernel::Pthread,int*,Libs::LibKernel::KernelSchedParam*);
    *Libs::Posix::GetErrorAddr()=123;
    Check(reinterpret_cast<GetSched>(sched_export->vaddr)(nullptr,nullptr,nullptr)==Libs::Posix::POSIX_EINVAL
        && *Libs::Posix::GetErrorAddr()==123,"pthread scheduling query returns its error directly without modifying errno");
    sched_key.name="oIRFTjoILbg";
    const auto* set_sched_export=symbols.Find(sched_key);
    Check(set_sched_export!=nullptr,"qualified pthread scheduling update resolves");
    using SetSched = KYTY_SYSV_ABI int (*)(Libs::LibKernel::Pthread,int,const Libs::LibKernel::KernelSchedParam*);
    Check(reinterpret_cast<SetSched>(set_sched_export->vaddr)(nullptr,0,nullptr)==Libs::Posix::POSIX_EINVAL
        && *Libs::Posix::GetErrorAddr()==123,"pthread scheduling update preserves direct-error ABI");
    const auto root = std::filesystem::temp_directory_path() /
        ("prosperox-posix-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(root),"create POSIX fixture mount");
    std::ofstream(root/"payload") << "synthetic";
    F::Mount(root,"/phase6posix");
    auto* error = Libs::Posix::GetErrorAddr();
    const auto writable=open("/phase6posix/payload",2,0);
    Check(writable>=3,"open writable mounted file");
    *error=123;
    Check(write(writable,"checkpoint",10)==10 && *error==123,
          "qualified POSIX write returns byte count and preserves errno on success");
    Check(seek(writable,0,0)==0,"rewind written file");
    char written[10]{};
    Check(F::KernelRead(writable,written,10)==10 && std::memcmp(written,"checkpoint",10)==0,
          "qualified write publishes actual file bytes");
    Check(close(writable)==0,"close writable file");
    Check(write(writable,"x",1)==-1 && *error==Libs::Posix::POSIX_EBADF,
          "qualified POSIX write translates a closed-descriptor error");
    std::ofstream(root/"payload",std::ios::trunc) << "synthetic";
    *error=0;
    Check(open("/phase6posix/missing",0,0)==-1 && *error==Libs::Posix::POSIX_ENOENT,
        "POSIX open translates kernel failure to minus one and guest errno");
    const auto fd=open("/phase6posix/payload",0,0);
    Check(fd>=3,"POSIX open returns a real mounted descriptor");
    Check(seek(fd,3,0)==3,"POSIX lseek preserves the successful offset");
    F::FileStat info{};
    Check(stat(fd,&info)==0,"POSIX fstat succeeds on the real descriptor");
    Check(close(fd)==0,"POSIX close releases the real descriptor");
    Check(close(fd)==-1 && *error==Libs::Posix::POSIX_EBADF,"POSIX close preserves bad-descriptor error");
    Check(seek(-1,0,0)==-1 && *error==Libs::Posix::POSIX_EBADF,"POSIX lseek translates failure");
    Check(stat(-1,&info)==-1 && *error==Libs::Posix::POSIX_EBADF,"POSIX fstat translates failure");
    Check(F::KernelOpen("/phase6posix/missing",0,0)==static_cast<int>(0x80020002),
        "kernel open retains its distinct kernel error convention");
    Check(unlink("/phase6posix/missing")==-1 && *error==Libs::Posix::POSIX_ENOENT,
        "unlink absent file returns minus one and ENOENT");
    Check(unlink("/phase6posix")==-1 && *error==Libs::Posix::POSIX_EPERM && std::filesystem::exists(root),
        "unlink refuses directories");
    Check(unlink("/phase6posix/../payload")==-1 && std::filesystem::exists(root/"payload"),
        "unlink cannot escape a mount");
    const auto retained=open("/phase6posix/payload",0,0);
    Check(retained>=3,"open file before unlink");
    *error=123;
    Check(unlink("/phase6posix/payload")==0 && *error==123,"unlink success preserves errno");
    Check(open("/phase6posix/payload",0,0)==-1 && *error==Libs::Posix::POSIX_ENOENT,
        "removed name no longer opens");
    char retained_bytes[9]{};
    Check(F::KernelRead(retained,retained_bytes,9)==9 && std::memcmp(retained_bytes,"synthetic",9)==0,
        "open descriptor retains unlinked contents");
    Check(close(retained)==0 && !std::filesystem::exists(root/"payload"),"close releases unlinked storage");
    std::ofstream(root/"closed") << "closed";
    Check(unlink("/phase6posix/closed")==0 && !std::filesystem::exists(root/"closed"),"unlink closed file");
    F::Umount("/phase6posix");
    std::filesystem::remove(root/"payload");
    std::filesystem::remove(root);
    std::puts("PHASE6_POSIX_PASS qualified open/close/write/lseek/fstat/unlink, real descriptors, error translation");
    return 0;
}
}
