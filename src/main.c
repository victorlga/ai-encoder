#include <asf.h>
#include "conf_board.h"

#include "gfx_mono_ug_2832hsweg04.h"
#include "gfx_mono_text.h"
#include "sysfont.h"

#define CLK_PIO         PIOD                   
#define CLK_PIO_ID      ID_PIOD
#define CLK_PIO_IDX     30
#define CLK_IDX_MASK (1u << CLK_PIO_IDX)

#define DT_PIO         PIOA
#define DT_PIO_ID      ID_PIOA
#define DT_PIO_IDX     6
#define DT_IDX_MASK (1u << DT_PIO_IDX)

#define SW_PIO         PIOC
#define SW_PIO_ID      ID_PIOC
#define SW_PIO_IDX     19
#define SW_IDX_MASK (1u << SW_PIO_IDX)

#define LED_PIO           PIOC
#define LED_PIO_ID        ID_PIOC
#define LED_PIO_IDX       8
#define LED_PIO_IDX_MASK  (1 << LED_PIO_IDX)



/** RTOS  */
#define TASK_OLED_STACK_SIZE                (1024*6/sizeof(portSTACK_TYPE))
#define TASK_OLED_STACK_PRIORITY            (tskIDLE_PRIORITY)

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask,  signed char *pcTaskName);
extern void vApplicationIdleHook(void);
extern void vApplicationTickHook(void);
extern void vApplicationMallocFailedHook(void);
extern void xPortSysTickHandler(void);

/** prototypes */
void but_callback(void);
static void encoder_init(void);
void CLK_callback(void);
void SW_callback(void);
static void task_plin(void *pvParameters);
static void task_oled(void *pvParameters);

/************************************************************************/
/* recursos RTOS                                                        */
/************************************************************************/

SemaphoreHandle_t xSemaphoreH;
SemaphoreHandle_t xSemaphoreA;
QueueHandle_t xQueueSW;
QueueHandle_t xQueueSW2;

/************************************************************************/
/* RTOS application funcs                                               */
/************************************************************************/

extern void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName) {
	printf("stack overflow %x %s\r\n", pxTask, (portCHAR *)pcTaskName);
	for (;;) {	}
}

extern void vApplicationIdleHook(void) { }

extern void vApplicationTickHook(void) { }

extern void vApplicationMallocFailedHook(void) {
	configASSERT( ( volatile void * ) NULL );
}


/************************************************************************/
/* handlers / callbacks                                                 */
/************************************************************************/

void CLK_callback(void) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (pio_get(DT_PIO, PIO_INPUT, DT_IDX_MASK)) {
		printf("CLK\n");
		xSemaphoreGiveFromISR(xSemaphoreH, &xHigherPriorityTaskWoken);
	}
	else {
		printf("DT\n");
		xSemaphoreGiveFromISR(xSemaphoreA, &xHigherPriorityTaskWoken);
	}
}

void SW_callback(void) {
	if (!pio_get(SW_PIO, PIO_INPUT, SW_IDX_MASK)) {
		rtt_init(RTT, 1);
	}
	else {
		int ul_previous_time = rtt_read_timer_value(RTT);
		int delay = ul_previous_time / 32768;
		xQueueSendFromISR(xQueueSW, &delay, 0);
		if (delay >= 3) {
			xQueueSendFromISR(xQueueSW2, &delay, 0);
		}
	}
}

/************************************************************************/
/* TASKS                                                                */
/************************************************************************/
char h_sel = 0;

static void task_plin(void *pvParameters) {
	uint32_t delay;
	for (;;)  {
		if (xQueueReceive(xQueueSW, &delay, 0)) {
			if (delay >= 3) {
				for (int i = 0; i<20; i++) {
					pio_set(LED_PIO, LED_PIO_IDX_MASK);
					vTaskDelay(50/portTICK_PERIOD_MS);
					pio_clear(LED_PIO, LED_PIO_IDX_MASK);
					vTaskDelay(50/portTICK_PERIOD_MS);
				}
			} else {
				h_sel++;
				if (h_sel >= 4)
					h_sel = 0;
			}
		}
	}
}

