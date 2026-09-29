#include <assert.h>
#include <string.h>
#define BRIDGE_TEST
#include "../src/xbox_bridge.c"

static s64 clock_ms;
static int present[MAX_EVENTS], disconnected[MAX_EVENTS], unsupported[MAX_EVENTS];
static char parent_name[MAX_EVENTS];
static int scans[MAX_EVENTS], closed[512], grabbed[512], destroyed[512];
static int fail_write_fd;
static struct input_event queued[512][8];
static int queued_count[512];
static struct input_event forwarded[512][16];
static int forwarded_count[512];
static u8 reports[512][9];
static int report_count[512];
static u16 uploaded_strong[512],uploaded_weak[512];
static s64 last_timeout_ms;

long test_source_candidate(int i,char *parent){
    scans[i]++;
    if(!present[i])return -1;
    parent[0]=parent_name[i];parent[1]=0;
    return 100+i;
}
long test_hidraw(const char *parent,enum report_profile *profile){
    for(int i=0;i<MAX_EVENTS;i++)if(present[i]&&parent_name[i]==parent[0]&&unsupported[i])return -1;
    *profile=REPORT_TTMAX_9;
    return 200+parent[0]-'A';
}
long test_virtual_pad(long src,int slot){(void)src;return 300+slot;}

