/*-----------------------------------------------------------
 *
 * MyApp - mAbassi
 *
 *-----------------------------------------------------------*/

#include "../inc/MyApp_MTL2.h"

#include "mAbassi.h"          /* MUST include "SAL.H" and not uAbassi.h        */
#include "Platform.h"         /* Everything about the target platform is here  */
#include "HWinfo.h"           /* Everything about the target hardware is here  */
#include "SysCall.h"          /* System Call layer stuff     */

#include "arm_pl330.h"
#include "dw_i2c.h"
#include "cd_qspi.h"
#include "dw_sdmmc.h"
#include "dw_spi.h"
#include "dw_uart.h"
#include "alt_gpio.h"

#include "gui.h"
#include "game.h"
#include "stdbool.h" // added by simon to print boolean values
MTC2_INFO *myTouch;
VIP_FRAME_READER *myReader;
uint32_t  *myFrameBuffer;
uint32_t *txdata;			// ***ADDED (global variable for msg to send)
SPI_EVENT *lastMsg;			// ***ADDED (global variable for rcvd msg)
int new_lvl;			// ***ADDED

int *flag;						// ***ADDED (global variable for break event)
int *flag2;						// ***ADDED (global variable for break event of the other player)
int *lvl1;						// ***ADDED (global variable for break level)
int *lvl2;						// ***ADDED (global variable for break level of the other player)
/*-----------------------------------------------------------*/


void Task_MTL2_image(void)
{
    MBX_t    *PrtMbx;
    PrtMbx = MBXopen("ImMailbox", 128);
    intptr_t *PtrMsg;
    SEM_t    *PtrSem;
    PtrSem = SEMopen("ImSemaphore");
    while(1){
    	if (MBXget(PrtMbx, PtrMsg, -1) == 0) {  // -1 = always blocks
    		GUI_DeskDraw((LVL*) *PtrMsg);
    		SEMpost(PtrSem);
		}
    }
}
void Task_MTL2(void)
{
    MTX_t    *PrtMtx;
    PrtMtx = MTXopen("Printf Mtx");

    myFrameBuffer = 0x20000000;

    TSKsleep(OS_MS_TO_TICK(500));

    // Initialize global timer
    assert(ALT_E_SUCCESS == alt_globaltmr_init());
    assert(ALT_E_SUCCESS == alt_globaltmr_start());

    MTXlock(PrtMtx, -1);
    printf("Starting MTL2 initialization\n");

    oc_i2c_init(fpga_i2c);
    myTouch = MTC2_Init(fpga_i2c, fpga_mtc2, LCD_TOUCH_INT_IRQ);

    // Enable IRQ for SPI & MTL

    OSisrInstall(GPT_SPI_IRQ, (void *) &spi_CallbackInterrupt);
    GICenable(GPT_SPI_IRQ, 128, 1);
    alt_write_word(SPI_CONTROL, SPI_CONTROL_IRRDY + SPI_CONTROL_IE);
    OSisrInstall(GPT_MTC2_IRQ, (void *) &mtc2_CallbackInterrupt);
    GICenable(GPT_MTC2_IRQ, 128, 1);

    // Enable interruptmask and edgecapture of PIO core for mtc2 irq
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x3);
    alt_write_word(PIOedgecapture_fpga_MTL, 0x3);

    // Mount drive 0 to a mount point
    if (0 != mount(FS_TYPE_NAME_AUTO, "/", 0, "0")) {
        printf("ERROR: cannot mount volume 0\n");
    }

    // List the current directory contents
    cmd_ls();

    printf("\nMTL2 initialization completed\n");
    MTXunlock(PrtMtx);

    bool GO = true;		//***ADDED
    printf("Go is true? : %d\n", GO);
    txdata = (uint32_t *)malloc(sizeof(uint32_t));		//***ADDED
    lastMsg = (SPI_EVENT *)malloc(sizeof(SPI_EVENT));	//***ADDED
    lastMsg->xcoord = 0;
    lastMsg->ycoord = 0;
    flag=(int *)malloc(sizeof(int));						//***ADDED by martin
    *flag=0x00000008;
    flag2=(int *)malloc(sizeof(int));						//***ADDED by martin
    *flag2=0x00000000;
    lvl1=(int *)malloc(sizeof(int));
    *lvl1=0;
    lvl2=(int *)malloc(sizeof(int));
    *lvl2=0;
    while(GO)
    {
    	GUI(myTouch);
    }
    free(txdata);
    free(lastMsg);
    free(flag);
    free(flag2);
    free(lvl1);
    free(lvl2);
}

