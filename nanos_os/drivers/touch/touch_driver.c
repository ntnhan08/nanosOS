/* NanosOS drivers/touch/touch_driver.c - GT911 capacitive touch via I2C0 */
#include "../../kernel/kernel.h"
#define I2C0_BASE    0x60013000UL
#define I2C0_CTR     (*(volatile uint32_t*)(I2C0_BASE+0x004))
#define I2C0_SR      (*(volatile uint32_t*)(I2C0_BASE+0x008))
#define I2C0_DATA    (*(volatile uint32_t*)(I2C0_BASE+0x01C))
#define I2C0_INT_CLR (*(volatile uint32_t*)(I2C0_BASE+0x024))
#define I2C0_INT_ST  (*(volatile uint32_t*)(I2C0_BASE+0x02C))
#define I2C0_COMD(n) (*(volatile uint32_t*)(I2C0_BASE+0x058+(n)*4))
#define I2C0_SCLLOW  (*(volatile uint32_t*)(I2C0_BASE+0x000))
#define I2C0_SCLHI   (*(volatile uint32_t*)(I2C0_BASE+0x038))
#define GT911_ADDR   0x5D
#define GT911_STATUS 0x814E
#define GT911_PT1    0x814F
#define GT911_ID     0x8140
#define GPIOW1TC     (*(volatile uint32_t*)0x6000400CUL)
#define GPIOW1TS     (*(volatile uint32_t*)0x60004008UL)
#define PIN_TRST 8
#define PIN_TINT 18
typedef struct{int16_t x,y;uint8_t pressure,event;} touch_pt_t;
static touch_pt_t s_pts[5]; static int s_npts;
static k_mutex_t s_lk;
static task_tcb_t s_task; static uint8_t s_stk[2048] ALIGNED(16);

static void i2c0_init(void){
    I2C0_SCLLOW=100; I2C0_SCLHI=97; /* 400kHz Fast mode @ 80MHz */
    I2C0_CTR=BIT(5)|BIT(4);}

static k_err_t gt911_read(uint16_t reg,uint8_t *buf,uint8_t len){
    int c=0;
    I2C0_COMD(c++)=0;          I2C0_DATA=(GT911_ADDR<<1)|0; I2C0_COMD(c++)=0x100|1;
    I2C0_DATA=(uint8_t)(reg>>8);I2C0_COMD(c++)=0x100|1;
    I2C0_DATA=(uint8_t)(reg&0xFF);I2C0_COMD(c++)=0x100|1;
    I2C0_COMD(c++)=0|(1<<11);  I2C0_DATA=(GT911_ADDR<<1)|1; I2C0_COMD(c++)=0x100|1;
    I2C0_COMD(c++)=0x300|len;
    I2C0_COMD(c++)=0x400;
    I2C0_CTR|=BIT(5);
    uint32_t t=10000; while(!(I2C0_INT_ST&BIT(4))&&--t){};
    I2C0_INT_CLR=BIT(4);
    if(!t) return K_ERR_TIMEOUT;
    for(int i=0;i<len;i++) buf[i]=(uint8_t)(I2C0_DATA&0xFF);
    return K_OK;}

static k_err_t gt911_write_byte(uint16_t reg,uint8_t val){
    int c=0;
    I2C0_COMD(c++)=0; I2C0_DATA=(GT911_ADDR<<1)|0; I2C0_COMD(c++)=0x100|1;
    I2C0_DATA=(uint8_t)(reg>>8); I2C0_COMD(c++)=0x100|1;
    I2C0_DATA=(uint8_t)(reg&0xFF);I2C0_COMD(c++)=0x100|1;
    I2C0_DATA=val; I2C0_COMD(c++)=0x100|1;
    I2C0_COMD(c++)=0x400;
    I2C0_CTR|=BIT(5);
    uint32_t t=10000; while(!(I2C0_INT_ST&BIT(4))&&--t){};
    I2C0_INT_CLR=BIT(4);
    return t?K_OK:K_ERR_TIMEOUT;}

static void touch_task(void *arg){ (void)arg;
    while(1){
        task_sleep_ms(16); /* ~60Hz */
        uint8_t st=0;
        if(gt911_read(GT911_STATUS,&st,1)!=K_OK) continue;
        uint8_t n=st&0x0F; bool rdy=(st&BIT(7))!=0;
        if(!rdy||!n){gt911_write_byte(GT911_STATUS,0);
            k_mutex_lock(&s_lk,K_FOREVER); s_npts=0; k_mutex_unlock(&s_lk); continue;}
        if(n>5)n=5;
        uint8_t pb[5*8]; gt911_read(GT911_PT1,pb,n*8);
        gt911_write_byte(GT911_STATUS,0);
        k_mutex_lock(&s_lk,K_FOREVER);
        s_npts=(int)n;
        for(int i=0;i<(int)n;i++){
            uint8_t *p=pb+i*8;
            s_pts[i].x=(int16_t)((p[3]<<8)|p[2]);
            s_pts[i].y=(int16_t)((p[5]<<8)|p[4]);
            s_pts[i].pressure=p[6]; s_pts[i].event=0;}
        k_mutex_unlock(&s_lk);
        /* Forward to GUI and AAP */
        extern void gui_inject_touch(int16_t,int16_t,uint8_t);
        extern void aap_send_touch(int16_t,int16_t,uint8_t);
        if(n>0){
            gui_inject_touch(s_pts[0].x,s_pts[0].y,0);
            aap_send_touch(s_pts[0].x,s_pts[0].y,0);}}}

void touch_driver_init(void){
    i2c0_init();
    k_mutex_init(&s_lk,"touch");
    /* GT911 reset: RST low → INT low → RST high */
    GPIOW1TC=BIT(PIN_TRST)|BIT(PIN_TINT);
    task_sleep_ms(10);
    GPIOW1TS=BIT(PIN_TRST);
    task_sleep_ms(50);
    uint8_t pid[4]={0};
    if(gt911_read(GT911_ID,pid,4)==K_OK)
        KLOGI("TOUCH","GT911 ID: %c%c%c%c",pid[0],pid[1],pid[2],pid[3]);
    else KLOGE("TOUCH","GT911 not found!");
    task_create(&s_task,"touch",touch_task,NULL,s_stk,sizeof(s_stk),5);
    KLOGI("TOUCH","GT911 ready 480x800 5-point");}

int touch_read(touch_pt_t *pts,int max){
    k_mutex_lock(&s_lk,K_FOREVER);
    int n=MIN(s_npts,max);
    for(int i=0;i<n;i++) pts[i]=s_pts[i];
    k_mutex_unlock(&s_lk); return n;}
