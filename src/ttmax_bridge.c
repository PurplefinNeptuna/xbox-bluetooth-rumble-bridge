// TT Max Android Bluetooth -> Android uinput gamepad with FF_RUMBLE.
// Standalone arm64 Linux program; built without libc for the tablet.
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long u64;
typedef signed long s64;

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
static long op(const char *p,int flags){return sc4(SYS_openat,AT_FDCWD,(long)p,flags,0);}
static long rd(long f,void *b,u64 n){return sc3(SYS_read,f,(long)b,n);}
static long wr(long f,const void *b,u64 n){return sc3(SYS_write,f,(long)b,n);}
static long io(long f,u64 req,long arg){return sc3(SYS_ioctl,f,req,arg);}
static void closefd(long f){if(f>=0)sc3(SYS_close,f,0,0);}
static void say(const char *s){u64 n=0;while(s[n])n++;wr(2,s,n);}
static void zero(void *p,u64 n){u8 *b=p;for(u64 i=0;i<n;i++)b[i]=0;}
static u64 len(const char *s){u64 n=0;while(s[n])n++;return n;}
static void copy(char *d,const char *s){while((*d++=*s++));}
static int contains(const char *s,const char *n){for(;*s;s++){const char *a=s,*b=n;while(*a&&*b&&*a==*b){a++;b++;}if(!*b)return 1;}return 0;}
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

static u16 strong[16],weak[16],length_ms[16];
static long hid=-1;
static s64 stop_at=0;
static u8 playing_strong=0,playing_weak=0;
static s64 now_ms(void){struct timespec t;if(sc3(SYS_clock_gettime,CLOCK_MONOTONIC,(long)&t,0)<0)return 0;return t.sec*1000+t.nsec/1000000;}
static void rumble(u8 a,u8 b){
    // Report 3, both main motors enabled. Explicit zero report is required.
    u8 pkt[9]={3,3,0,0,a,b,0,0,1};
    if(hid>=0&&wr(hid,pkt,9)==9){playing_strong=a;playing_weak=b;}
}
static void stop(void){if(playing_strong||playing_weak)rumble(0,0);stop_at=0;}
static int bit(const u8 *b,int k){return (b[k/8]>>(k%8))&1;}
static long source(void){
    char path[48],num[12],name[96];struct input_id id;
    for(int i=0;i<64;i++){
        copy(path,"/dev/input/event");number(num,i);copy(path+len(path),num);
        long f=op(path,O_RDONLY);
        if(f<0)continue;
        zero(&id,sizeof(id));zero(name,sizeof(name));
        if(io(f,EVIOCGID,(long)&id)>=0&&id.bustype==5&&id.vendor==0x045e&&id.product==0x02fd&&
           io(f,EVIOCGNAME(96),(long)name)>0&&contains(name,"GuliKit Controller AD"))return f;
        closefd(f);
    }
    return -1;
}
static long hidraw(void){
    char path[80],num[12],link[256];
    for(int i=0;i<64;i++){
        copy(path,"/sys/class/hidraw/hidraw");number(num,i);copy(path+len(path),num);copy(path+len(path),"/device");
        long n=sc4(SYS_readlinkat,AT_FDCWD,(long)path,(long)link,255);
        if(n<=0)continue;link[n]=0;
        if(contains(link,"045E:02FD")||contains(link,"045e:02fd")){
            copy(path,"/dev/hidraw");copy(path+len(path),num);
            return op(path,O_WRONLY);
        }
    }
    return -1;
}
static long virtual_pad(long src){
    long f=op("/dev/uinput",O_RDWR|O_NONBLOCK);if(f<0)return f;
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
    copy(s.name,"TT Max Rumble Bridge");s.max_effects=16;
    if(io(f,_IOW(3,92),(long)&s)<0||io(f,_IO(1),0)<0)goto fail;
    return f;
fail:closefd(f);return -1;
}
static void handle_ff(long pad,struct input_event *e){
    if(e->type==EV_UINPUT&&e->code==UI_FF_UPLOAD){
        struct upload u;zero(&u,sizeof(u));u.request_id=e->value;
        if(io(pad,_IOWR(200,104),(long)&u)>=0){
            int id=u.effect.id;
            if(id>=0&&id<16&&u.effect.type==FF_RUMBLE){
                strong[id]=(u16)u.effect.data[0]|((u16)u.effect.data[1]<<8);
                weak[id]=(u16)u.effect.data[2]|((u16)u.effect.data[3]<<8);
                length_ms[id]=u.effect.length;
                u.retval=0;
            }else u.retval=-22;
            io(pad,_IOW(201,104),(long)&u);
        }
    }else if(e->type==EV_UINPUT&&e->code==UI_FF_ERASE){
        struct erase r;zero(&r,sizeof(r));r.request_id=e->value;
        if(io(pad,_IOWR(202,12),(long)&r)>=0){
            if(r.effect_id<16){strong[r.effect_id]=weak[r.effect_id]=length_ms[r.effect_id]=0;r.retval=0;}
            else r.retval=-22;
            io(pad,_IOW(203,12),(long)&r);
        }
    }else if(e->type==EV_FF){
        int id=e->code;
        if(id>=0&&id<16&&e->value>0){
            u8 a=strong[id]>>8,b=weak[id]>>8;
            rumble(a,b);
            int duration=length_ms[id];if(duration<50)duration=50;if(duration>1000)duration=1000;
            stop_at=now_ms()+duration;
        }else if(e->value==0)stop();
    }
}
static int run(void){
    long src=source();if(src<0)return 1;
    hid=hidraw();if(hid<0){say("TT Max Bluetooth hidraw not accessible\n");closefd(src);return 2;}
    rumble(0,0);
    long pad=virtual_pad(src);if(pad<0){say("Could not create uinput pad\n");closefd(src);closefd(hid);return 3;}
    if(io(src,EVIOCGRAB,1)<0){say("Could not grab original gamepad\n");io(pad,_IO(2),0);closefd(pad);closefd(src);closefd(hid);return 4;}
    say("TT Max rumble bridge active\n");
    struct pollfd pf[2]={{(s32)src,1,0},{(s32)pad,1,0}};
    while(1){
        struct timespec timeout={0,20000000};
        long n=sc4(SYS_ppoll,(long)pf,2,(long)&timeout,0);
        if(n<0){say("poll failed\n");break;}
        if(pf[0].revents&1){
            struct input_event ev[32];long count=rd(src,ev,sizeof(ev));
            if(count<=0)break;
            for(long i=0;i<count/(long)sizeof(ev[0]);i++){
                if(ev[i].type==EV_KEY||ev[i].type==EV_ABS||ev[i].type==0)
                    wr(pad,&ev[i],sizeof(ev[i]));
            }
        }
        if(pf[1].revents&1){
            struct input_event ev[32];long count=rd(pad,ev,sizeof(ev));
            for(long i=0;i<count/(long)sizeof(ev[0]);i++)handle_ff(pad,&ev[i]);
        }
        if(stop_at&&now_ms()>=stop_at)stop();
        if((pf[0].revents|pf[1].revents)&(8|16|32))break;
    }
    stop();io(src,EVIOCGRAB,0);io(pad,_IO(2),0);closefd(pad);closefd(src);closefd(hid);
    return 5;
}
void _start(void){int code=run();sc3(SYS_exit,code,0,0);for(;;);}