// Used by i2C_core.c

void delay_us(uint32_t us) {
    uint64_t start_time = alt_globaltmr_get64();
    uint32_t timer_prescaler = alt_globaltmr_prescaler_get() + 1;
    uint64_t end_time;
    alt_freq_t timer_clock;

    assert(ALT_E_SUCCESS == alt_clk_freq_get(ALT_CLK_MPU_PERIPH, &timer_clock));
    end_time = start_time + us * ((timer_clock / timer_prescaler) / ALT_MICROSECS_IN_A_SEC);

    while(alt_globaltmr_get64() < end_time);
}

/*-----------------------------------------------------------*/

/* Align on cache lines if cached transfers */


void Task_DisplayFile(void)
{
	static char g_Buffer[3*100] __attribute__ ((aligned (OX_CACHE_LSIZE)));
    SEM_t    *PtrSem;
    int       FdSrc;
    int       Nrd;
    int       i;

    static const char theFileName[] = "f1.dat";

    PtrSem = SEMopen("MySem_DisplayFile");

    for( ;; )
    {
        SEMwait(PtrSem, -1);    // -1 = Infinite blocking
        SEMreset(PtrSem);

        FdSrc = open(theFileName, O_RDONLY, 0777);
        if (FdSrc >= 0) {
            myFrameBuffer = 0x20000000;

            myFrameBuffer += 800*40; // en y
            myFrameBuffer += 100; // en x

            do {
                Nrd = read(FdSrc, &g_Buffer[0], sizeof(g_Buffer));
                i=0;
                while(i < Nrd) {
                    *myFrameBuffer++ = (g_Buffer[i] << 16) + (g_Buffer[i+1] << 8) + g_Buffer[i+2];
                    i += 3;
                }
                myFrameBuffer += 800-100;

            } while (Nrd >= sizeof(g_Buffer));
            close(FdSrc);
            myFrameBuffer = 0x20000000;
        }
    }
}

bool validData(uint32_t data){
	int flag = (data>>24)&0x000F;
	bool validFlag = (flag==1 || flag==2 || flag==4 || flag==8 || flag==0);
	uint32_t padder_12 = 0xFFF;
	uint16_t xcoord = (uint16_t) ((data >> 12)& padder_12);
	uint16_t ycoord = (uint16_t) (data & padder_12);
	bool validCoord = (xcoord <= 800 && xcoord >= 0 && ycoord <= 480 && ycoord>=0);

	return validCoord && validFlag;
}

/*-----------------------------------------------------------*/

// Structure of SPI_RXDATA / SPI_TXDATA:
// bits [16: 32]		x coordinate of player
// bits [0 : 15]		y coordinate of player
// xxxxxxxxxxxxxxxxyyyyyyyyyyyyyyyy
void spi_CallbackInterrupt (uint32_t icciar, void *context)
{
    uint32_t status = alt_read_word(SPI_STATUS);
    uint32_t rxdata = alt_read_word(SPI_RXDATA);
    // ***ADDED - RECEIVE OTHER PLAYER'S POSITION
    // Empty msg queue if full
	static uint32_t prevrxdata = 0;

    if(prevrxdata != rxdata){ // ***ADDED - TRANSMIT OWN PLAYER'S POSITION
    	printf("SPI_Interrupt - rxdata!=prevrxdata\n");
    	uint32_t padder_12 = 0xFFF;
		MTX_t *RMtx = MTXopen("RXData Mtx");
		MTXlock(RMtx, -1);

		// Convert received msg into SPI_EVENT structure
		if (validData(rxdata)){
			printf("SPI_Interrupt - Valid Data\n");
			*flag2=(rxdata>>24)&0xF;
			*lvl2=rxdata>>28;
			printf("lvl2 :%d\n",*lvl2);
			if(*flag2==0 && *flag==0){
				lastMsg->xcoord = (uint16_t) ((rxdata >> 12)& padder_12);
				lastMsg->ycoord = (uint16_t) (rxdata & padder_12);
			}
	    	printf("SPI_Interrupt - flag = %d, flag2 = %d\n", *flag, *flag2);
		}
		MTXunlock(RMtx);
		prevrxdata = rxdata;
    }

	MTX_t *TMtx = MTXopen("TXData Mtx");
	MTXlock(TMtx, -1);

	alt_write_word(SPI_TXDATA, *txdata);

	MTXunlock(TMtx);

    // Clear the status of SPI core
    alt_write_word(SPI_STATUS, 0x00);
}

