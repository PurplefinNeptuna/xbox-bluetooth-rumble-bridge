// Bluetooth 045e:02fd -> Android uinput gamepad with FF_RUMBLE.
// Standalone arm64 Linux program; built without libc for the tablet.
#include "bridge_core.h"
typedef signed short s16;
typedef signed int s32;
typedef unsigned long u64;

#define SYS_ioctl 29
#define SYS_openat 56
#define SYS_close 57
#define SYS_read 63
#define SYS_write 64
#define SYS_readlinkat 78
#define SYS_clock_gettime 113
#define SYS_ppoll 73
#define SYS_exit 93
#define AT_FDCWD -100
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_NONBLOCK 04000
#define EV_KEY 1
#define EV_ABS 3
#define EV_FF 0x15
#define EV_UINPUT 0x101
#define FF_RUMBLE 0x50
#define UI_FF_UPLOAD 1
#define UI_FF_ERASE 2
#define CLOCK_MONOTONIC 1
#define MAX_CONTROLLERS 8
#define MAX_EVENTS 64
#define SCAN_INTERVAL_MS 2000

#ifdef BRIDGE_TEST
long test_sc3(long n,long a,long b,long c);
long test_sc4(long n,long a,long b,long c,long d);
static long sc3(long n,long a,long b,long c){return test_sc3(n,a,b,c);}
static long sc4(long n,long a,long b,long c,long d){return test_sc4(n,a,b,c,d);}
#else
static long sc3(long n,long a,long b,long c) {
    register long x0 __asm__("x0")=a, x1 __asm__("x1")=b, x2 __asm__("x2")=c;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x8) : "memory");
    return x0;
}
static long sc4(long n,long a,long b,long c,long d) {
    register long x0 __asm__("x0")=a, x1 __asm__("x1")=b, x2 __asm__("x2")=c, x3 __asm__("x3")=d;
    register long x8 __asm__("x8")=n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x8) : "memory");
    return x0;
}
#endif
static long op(const char *p,int flags){return sc4(SYS_openat,AT_FDCWD,(long)p,flags,0);}
static long rd(long f,void *b,u64 n){return sc3(SYS_read,f,(long)b,n);}
static long wr(long f,const void *b,u64 n){return sc3(SYS_write,f,(long)b,n);}
static long io(long f,u64 req,long arg){return sc3(SYS_ioctl,f,req,arg);}
static void closefd(long f){if(f>=0)sc3(SYS_close,f,0,0);}
static void say(const char *s){u64 n=0;while(s[n])n++;wr(2,s,n);}
static void zero(void *p,u64 n){u8 *b=p;for(u64 i=0;i<n;i++)b[i]=0;}
static u64 len(const char *s){u64 n=0;while(s[n])n++;return n;}
static void copy(char *d,const char *s){while((*d++=*s++));}
static int equal(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static const char *leaf(const char *s){const char *p=s;for(;*s;s++)if(*s=='/')p=s+1;return p;}
static void number(char *p,int i){char b[12];int n=0;do{b[n++]='0'+i%10;i/=10;}while(i);while(n)*p++=b[--n];*p=0;}
static u64 ioc(int dir,int type,int nr,int size){return ((u64)dir<<30)|((u64)size<<16)|((u64)type<<8)|nr;}
#define _IO(n) ioc(0,'U',n,0)
#define _IOW(n,s) ioc(1,'U',n,s)
#define _IOWR(n,s) ioc(3,'U',n,s)
#define EVIOCGBIT(ev,n) ioc(2,'E',0x20+(ev),n)
#define EVIOCGABS(a) ioc(2,'E',0x40+(a),24)
#define EVIOCGID ioc(2,'E',2,8)
#define EVIOCGNAME(n) ioc(2,'E',6,n)
#define EVIOCGRAB ioc(1,'E',0x90,4)

struct input_id{u16 bustype,vendor,product,version;};
struct absinfo{s32 value,minimum,maximum,fuzz,flat,resolution;};
struct input_event{s64 sec,usec;u16 type,code;s32 value;};
struct setup{struct input_id id;char name[80];u32 max_effects;};
struct abssetup{u16 code,pad;struct absinfo info;};
struct ff_effect{u16 type;s16 id;u16 direction;u16 button,interval;u16 length,delay;u8 data[32] __attribute__((aligned(8)));};
struct upload{u32 request_id;s32 retval;struct ff_effect effect,old;};
struct erase{u32 request_id;s32 retval;u32 effect_id;};
struct timespec{s64 sec,nsec;};
struct pollfd{s32 fd;s16 events,revents;};

_Static_assert(sizeof(struct input_event)==24,"input_event ABI");
_Static_assert(sizeof(struct ff_effect)==48,"ff_effect ABI");
_Static_assert(sizeof(struct upload)==104,"uinput upload ABI");

struct controller {
    long src,hid,pad;
    int event_index,active;
    char hid_parent[256];
    enum report_profile profile;
    struct rumble_effect effects[16];
    u16 playing_strong,playing_weak;
};
static struct controller controllers[MAX_CONTROLLERS];
static u8 rejected[MAX_EVENTS];
static s64 next_scan;
static s64 now_ms(void){struct timespec t;if(sc3(SYS_clock_gettime,CLOCK_MONOTONIC,(long)&t,0)<0)return 0;return t.sec*1000+t.nsec/1000000;}
static int rumble(struct controller *c,u16 a,u16 b){
    if(a==c->playing_strong&&b==c->playing_weak)return 0;
    u8 pkt[9];int n=make_report(c->profile,a,b,pkt);
    if(c->hid>=0&&n&&wr(c->hid,pkt,n)==n){c->playing_strong=a;c->playing_weak=b;return 0;}
    say("rumble report write failed\n");return -1;
}
static void stop(struct controller *c){if(c->playing_strong||c->playing_weak)rumble(c,0,0);}
static int update_rumble(struct controller *c,s64 now){u16 a,b;mix_effects(c->effects,16,now,&a,&b);return rumble(c,a,b);}
static int bit(const u8 *b,int k){return (b[k/8]>>(k%8))&1;}
#ifndef BRIDGE_TEST
static long source_candidate(int i,char *hid_parent){
    char path[96],num[12],name[96],parent[256];struct input_id id;
    copy(path,"/dev/input/event");number(num,i);copy(path+len(path),num);
    long f=op(path,O_RDONLY);
    if(f<0)return -1;
    zero(&id,sizeof(id));zero(name,sizeof(name));
    if(io(f,EVIOCGID,(long)&id)>=0&&id.bustype==5&&id.vendor==0x045e&&id.product==0x02fd&&
       io(f,EVIOCGNAME(96),(long)name)>0&&
       !equal(name,"Xbox Bluetooth Rumble Bridge")&& !equal(name,"TT Max Rumble Bridge")){
        u8 keys[96];zero(keys,sizeof(keys));
        if(io(f,EVIOCGBIT(EV_KEY,sizeof(keys)),(long)keys)>=0&&bit(keys,304)){
            copy(path,"/sys/class/input/event");number(num,i);copy(path+len(path),num);
            copy(path+len(path),"/device/device");
            long n=sc4(SYS_readlinkat,AT_FDCWD,(long)path,(long)parent,255);
            if(n>0){parent[n]=0;copy(hid_parent,leaf(parent));return f;}
        }
    }
    closefd(f);
    return -1;
}
static long hidraw(const char *hid_parent,enum report_profile *profile){
    char path[128],num[12],link[256];
    for(int i=0;i<MAX_EVENTS;i++){
        copy(path,"/sys/class/hidraw/hidraw");number(num,i);copy(path+len(path),num);copy(path+len(path),"/device");
        long n=sc4(SYS_readlinkat,AT_FDCWD,(long)path,(long)link,255);
        if(n<=0)continue;link[n]=0;
        if(equal(leaf(link),hid_parent)){
            copy(path,"/sys/class/hidraw/hidraw");copy(path+len(path),num);
            copy(path+len(path),"/device/report_descriptor");
            long d=op(path,O_RDONLY);
            if(d<0)return -3;
            u8 descriptor[2048];long size=rd(d,descriptor,sizeof(descriptor));closefd(d);
            if(size<=0||size==(long)sizeof(descriptor))return -3;
            *profile=report_profile_from_descriptor(descriptor,(u32)size);
            if(*profile==REPORT_UNKNOWN)return -2;
            copy(path,"/dev/hidraw");copy(path+len(path),num);
            long f=op(path,O_WRONLY);
            return f<0?-1:f;
        }
    }
    return -1;
}
static long virtual_pad(long src,int slot){
    long f=op("/dev/uinput",O_RDWR|O_NONBLOCK);if(f<0)return f;
    char phys[32];copy(phys,"xbox-bridge/pad");number(phys+len(phys),slot);
    if(io(f,_IOW(108,sizeof(char*)),(long)phys)<0)goto fail;
    u8 keys[96],axes[8];zero(keys,sizeof(keys));zero(axes,sizeof(axes));
    if(io(src,EVIOCGBIT(EV_KEY,sizeof(keys)),(long)keys)<0||io(src,EVIOCGBIT(EV_ABS,sizeof(axes)),(long)axes)<0)goto fail;
    if(io(f,_IOW(100,4),EV_KEY)<0||io(f,_IOW(100,4),EV_ABS)<0||io(f,_IOW(100,4),EV_FF)<0||io(f,_IOW(107,4),FF_RUMBLE)<0)goto fail;
    for(int k=0;k<768;k++)if(bit(keys,k)&&io(f,_IOW(101,4),k)<0)goto fail;
    for(int a=0;a<64;a++)if(bit(axes,a)){
        struct abssetup s;zero(&s,sizeof(s));s.code=a;
        if(io(src,EVIOCGABS(a),(long)&s.info)<0||io(f,_IOW(4,28),(long)&s)<0)goto fail;
    }
    struct setup s;zero(&s,sizeof(s));
    s.id.bustype=5;s.id.vendor=0x045e;s.id.product=0x02fd;s.id.version=0x0903;
    copy(s.name,"Xbox Bluetooth Rumble Bridge");s.max_effects=16;
    if(io(f,_IOW(3,92),(long)&s)<0||io(f,_IO(1),0)<0)goto fail;
    return f;
fail:closefd(f);return -1;
}
#else
long test_source_candidate(int i,char *hid_parent);
long test_hidraw(const char *hid_parent,enum report_profile *profile);
long test_virtual_pad(long src,int slot);
#define source_candidate test_source_candidate
#define hidraw test_hidraw
#define virtual_pad test_virtual_pad
#endif
static void close_controller(struct controller *c){
    if(!c->active)return;
    stop(c);
    io(c->src,EVIOCGRAB,0);
    io(c->pad,_IO(2),0);
    closefd(c->pad);closefd(c->src);closefd(c->hid);
    zero(c,sizeof(*c));
}
static int active_event(int index){
    for(int i=0;i<MAX_CONTROLLERS;i++)if(controllers[i].active&&controllers[i].event_index==index)return 1;
    return 0;
}
static int active_parent(const char *parent){
    for(int i=0;i<MAX_CONTROLLERS;i++)if(controllers[i].active&&equal(controllers[i].hid_parent,parent))return 1;
    return 0;
}
static int free_slot(void){for(int i=0;i<MAX_CONTROLLERS;i++)if(!controllers[i].active)return i;return -1;}
static void reject(int event,int reason){
    if(rejected[event]==reason)return;
    rejected[event]=(u8)reason;
    if(reason==2)say("Unsupported HID output report 3; leaving controller untouched\n");
    else if(reason==3)say("HID descriptor not readable or invalid\n");
    else if(reason==4)say("Could not create or grab virtual gamepad\n");
    else if(reason==5)say("Initial stop report failed\n");
    else say("Bluetooth hidraw not accessible\n");
}
static void scan_sources(void){
    for(int event=0;event<MAX_EVENTS;event++){
        if(active_event(event))continue;
        int slot=free_slot();if(slot<0)break;
        char parent[256];long src=source_candidate(event,parent);
        if(src<0){rejected[event]=0;continue;}
        if(active_parent(parent)){closefd(src);continue;}
        enum report_profile profile=REPORT_UNKNOWN;
        long hid=hidraw(parent,&profile);
        if(hid<0){reject(event,(int)-hid);closefd(src);continue;}
        u8 clear_report[9];int n=make_report(profile,0,0,clear_report);
        if(n<=0||wr(hid,clear_report,n)!=n){reject(event,5);closefd(hid);closefd(src);continue;}
        long pad=virtual_pad(src,slot);
        if(pad<0){reject(event,4);closefd(hid);closefd(src);continue;}
        if(io(src,EVIOCGRAB,1)<0){reject(event,4);io(pad,_IO(2),0);closefd(pad);closefd(hid);closefd(src);continue;}
        struct controller *c=&controllers[slot];zero(c,sizeof(*c));
        c->src=src;c->hid=hid;c->pad=pad;c->event_index=event;c->profile=profile;c->active=1;
        copy(c->hid_parent,parent);
        rejected[event]=0;
        say("Xbox Bluetooth rumble bridge active\n");
    }
}
static int handle_ff(struct controller *c,struct input_event *e,s64 now){
    long pad=c->pad;
    if(e->type==EV_UINPUT&&e->code==UI_FF_UPLOAD){
        struct upload u;zero(&u,sizeof(u));u.request_id=e->value;
        if(io(pad,_IOWR(200,104),(long)&u)>=0){
            int id=u.effect.id;
            if(id>=0&&id<16&&u.effect.type==FF_RUMBLE){
                c->effects[id].strong=(u16)u.effect.data[0]|((u16)u.effect.data[1]<<8);
                c->effects[id].weak=(u16)u.effect.data[2]|((u16)u.effect.data[3]<<8);
                c->effects[id].length=u.effect.length;
                c->effects[id].delay=u.effect.delay;
                u.retval=0;
            }else u.retval=-22;
            if(io(pad,_IOW(201,104),(long)&u)<0)return -1;
        }else return -1;
    }else if(e->type==EV_UINPUT&&e->code==UI_FF_ERASE){
        struct erase r;zero(&r,sizeof(r));r.request_id=e->value;
        if(io(pad,_IOWR(202,12),(long)&r)>=0){
            if(r.effect_id<16){zero(&c->effects[r.effect_id],sizeof(c->effects[0]));r.retval=0;}
            else r.retval=-22;
            if(io(pad,_IOW(203,12),(long)&r)<0)return -1;
        }else return -1;
    }else if(e->type==EV_FF){
        int id=e->code;
        if(id>=0&&id<16){
            if(e->value>0)play_effect(&c->effects[id],now,e->value);
            else c->effects[id].active=0;
            return update_rumble(c,now);
        }
    }
    return 0;
}
static s64 next_effect_deadline(s64 now){
    s64 deadline=next_scan;
    for(int i=0;i<MAX_CONTROLLERS;i++)if(controllers[i].active)
        for(int j=0;j<16;j++){
            struct rumble_effect *e=&controllers[i].effects[j];
            if(!e->active)continue;
            if(e->start>now&&e->start<deadline)deadline=e->start;
            if(e->end>now&&e->end<deadline)deadline=e->end;
        }
    return deadline;
}
static int read_source(struct controller *c){
    struct input_event ev[32];long count=rd(c->src,ev,sizeof(ev));
    if(count<=0||count%(long)sizeof(ev[0]))return -1;
    for(long i=0;i<count/(long)sizeof(ev[0]);i++)
        if(ev[i].type==EV_KEY||ev[i].type==EV_ABS||ev[i].type==0)
            if(wr(c->pad,&ev[i],sizeof(ev[i]))!=(long)sizeof(ev[i]))return -1;
    return 0;
}
static int read_pad(struct controller *c,s64 now){
    struct input_event ev[32];long count=rd(c->pad,ev,sizeof(ev));
    if(count<=0||count%(long)sizeof(ev[0]))return -1;
    for(long i=0;i<count/(long)sizeof(ev[0]);i++)if(handle_ff(c,&ev[i],now)<0)return -1;
    return 0;
}
static int bridge_step(void){
    s64 now=now_ms();
    if(now>=next_scan){scan_sources();next_scan=now+SCAN_INTERVAL_MS;}
    struct pollfd pf[MAX_CONTROLLERS*2];int slots[MAX_CONTROLLERS*2],kind[MAX_CONTROLLERS*2],count=0;
    for(int i=0;i<MAX_CONTROLLERS;i++)if(controllers[i].active){
        pf[count]=(struct pollfd){(s32)controllers[i].src,1,0};slots[count]=i;kind[count++]=0;
        pf[count]=(struct pollfd){(s32)controllers[i].pad,1,0};slots[count]=i;kind[count++]=1;
    }
    s64 deadline=next_effect_deadline(now);s64 wait=deadline-now;if(wait<0)wait=0;
    struct timespec timeout={wait/1000,(wait%1000)*1000000};
    long ready=sc4(SYS_ppoll,(long)pf,count,(long)&timeout,0);
    if(ready<0){if(ready==-4)return 0;say("poll failed\n");return -1;}
    now=now_ms();
    for(int p=0;p<count;p++){
        struct controller *c=&controllers[slots[p]];
        if(!c->active)continue;
        if(pf[p].revents&(8|16|32)){close_controller(c);continue;}
        if(!(pf[p].revents&1))continue;
        int result=kind[p]?read_pad(c,now):read_source(c);
        if(result<0)close_controller(c);
    }
    for(int i=0;i<MAX_CONTROLLERS;i++)if(controllers[i].active)
        if(update_rumble(&controllers[i],now)<0)close_controller(&controllers[i]);
    return 0;
}
#ifndef BRIDGE_TEST
void _start(void){while(bridge_step()==0){}for(int i=0;i<MAX_CONTROLLERS;i++)close_controller(&controllers[i]);sc3(SYS_exit,1,0,0);for(;;);}
#endif