static void task_oled(void *pvParameters) {
	encoder_init();
	gfx_mono_ssd1306_init();

	gfx_mono_draw_string("0x ", 0, 0, &sysfont);
	
	gfx_mono_draw_string("0", 20, 0, &sysfont);

	gfx_mono_draw_string("0", 30, 0, &sysfont);

	gfx_mono_draw_string("0", 40, 0, &sysfont);

	gfx_mono_draw_string("0", 50, 0, &sysfont);
	
	char digits[16] = {'0', '1', '2', '3', '4', '5','6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
	char lcd_pos[4] = {20, 30, 40, 50};
	char h_i[4] = {0,0,0,0};
	
	for (;;)  {
		
		if (xSemaphoreTake(xSemaphoreH, 0)) {
			printf("Recebeu CLK\n");
			char val = h_i[h_sel];
			val++;
			
			if (val >= 15)
				val = 0;
			h_i[h_sel] = val;
			char str;
			printf("%c\n", digits[val]);
			sprintf(str, "%c", digits[val]);
			gfx_mono_draw_string(str, lcd_pos[h_sel], 0, &sysfont);
		}
		
		////////////////////////////////////////////////////
		
		if (xSemaphoreTake(xSemaphoreA, 0)) {
			printf("Recebeu DT\n");
			char val = h_i[h_sel];
			val--;
			
			if (val <= 0)
				val = 15;
				
			h_i[h_sel] = val;
			char str;
			printf("%c\n", digits[val]);
			sprintf(str, "%c", digits[val]);
			gfx_mono_draw_string(str, lcd_pos[h_sel], 0, &sysfont);
		}
		
		vTaskDelay(120/portTICK_PERIOD_MS);
	}
}

/************************************************************************/
/* funcoes                                                              */
/************************************************************************/

static void configure_console(void) {
	const usart_serial_options_t uart_serial_options = {
		.baudrate = CONF_UART_BAUDRATE,
		.charlength = CONF_UART_CHAR_LENGTH,
		.paritytype = CONF_UART_PARITY,
		.stopbits = CONF_UART_STOP_BITS,
	};

	/* Configure console UART. */
	stdio_serial_init(CONF_UART, &uart_serial_options);

	/* Specify that stdout should not be buffered. */
	setbuf(stdout, NULL);
}

static void LED_init(void) {
	pmc_enable_periph_clk(LED_PIO_ID);
	pio_set_output(LED_PIO, LED_PIO_IDX_MASK, 0, 0, 0);	
}

static void encoder_init(void) {
	// Inicializa clock do periférico PIO responsavel pelo botao
	pmc_enable_periph_clk(CLK_PIO);
	pmc_enable_periph_clk(DT_PIO_ID);
	pmc_enable_periph_clk(SW_PIO_ID);

	// Configura PIO do encoder como entrada com pullup + debounce
	pio_configure(CLK_PIO, PIO_INPUT, CLK_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(CLK_PIO, CLK_IDX_MASK, 60);

	pio_configure(DT_PIO, PIO_INPUT, DT_IDX_MASK, PIO_DEBOUNCE);
	pio_set_debounce_filter(DT_PIO, DT_IDX_MASK, 60);

	pio_configure(SW_PIO, PIO_INPUT, SW_IDX_MASK, PIO_PULLUP | PIO_DEBOUNCE);
	pio_set_debounce_filter(SW_PIO, SW_IDX_MASK, 60);
	
	// Configura interrupção no pino referente ao botao e associa
	// função de callback caso uma interrupção for gerada
	pio_handler_set(CLK_PIO,
					CLK_PIO_ID,
					CLK_IDX_MASK,
					PIO_IT_FALL_EDGE,
					&CLK_callback);

	pio_handler_set(SW_PIO,
					SW_PIO_ID,
					SW_IDX_MASK,
					PIO_IT_EDGE,
					&SW_callback);

	// Ativa interrupção e limpa primeira IRQ gerada na ativacao
	pio_enable_interrupt(CLK_PIO, CLK_IDX_MASK);
	pio_get_interrupt_status(CLK_PIO);

	pio_enable_interrupt(SW_PIO, SW_IDX_MASK);
	pio_get_interrupt_status(SW_PIO);

	// Configura NVIC para receber interrupcoes do PIO do encoder

	NVIC_EnableIRQ(CLK_PIO_ID);
	NVIC_SetPriority(CLK_PIO_ID, 4);

	NVIC_EnableIRQ(SW_PIO_ID);
	NVIC_SetPriority(SW_PIO_ID, 4);
}

/************************************************************************/
/* main                                                                 */
/************************************************************************/


int main(void) {
	/* Initialize the SAM system */
	sysclk_init();
	board_init();
	LED_init();

	/* Initialize the console uart */
	configure_console();
	
	xSemaphoreA = xSemaphoreCreateBinary();
	if (xSemaphoreA == NULL)
	printf("falha em criar o semaforo A\n");
	
	xSemaphoreH = xSemaphoreCreateBinary();
	if (xSemaphoreH == NULL)
		printf("falha em criar o semaforo H\n");
		
	xQueueSW = xQueueCreate(32, sizeof(uint32_t));
	if (xQueueSW == NULL)
		printf("falha em criar o semaforo SW\n");
		
	xQueueSW2 = xQueueCreate(32, sizeof(uint32_t));
	if (xQueueSW == NULL)
		printf("falha em criar o semaforo SW\n");
	
	/* Create task to control oled */
	if (xTaskCreate(task_oled, "oled", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
	  printf("Failed to create oled task\r\n");
	}
	
	if (xTaskCreate(task_plin, "plin", TASK_OLED_STACK_SIZE, NULL, TASK_OLED_STACK_PRIORITY, NULL) != pdPASS) {
		printf("Failed to create plin task\r\n");
	}

	/* Start the scheduler. */
	vTaskStartScheduler();

  /* RTOS não deve chegar aqui !! */
	while(1){}

	/* Will only get here if there was insufficient memory to create the idle task. */
	return 0;
}