/*-----------------------------------------------------------*/

void mtc2_CallbackInterrupt (uint32_t icciar, void *context)
{

    // Clear the interruptmask of PIO core
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x0);

    mtc2_QueryData(myTouch);

    // Enable the interruptmask and edge register of PIO core for new interrupt
    alt_write_word(PIOinterruptmask_fpga_MTL, 0x1);
    alt_write_word(PIOedgecapture_fpga_MTL, 0x1);
}

/*-----------------------------------------------------------*/

void Task_HPS_Led(void)
{
    MTX_t    *PrtMtx;       // Mutex for exclusive access to printf()
    MBX_t    *PrtMbx;
    intptr_t *PtrMsg;

    setup_hps_gpio();   // This is the Adam&Eve Task and we have first to setup the gpio
    button_ConfigureInterrupt();

    PrtMtx = MTXopen("Printf Mtx");
    MTXlock(PrtMtx, -1);
    printf("\n\nDE10-Nano - MyApp_MTL2\n\n");
    printf("Task_HPS_Led running on core #%d\n\n", COREgetID());
    MTXunlock(PrtMtx);

    PrtMbx = MBXopen("MyMailbox", 128);

	for( ;; )
	{
        if (MBXget(PrtMbx, PtrMsg, 0) == 0) {  // 0 = Never blocks
            MTXlock(PrtMtx, -1);
            printf("Receive message (Core = %d)\n", COREgetID());
            MTXunlock(PrtMtx);
        }
        toogle_hps_led();

        TSKsleep(OS_MS_TO_TICK(500));
	}
}

/*-----------------------------------------------------------*/

void Task_FPGA_Led(void)
{
    uint32_t leds_mask;

    alt_write_word(fpga_leds, 0x01);

	for( ;; )
	{
        leds_mask = alt_read_word(fpga_leds);
        if (leds_mask != (0x01 << (LED_PIO_DATA_WIDTH - 1))) {
            // rotate leds
            leds_mask <<= 1;
        } else {
            // reset leds
            leds_mask = 0x1;
        }
        alt_write_word(fpga_leds, leds_mask);

        TSKsleep(OS_MS_TO_TICK(250));
	}
}
/*-----------------------------------------------------------*/

void Task_FPGA_Button(void)
{
    MTX_t    *PrtMtx;       // Mutex for exclusive access to printf()
    MBX_t    *PrtMbx;
    intptr_t  PtrMsg;
    SEM_t    *PtrSem;

    PrtMtx = MTXopen("Printf Mtx");
    PrtMbx = MBXopen("MyMailbox", 1);
    PtrSem = SEMopen("MySemaphore");

    for( ;; )
    {
        SEMwait(PtrSem, -1);    // -1 = Infinite blocking
        SEMreset(PtrSem);
        MTXlock(PrtMtx, -1);
        printf("Receive IRQ from Button and send message (Core = %d)\n", COREgetID());
        MTXunlock(PrtMtx);

        MBXput(PrtMbx, PtrMsg, -1); // -1 = Infinite blocking
    }
}

/*-----------------------------------------------------------*/

void button_CallbackInterrupt (uint32_t icciar, void *context)
{

    uint32_t button;

    button = alt_read_word(PIOdata_fpga_button);

    // Clear the interruptmask of PIO core
    alt_write_word(PIOinterruptmask_fpga_button, 0x0);

    // Enable the interruptmask and edge register of PIO core for new interrupt
    alt_write_word(PIOinterruptmask_fpga_button, 0x3);
    alt_write_word(PIOedgecapture_fpga_button, 0x3);
    SEM_t    *PtrSem;
    PtrSem = SEMopen("MySemaphore");
    SEMpost(PtrSem);

    if (button == 1) {
        PtrSem = SEMopen("MySem_DisplayFile");
        SEMpost(PtrSem);
    }
}

