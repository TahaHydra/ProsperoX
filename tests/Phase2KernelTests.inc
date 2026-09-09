// Original synthetic kernel contracts. No guest executable or commercial data.
#include "kernel/semaphore.h"
#include "kernel/eventFlag.h"
#include "kernel/waitDeadline.h"
#include "kernel/syncOnAddress.h"
#include "kernel/eventQueue.h"
#include "libs/errno.h"
#include <semaphore>

namespace Phase2 {
using Phase1::Check;
namespace S = Libs::LibKernel::Semaphore;
namespace F = Libs::LibKernel::EventFlag;
namespace W = Libs::LibKernel::WaitSupport;
using namespace Libs::LibKernel;
namespace Q = Libs::LibKernel::EventQueue;

static void ReentrantEventDelete(Q::KernelEqueue queue, Q::KernelEqueueEvent* event) {
    auto* called = static_cast<int*>(event->filter.data);
    ++*called;
    Check(Q::KernelDeleteEvent(queue,event->event.ident,event->event.filter)==KERNEL_ERROR_ENOENT,
        "delete callback observes its event already removed");
    Check(Q::KernelAddUserEvent(queue,91)==OK,"delete callback can re-enter the queue");
}

struct ScopedWaitHooks {
    W::Hooks hooks;
    W::Hooks* previous = W::test_hooks;
    ScopedWaitHooks() { W::test_hooks = &hooks; }
    ~ScopedWaitHooks() { W::test_hooks = previous; }
};

int KernelContracts() {
    S::KernelSema sem = nullptr;
    Check(S::KernelCreateSema(&sem,"phase2",1,0,INT_MAX,nullptr)==OK,"create semaphore");
    Check(S::KernelSignalSema(sem,-1)==KERNEL_ERROR_EINVAL,"negative signal rejected");
    Check(S::KernelSignalSema(sem,INT_MAX)==OK && S::KernelSignalSema(sem,1)==KERNEL_ERROR_EINVAL,
        "semaphore maximum count cannot overflow");
    uint32_t timeout=0;
    Check(S::KernelWaitSema(sem,INT_MAX,&timeout)==OK,"wake before wait is retained");
    {
        uint64_t now=100;
        ScopedWaitHooks scope;
        scope.hooks.now_micros=[&]{return now;};
        scope.hooks.before_park=[&]{now+=1;};
        timeout=3;
        Check(S::KernelWaitSema(sem,1,&timeout)==KERNEL_ERROR_ETIMEDOUT && timeout==0 && now==103,
            "fake-clock semaphore timeout has an exact deadline");
        scope.hooks.before_park=[&]{now+=7; Check(S::KernelSignalSema(sem,1)==OK,"inject boundary signal");};
        timeout=7;
        Check(S::KernelWaitSema(sem,1,&timeout)==OK && timeout==0,
            "a grant linearized before timeout observation wins");
        scope.hooks.before_park=[&]{int count=-1; Check(S::KernelCancelSema(sem,0,&count)==OK && count==1,"cancel registered waiter");};
        timeout=10;
        Check(S::KernelWaitSema(sem,1,&timeout)==KERNEL_ERROR_ECANCELED && timeout==10,"cancel is distinct from timeout");
        scope.hooks.before_park=[&]{Check(S::KernelDeleteSema(sem)==OK,"delete registered semaphore waiter");};
        Check(S::KernelWaitSema(sem,1,nullptr)==KERNEL_ERROR_EACCES,"delete safely releases a pinned waiter");
    }
    const auto stale=sem;
    Check(S::KernelCreateSema(&sem,"replacement",1,1,1,nullptr)==OK && sem!=stale,"semaphore identity is not reused");
    Check(S::KernelPollSema(stale,1)==KERNEL_ERROR_ESRCH && S::KernelDeleteSema(stale)==KERNEL_ERROR_ESRCH,
        "stale semaphore cannot operate on its replacement");
    Check(S::KernelDeleteSema(sem)==OK,"delete replacement semaphore");

    F::KernelEventFlag flag=nullptr;
    Check(F::KernelCreateEventFlag(&flag,"phase2",0x21,3,nullptr)==OK,"create multi-waiter flag");
    uint64_t bits=0;
    Check(F::KernelPollEventFlag(flag,3,0x21,&bits)==OK && bits==3,"AND and clear matched bits");
    Check(F::KernelPollEventFlag(flag,1,1,&bits)==KERNEL_ERROR_EBUSY && bits==0,"cleared event does not succeed");
    Check(F::KernelPollEventFlag(flag,1,0xff,&bits)==KERNEL_ERROR_EINVAL,"invalid wait mode returns error");
    {
        uint64_t now=0;
        ScopedWaitHooks scope;
        scope.hooks.now_micros=[&]{return now;};
        scope.hooks.before_park=[&]{now+=1;};
        timeout=5;
        Check(F::KernelWaitEventFlag(flag,1,1,&bits,&timeout)==KERNEL_ERROR_ETIMEDOUT && now==5 && timeout==0,
            "fake-clock event timeout has an exact deadline");
        scope.hooks.before_park=[&]{int count=-1; Check(F::KernelCancelEventFlag(flag,2,&count)==OK && count==1,"cancel event waiter");};
        Check(F::KernelWaitEventFlag(flag,1,1,&bits,nullptr)==KERNEL_ERROR_ECANCELED && bits==2,"event cancellation reports current bits");
        scope.hooks.before_park=[&]{Check(F::KernelDeleteEventFlag(flag)==OK,"delete event waiter");};
        Check(F::KernelWaitEventFlag(flag,1,1,&bits,nullptr)==KERNEL_ERROR_EACCES,"event deletion safely releases waiter");
    }
    Check(F::KernelSetEventFlag(flag,1)==KERNEL_ERROR_ESRCH,"stale flag is invalid");
    {
        uint64_t now=0;
        ScopedWaitHooks scope;
        scope.hooks.now_micros=[&]{return now;};
        scope.hooks.before_park=[&]{++now;};
        alignas(8) volatile uint64_t word=0;
        timeout=4;
        Check(SyncOnAddress::Wait64(&word,0,&timeout,nullptr)==KERNEL_ERROR_ETIMEDOUT && now==4,
            "address timeout uses the same exact monotonic clock");
        scope.hooks.before_park=[&]{Check(SyncOnAddress::Wake(&word,1)==OK,"wake registered address waiter");};
        Check(SyncOnAddress::Wait64(&word,0,nullptr,nullptr)==OK,"address wake is retained before host park");
        Q::KernelEqueue queue=Q::KERNEL_EQUEUE_INVALID;
        Check(Q::KernelCreateEqueue(&queue,"fake-clock")==OK,"create clock queue");
        scope.hooks.before_park=[&]{++now;};
        const auto start=now;
        Q::KernelEvent event{};
        int count=-1;
        timeout=9;
        Check(Q::KernelWaitEqueue(queue,&event,1,&count,&timeout)==KERNEL_ERROR_ETIMEDOUT && count==0 && now==start+9,
            "queue timeout uses the exact fake-clock deadline");
        int callbacks=0;
        Q::KernelEqueueEvent entry{};
        entry.event.ident=90; entry.event.filter=Q::KERNEL_EVFILT_USER;
        entry.filter.data=&callbacks; entry.filter.delete_event_func=ReentrantEventDelete;
        Check(Q::KernelAddEvent(queue,entry)==OK && Q::KernelDeleteEvent(queue,90,Q::KERNEL_EVFILT_USER)==OK && callbacks==1,
            "reentrant deletion callback runs once without invalidating iteration");
        scope.hooks.before_park=[&]{Check(Q::KernelDeleteEqueue(queue)==OK,"close registered queue waiter");};
        Check(Q::KernelWaitEqueue(queue,&event,1,&count,nullptr)==KERNEL_ERROR_EBADF,"queue close releases pinned wait");
    }
    std::puts("PHASE2_PASS exact clock, wake-before-wait, grant/timeout, cancel/delete, stale handles, count overflow, event masks");
    return 0;
}

int KernelLifecycleStress() {
    uint32_t seed=0x505832;
    constexpr unsigned iterations=10000;
    for (unsigned iteration=0; iteration<iterations; ++iteration) {
        seed^=seed<<13; seed^=seed>>17; seed^=seed<<5;
        const unsigned action=seed%3;
        const bool event=(seed&4)!=0;
        S::KernelSema sem=nullptr;
        F::KernelEventFlag flag=nullptr;
        if (event) Check(F::KernelCreateEventFlag(&flag,"stress",0x21,0,nullptr)==OK,"create stress flag");
        else Check(S::KernelCreateSema(&sem,"stress",1,0,1,nullptr)==OK,"create stress semaphore");
        std::binary_semaphore registered(0), release(0);
        int status=-1;
        std::thread waiter([&]{
            ScopedWaitHooks scope;
            scope.hooks.before_park=[&]{registered.release(); release.acquire();};
            uint64_t bits=0;
            status=event ? F::KernelWaitEventFlag(flag,1,1,&bits,nullptr) : S::KernelWaitSema(sem,1,nullptr);
        });
        Check(registered.try_acquire_for(std::chrono::seconds(2)),"waiter registration is bounded");
        int count=-1;
        int operation=0;
        if (action==0) operation=event ? F::KernelSetEventFlag(flag,1) : S::KernelSignalSema(sem,1);
        if (action==1) operation=event ? F::KernelCancelEventFlag(flag,0,&count) : S::KernelCancelSema(sem,0,&count);
        if (action==2) operation=event ? F::KernelDeleteEventFlag(flag) : S::KernelDeleteSema(sem);
        Check(operation==OK && (action!=1 || count==1),"seeded lifecycle operation succeeds");
        release.release();
        waiter.join();
        const int expected=action==0 ? OK : action==1 ? KERNEL_ERROR_ECANCELED : KERNEL_ERROR_EACCES;
        Check(status==expected,"seeded waiter observes the linearized outcome");
        if (action!=2) Check((event ? F::KernelDeleteEventFlag(flag) : S::KernelDeleteSema(sem))==OK,"release stress object");
        Check((event ? F::KernelSetEventFlag(flag,1) : S::KernelSignalSema(sem,1))==KERNEL_ERROR_ESRCH,
            "retired stress handle cannot re-enter");
        if (iteration%1000==0) std::printf("PHASE2_STRESS seed=0x505832 iteration=%u state=0x%08x\n",iteration,seed);
    }
    std::puts("PHASE2_PASS 10000 seeded real-thread semaphore/event lifecycles, joined workers, stale identities rejected");
    return 0;
}
}
