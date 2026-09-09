#include "kernel/eventFlag.h"
#include "common/assert.h"
#include "libs/errno.h"
#include "libs/libs.h"
#include "kernel/handleRegistry.h"
#include "kernel/waitDeadline.h"

namespace Libs::LibKernel::EventFlag {
LIB_NAME("libkernel", "libkernel");
class KernelEventFlagPrivate {
public:
 enum class Result { Ok, AlreadyWaiting, TimedOut, Canceled, Deleted };
 enum class ClearMode { None, All, Bits }; enum class WaitMode { And, Or };
 KernelEventFlagPrivate(std::string n,bool s,bool f,uint64_t b):name(std::move(n)),single(s),fifo(f),bits(b){}
 ~KernelEventFlagPrivate(){Close();}
 void Close(){Common::LockGuard l(m); if(closed)return; closed=true; status=Result::Deleted; cv.SignalAll();}
 void Set(uint64_t b){Common::LockGuard l(m);if(!closed){bits|=b;status=Result::Ok;cv.SignalAll();}}
 void Clear(uint64_t b){Common::LockGuard l(m);if(!closed)bits&=b;}
 void Cancel(uint64_t b,int* n){Common::LockGuard l(m);if(n)*n=waiters;if(!closed){bits=b;status=Result::Canceled;cv.SignalAll();}}
 Result Wait(uint64_t want,WaitMode wm,ClearMode cm,uint64_t* out,uint32_t* micros){
  Common::LockGuard l(m); if(single&&waiters)return Result::AlreadyWaiting; WaitSupport::Deadline d(micros);
  auto match=[&]{return wm==WaitMode::And?(bits&want)==want:(bits&want)!=0;};
  while(!match()) { if(closed){if(out)*out=bits;d.Update(micros);return Result::Deleted;} if(status==Result::Canceled){if(out)*out=bits;d.Update(micros);status=Result::Ok;return Result::Canceled;} if(d.Expired()){if(out)*out=bits;d.Update(micros);return Result::TimedOut;} ++waiters; WaitSupport::Park(cv,m,d.Slice()); --waiters; }
  if(out)*out=bits; d.Update(micros); if(cm==ClearMode::All)bits=0;else if(cm==ClearMode::Bits)bits&=~want; return Result::Ok;
 }
 Result Poll(uint64_t b,WaitMode w,ClearMode c,uint64_t* o){uint32_t t=0;return Wait(b,w,c,o,&t);}
private: Common::Mutex m; Common::CondVar cv; std::string name; bool single=true,fifo=true,closed=false; int waiters=0; Result status=Result::Ok; uint64_t bits=0;
};
namespace { struct Decoded{KernelEventFlagPrivate::WaitMode wait;KernelEventFlagPrivate::ClearMode clear;}; bool Decode(uint32_t v,Decoded* d){switch(v&0xfu){case 1:d->wait=KernelEventFlagPrivate::WaitMode::And;break;case 2:d->wait=KernelEventFlagPrivate::WaitMode::Or;break;default:return false;}switch(v&0xf0u){case 0:d->clear=KernelEventFlagPrivate::ClearMode::None;break;case 0x10:d->clear=KernelEventFlagPrivate::ClearMode::All;break;case 0x20:d->clear=KernelEventFlagPrivate::ClearMode::Bits;break;default:return false;}return true;} HandleRegistry<KernelEventFlag,KernelEventFlagPrivate> g_flags; }
int KYTY_SYSV_ABI KernelCreateEventFlag(KernelEventFlag* e,const char* n,uint32_t a,uint64_t b,const void* p){if(!e||!n||p||(a&~0x33u))return KERNEL_ERROR_EINVAL;bool s=(a&0xf0u)!=0x20u,f=(a&0xfu)!=2;if((a&0xfu)>2||(a&0xf0u)>0x20u)return KERNEL_ERROR_EINVAL;*e=g_flags.Insert(std::make_shared<KernelEventFlagPrivate>(n,s,f,b));return OK;}
int KYTY_SYSV_ABI KernelDeleteEventFlag(KernelEventFlag e){auto p=g_flags.Remove(e);if(!p)return KERNEL_ERROR_ESRCH;p->Close();return OK;}
int KYTY_SYSV_ABI KernelWaitEventFlag(KernelEventFlag e,uint64_t b,uint32_t v,uint64_t* o,KernelUseconds* t){if(!b)return KERNEL_ERROR_EINVAL;auto p=g_flags.Acquire(e);if(!p)return KERNEL_ERROR_ESRCH;Decoded d;if(!Decode(v,&d))return KERNEL_ERROR_EINVAL;auto r=p->Wait(b,d.wait,d.clear,o,t);return r==KernelEventFlagPrivate::Result::Ok?OK:r==KernelEventFlagPrivate::Result::AlreadyWaiting?KERNEL_ERROR_EPERM:r==KernelEventFlagPrivate::Result::TimedOut?KERNEL_ERROR_ETIMEDOUT:r==KernelEventFlagPrivate::Result::Canceled?KERNEL_ERROR_ECANCELED:KERNEL_ERROR_EACCES;}
int KYTY_SYSV_ABI KernelPollEventFlag(KernelEventFlag e,uint64_t b,uint32_t v,uint64_t* o){if(!b)return KERNEL_ERROR_EINVAL;auto p=g_flags.Acquire(e);if(!p)return KERNEL_ERROR_ESRCH;Decoded d;if(!Decode(v,&d))return KERNEL_ERROR_EINVAL;auto r=p->Poll(b,d.wait,d.clear,o);return r==KernelEventFlagPrivate::Result::Ok?OK:r==KernelEventFlagPrivate::Result::AlreadyWaiting?KERNEL_ERROR_EPERM:KERNEL_ERROR_EBUSY;}
int KYTY_SYSV_ABI KernelSetEventFlag(KernelEventFlag e,uint64_t b){auto p=g_flags.Acquire(e);if(!p)return KERNEL_ERROR_ESRCH;p->Set(b);return OK;}
int KYTY_SYSV_ABI KernelClearEventFlag(KernelEventFlag e,uint64_t b){auto p=g_flags.Acquire(e);if(!p)return KERNEL_ERROR_ESRCH;p->Clear(b);return OK;}
int KYTY_SYSV_ABI KernelCancelEventFlag(KernelEventFlag e,uint64_t b,int* n){auto p=g_flags.Acquire(e);if(!p)return KERNEL_ERROR_ESRCH;p->Cancel(b,n);return OK;}
}