/*-----------------------------------------------------------*/

void button_ConfigureInterrupt( void )
{
    OSisrInstall(GPT_BUTTON_IRQ, (void *) &button_CallbackInterrupt);
    GICenable(GPT_BUTTON_IRQ, 128, 1);

    // Enable interruptmask and edgecapture of PIO core for buttons 0 and 1
    alt_write_word(PIOinterruptmask_fpga_button, 0x3);
    alt_write_word(PIOedgecapture_fpga_button, 0x3);
}

/*-----------------------------------------------------------*/

void setup_hps_gpio()
{
    uint32_t hps_gpio_config_len = 2;
    ALT_GPIO_CONFIG_RECORD_t hps_gpio_config[] = {
        {HPS_LED_IDX  , ALT_GPIO_PIN_OUTPUT, 0, 0, ALT_GPIO_PIN_DEBOUNCE, ALT_GPIO_PIN_DATAZERO},
        {HPS_KEY_N_IDX, ALT_GPIO_PIN_INPUT , 0, 0, ALT_GPIO_PIN_DEBOUNCE, ALT_GPIO_PIN_DATAZERO}
    };

    assert(ALT_E_SUCCESS == alt_gpio_init());
    assert(ALT_E_SUCCESS == alt_gpio_group_config(hps_gpio_config, hps_gpio_config_len));
}

/*-----------------------------------------------------------*/

void toogle_hps_led()
{
    uint32_t hps_led_value = alt_read_word(ALT_GPIO1_SWPORTA_DR_ADDR);
    hps_led_value >>= HPS_LED_PORT_BIT;
    hps_led_value = !hps_led_value;
    hps_led_value <<= HPS_LED_PORT_BIT;
    alt_gpio_port_data_write(HPS_LED_PORT, HPS_LED_MASK, hps_led_value);
}

/*-----------------------------------------------------------*/

void cmd_ls()
{
    DIR_t         *Dinfo;
    struct dirent *DirFile;
    struct stat    Finfo;
    char           Fname[SYS_CALL_MAX_PATH+1];
    char           MyDir[SYS_CALL_MAX_PATH+1];
    struct tm     *Time;

    /* Refresh the current directory path            */
    if (NULL == getcwd(&MyDir[0], sizeof(MyDir))) {
        printf("ERROR: cannot obtain current directory\n");
        return;
    }

    /* Open the dir to see if it's there            */
    if (NULL == (Dinfo = opendir(&MyDir[0]))) {
        printf("ERROR: cannot open directory\n");
        return;
    }

    /* Valid directory, read each entries and print    */
    while(NULL != (DirFile = readdir(Dinfo))) {
        strcpy(&Fname[0], &MyDir[0]);
        strcat(&Fname[0], "/");
        strcat(&Fname[0], &(DirFile->d_name[0]));

        stat(&Fname[0], &Finfo);
        putchar(((Finfo.st_mode & S_IFMT) == S_IFLNK) ? 'l' :
                ((Finfo.st_mode & S_IFMT) == S_IFDIR) ? 'd':' ');
        putchar((Finfo.st_mode & S_IRUSR) ? 'r' : '-');
        putchar((Finfo.st_mode & S_IWUSR) ? 'w' : '-');
        putchar((Finfo.st_mode & S_IXUSR) ? 'x' : '-');

        if ((Finfo.st_mode & S_IFLNK) == S_IFLNK) {
            printf(" (%-9s mnt point)           - ", media_info(DirFile->d_drv));
        }
        else {
            Time = localtime(&Finfo.st_mtime);
            if (Time != NULL) {
                printf(" (%04d.%02d.%02d %02d:%02d:%02d) ", Time->tm_year + 1900,
                       Time->tm_mon,
                       Time->tm_mday,
                       Time->tm_hour,
                       Time->tm_min,
                       Time->tm_sec);
            }
            printf(" %10lu ", Finfo.st_size);
        }
        puts(DirFile->d_name);
    }
    closedir(Dinfo);
}

/*-----------------------------------------------------------*/