long test_sc3(long n,long a,long b,long c){
    if(n==SYS_clock_gettime){
        struct timespec *t=(struct timespec *)b;
        t->sec=clock_ms/1000;t->nsec=(clock_ms%1000)*1000000;return 0;
    }
    if(n==SYS_close){closed[a]++;return 0;}
    if(n==SYS_write){
        if(a==fail_write_fd)return -5;
        if(a>=200&&a<300){
            assert(c<=9);memcpy(reports[a],(void *)b,(unsigned long)c);report_count[a]++;return c;
        }
        if(a>=300&&a<400){
            assert(forwarded_count[a]<16);
            forwarded[a][forwarded_count[a]++]=*(struct input_event *)b;return c;
        }
        return c;
    }
    if(n==SYS_read){
        if(a<0||a>=512||!queued_count[a])return -11;
        int bytes=queued_count[a]*(int)sizeof(struct input_event);
        assert(bytes<=c);memcpy((void *)b,queued[a],bytes);queued_count[a]=0;return bytes;
    }
    if(n==SYS_ioctl){
        if((u64)b==EVIOCGRAB){grabbed[a]=(int)c;return 0;}
        if((u64)b==_IO(2)){destroyed[a]++;return 0;}
        if((u64)b==_IOWR(200,104)){
            struct upload *u=(struct upload *)c;
            u->effect.type=FF_RUMBLE;u->effect.id=0;u->effect.length=1000;
            u->effect.data[0]=(u8)uploaded_strong[a];u->effect.data[1]=(u8)(uploaded_strong[a]>>8);
            u->effect.data[2]=(u8)uploaded_weak[a];u->effect.data[3]=(u8)(uploaded_weak[a]>>8);
            return 0;
        }
        return 0;
    }
    return -1;
}
long test_sc4(long n,long a,long b,long c,long d){
    (void)d;
    assert(n==SYS_ppoll);
    struct pollfd *pf=(struct pollfd *)a;
    struct timespec *timeout=(struct timespec *)c;
    last_timeout_ms=timeout->sec*1000+timeout->nsec/1000000;
    int ready=0;
    for(int i=0;i<b;i++){
        pf[i].revents=0;
        if(pf[i].fd>=100&&pf[i].fd<164&&disconnected[pf[i].fd-100])pf[i].revents=16;
        else if(queued_count[pf[i].fd])pf[i].revents=1;
        if(pf[i].revents)ready++;
    }
    if(!ready)clock_ms+=last_timeout_ms;
    return ready;
}
static void push(int fd,u16 type,u16 code,s32 value){
    assert(queued_count[fd]<8);
    queued[fd][queued_count[fd]++]=(struct input_event){.type=type,.code=code,.value=value};
}
static void reset(void){
    memset(controllers,0,sizeof(controllers));next_scan=0;clock_ms=100;
    memset(present,0,sizeof(present));memset(disconnected,0,sizeof(disconnected));
    memset(unsupported,0,sizeof(unsupported));memset(parent_name,0,sizeof(parent_name));
    memset(scans,0,sizeof(scans));memset(closed,0,sizeof(closed));
    memset(grabbed,0,sizeof(grabbed));memset(destroyed,0,sizeof(destroyed));
    memset(queued_count,0,sizeof(queued_count));
    memset(forwarded_count,0,sizeof(forwarded_count));
    memset(report_count,0,sizeof(report_count));
    memset(uploaded_strong,0,sizeof(uploaded_strong));
    memset(uploaded_weak,0,sizeof(uploaded_weak));fail_write_fd=-1;
    last_timeout_ms=-1;
}
int main(void){
    reset();
    assert(bridge_step()==0);
    assert(scans[0]==1&&scans[63]==1&&last_timeout_ms==2000);
    assert(clock_ms==2100);

    reset();
    present[0]=1;parent_name[0]='X';unsupported[0]=1;
    present[1]=1;parent_name[1]='A';
    present[2]=1;parent_name[2]='B';
    present[3]=1;parent_name[3]='A';
    assert(bridge_step()==0);
    assert(controllers[0].active&&controllers[0].event_index==1);
    assert(controllers[1].active&&controllers[1].event_index==2);
    assert(closed[103]==1&&report_count[200]==1&&report_count[201]==1);

    uploaded_strong[300]=65535;uploaded_weak[301]=32768;
    push(300,EV_UINPUT,UI_FF_UPLOAD,1);
    push(301,EV_UINPUT,UI_FF_UPLOAD,1);
    assert(bridge_step()==0);
    push(300,EV_FF,0,1);push(301,EV_FF,0,1);
    push(101,EV_KEY,304,1);push(102,EV_ABS,0,17);
    assert(bridge_step()==0);
    assert(reports[200][4]==255&&reports[200][5]==0);
    assert(reports[201][4]==0&&reports[201][5]==128);
    assert(forwarded_count[300]==1&&forwarded[300][0].code==304);
    assert(forwarded_count[301]==1&&forwarded[301][0].value==17);

    push(300,EV_FF,0,0);assert(bridge_step()==0);
    assert(reports[200][4]==0&&reports[201][5]==128);

    push(300,EV_FF,0,1);assert(bridge_step()==0);
    assert(reports[200][4]==255);
    assert(bridge_step()==0);
    assert(last_timeout_ms==1000&&reports[200][4]==0);

    disconnected[1]=1;present[1]=0;
    assert(bridge_step()==0);
    assert(!controllers[0].active&&controllers[1].active);
    assert(!grabbed[101]&&destroyed[300]==1&&closed[101]==1);
    disconnected[1]=0;present[3]=0;present[4]=1;parent_name[4]='A';
    clock_ms=next_scan;assert(bridge_step()==0);
    assert(controllers[0].active&&controllers[0].event_index==4);
    assert(controllers[1].active);

    controllers[0].effects[0]=(struct rumble_effect){.strong=65535,.length=1000};
    fail_write_fd=200;push(300,EV_FF,0,1);
    assert(bridge_step()==0);
    assert(!controllers[0].active&&controllers[1].active);

    disconnected[2]=1;present[2]=0;
    assert(bridge_step()==0);
    assert(!controllers[1].active&&reports[201][4]==0&&reports[201][5]==0);

    reset();
    for(int i=0;i<9;i++){present[i]=1;parent_name[i]=(char)('A'+i);}
    assert(bridge_step()==0);
    for(int i=0;i<MAX_CONTROLLERS;i++)assert(controllers[i].active);
    assert(scans[8]==0);
    return 0;
}
