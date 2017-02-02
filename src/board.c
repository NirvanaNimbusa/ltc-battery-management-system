#include <string.h>
#include "board.h"

const uint32_t OscRateIn = 0;

#define UART_BUFFER_SIZE 100

#ifndef TEST_HARDWARE
static RINGBUFF_T uart_rx_ring;
static uint8_t _uart_rx_ring[UART_BUFFER_SIZE];
static RINGBUFF_T uart_tx_ring;
static uint8_t _uart_tx_ring[UART_BUFFER_SIZE];

static LTC6804_CONFIG_T ltc6804_config;
static LTC6804_STATE_T ltc6804_state;
static Chip_SSP_DATA_SETUP_T ltc6804_xf_setup;
static uint8_t ltc6804_tx_buf[LTC6804_CALC_BUFFER_LEN(MAX_NUM_MODULES)]; 
static uint8_t ltc6804_rx_buf[LTC6804_CALC_BUFFER_LEN(MAX_NUM_MODULES)]; 
static uint8_t ltc6804_cfg[LTC6804_DATA_LEN]; 
static uint16_t ltc6804_bal_list[MAX_NUM_MODULES]; 
static LTC6804_ADC_RES_T ltc6804_adc_res;
static LTC6804_OWT_RES_T ltc6804_owt_res; 
// ltc6804 timing variables
static bool ltc6804_get_cell_voltages;

static bool ltc6804_initialized;

static uint8_t str[10];

#define LTC_CELL_VOLTAGE_FREQ 10
#endif
// ------------------------------------------------
// Private Functions

void canBaudrateCalculate(uint32_t baud_rate, uint32_t *can_api_timing_cfg) {
#ifdef TEST_HARDWARE
	(void)(baud_rate);
	(void)(can_api_timing_cfg);
#else
	uint32_t pClk, div, quanta, segs, seg1, seg2, clk_per_bit, can_sjw;
	Chip_Clock_EnablePeriphClock(SYSCTL_CLOCK_CAN);
	pClk = Chip_Clock_GetMainClockRate();

	clk_per_bit = pClk / baud_rate;

	for (div = 0; div <= 15; div++) {
		for (quanta = 1; quanta <= 32; quanta++) {
			for (segs = 3; segs <= 17; segs++) {
				if (clk_per_bit == (segs * quanta * (div + 1))) {
					segs -= 3;
					seg1 = segs / 2;
					seg2 = segs - seg1;
					can_sjw = seg1 > 3 ? 3 : seg1;
					can_api_timing_cfg[0] = div;
					can_api_timing_cfg[1] =
						((quanta - 1) & 0x3F) | (can_sjw & 0x03) << 6 | (seg1 & 0x0F) << 8 | (seg2 & 0x07) << 12;
					return;
				}
			}
		}
	}
#endif
}

#ifndef TEST_HARDWARE

/**
 * @brief	UART Interrupt Handler
 * @return	Nothing
 * @note	stuff more stuff into the buffer
 */

void UART_IRQHandler(void) {
	Chip_UART_IRQRBHandler(LPC_USART, &uart_rx_ring, &uart_tx_ring);
}

void TIMER32_0_IRQHandler(void) {
    if (Chip_TIMER_MatchPending(LPC_TIMER32_0, 0)) {
        Chip_TIMER_ClearMatch(LPC_TIMER32_0, 0);
        // Do something

        ltc6804_get_cell_voltages = true;
    }
}

#endif

/**
 * @brief SysTick Interrupt Handler
 */
// void SysTick_Handler(void) {

// }

// ------------------------------------------------
// Public Functions



uint32_t Board_Print(const char *str) {
#ifdef TEST_HARDWARE
	return printf("%s", str);
#else
	return Chip_UART_SendRB(LPC_USART, &uart_tx_ring, str, strlen(str));
#endif
}

uint32_t Board_Println(const char *str) {
#ifdef TEST_HARDWARE
	return printf("%s\r\n", str);
#else
	uint32_t count = Board_Print(str);
	return count + Board_Print("\r\n");
#endif
}

uint32_t Board_Write(const char *str, uint32_t count) {
#ifdef TEST_HARDWARE
	return printf("%.*s", count, str);
#else
	return Chip_UART_SendRB(LPC_USART, &uart_tx_ring, str, count);
#endif
}

uint32_t Board_Read(char *charBuffer, uint32_t length) {
#ifdef TEST_HARDWARE
	fgets(charBuffer, length, stdin);
	return strlen(charBuffer);
#else
	return Chip_UART_ReadRB(LPC_USART, &uart_rx_ring, charBuffer, length);
#endif
}

// USE THESE SPARINGLY. ONLY WHEN A PRINT WOULD RESULT IN A BUFFER OVERFLOW
uint32_t Board_Print_BLOCKING(const char *str) {
#ifdef TEST_HARDWARE
	return printf("%s", str);
#else
	return Chip_UART_SendBlocking(LPC_USART, str, strlen(str));
#endif
}

uint32_t Board_Println_BLOCKING(const char *str) {
	uint32_t count = Board_Print_BLOCKING(str);
	return count + Board_Print_BLOCKING("\r\n");
}



void Board_UART_Init(uint32_t baudRateHz) {
#ifdef TEST_HARDWARE
	(void)(baudRateHz);
#else
	// Initialize UART Buffers
	RingBuffer_Init(&uart_rx_ring, _uart_rx_ring, sizeof(uint8_t), UART_BUFFER_SIZE);
	RingBuffer_Flush(&uart_rx_ring);
	RingBuffer_Init(&uart_tx_ring, _uart_tx_ring, sizeof(uint8_t), UART_BUFFER_SIZE);
	RingBuffer_Flush(&uart_tx_ring);

	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_6, (IOCON_FUNC1 | IOCON_MODE_INACT));/* RXD */
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO1_7, (IOCON_FUNC1 | IOCON_MODE_INACT));/* TXD */
 
	Chip_UART_Init(LPC_USART);
	Chip_UART_SetBaudFDR(LPC_USART, baudRateHz);
	Chip_UART_ConfigData(LPC_USART, (UART_LCR_WLEN8 | UART_LCR_SBS_1BIT | UART_LCR_PARITY_DIS));
	Chip_UART_SetupFIFOS(LPC_USART, (UART_FCR_FIFO_EN | UART_FCR_TRG_LEV2));
	Chip_UART_TXEnable(LPC_USART);

	Chip_UART_IntEnable(LPC_USART, UART_IER_RBRINT);
	NVIC_ClearPendingIRQ(UART0_IRQn);
	NVIC_EnableIRQ(UART0_IRQn);
#endif
}

void Board_SPI_Init(uint32_t baudRateHz) {
#ifdef TEST_HARDWARE
	(void)(baudRateHz);
#else
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_8, (IOCON_FUNC1 | IOCON_MODE_INACT));	/* MISO0 */
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_9, (IOCON_FUNC1 | IOCON_MODE_INACT));	/* MOSI0 */
	Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO2_11, (IOCON_FUNC1 | IOCON_MODE_INACT));	/* SCK0 */
	Chip_IOCON_PinLocSel(LPC_IOCON, IOCON_SCKLOC_PIO2_11);

	Chip_SSP_Init(LPC_SSP0);
	Chip_SSP_SetBitRate(LPC_SSP0, baudRateHz);

	Chip_SSP_SetFormat(LPC_SSP0, SSP_BITS_8, SSP_FRAMEFORMAT_SPI, SSP_CLOCK_MODE0);
	Chip_SSP_SetMaster(LPC_SSP0, true);
	Chip_SSP_Enable(LPC_SSP0);
#endif
}

void Board_CCAN_Init(uint32_t baudRateHz, 
					void (*CAN_rx)(uint8_t), 
					void (*CAN_tx)(uint8_t), 
					void (*CAN_error)(uint32_t)) {
#ifdef TEST_HARDWARE
	(void)(baudRateHz);
	(void)(CAN_rx);
	(void)(CAN_tx);
	(void)(CAN_error);
#else
	uint32_t CanApiClkInitTable[2];
	CCAN_CALLBACKS_T callbacks = {
		CAN_rx,
		CAN_tx,
		CAN_error,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
	};
	canBaudrateCalculate(baudRateHz, CanApiClkInitTable);

	LPC_CCAN_API->init_can(CanApiClkInitTable, TRUE);
	LPC_CCAN_API->config_calb(&callbacks);
	NVIC_EnableIRQ(CAN_IRQn);
#endif
}

// void Board_LED_Init(void) {
// #ifndef TEST_HARDWARE
// 	Chip_GPIO_Init(LPC_GPIO);
// 	Chip_GPIO_SetPinDIROutput(LPC_GPIO, LED0_GPIO, LED0_PIN);
// #endif
// }

// void Board_LED_On(void) {
// #ifndef TEST_HARDWARE
// 	Chip_GPIO_SetPinOutHigh(LPC_GPIO, LED0_GPIO, LED0_PIN);
// #endif
// }

// void Board_LED_Off(void) {
// #ifndef TEST_HARDWARE
// 	Chip_GPIO_SetPinOutLow(LPC_GPIO, LED0_GPIO, LED0_PIN);
// #endif
// }

// void Board_LED_Toggle(void) {
// #ifndef TEST_HARDWARE
// 	Chip_GPIO_SetPinState(LPC_GPIO, LED0, 1 - Chip_GPIO_GetPinState(LPC_GPIO, LED0));
// #endif
// }


void Board_Headroom_Init(void){
#ifndef TEST_HARDWARE
	Chip_GPIO_SetPinDIROutput(LPC_GPIO, HEADROOM);
#endif
}

void Board_Headroom_Toggle(void){
#ifndef TEST_HARDWARE
	Chip_GPIO_SetPinState(LPC_GPIO, HEADROOM, 1 - Chip_GPIO_GetPinState(LPC_GPIO, HEADROOM));
#endif
}

void Board_Switch_Init(void) {
#ifndef TEST_HARDWARE
	Chip_GPIO_Init(LPC_GPIO);
	Chip_GPIO_SetPinDIRInput(LPC_GPIO, SWITCH_GPIO, SWITCH_PIN);
#endif
}

bool Board_Switch_Read(void) {
#ifdef TEST_HARDWARE
	return 0;
#else
	return Chip_GPIO_GetPinState(LPC_GPIO, SWITCH_GPIO, SWITCH_PIN);
#endif
}

void Board_Close_Contactors(bool close_contactors) {
	//TODO: implement function
	UNUSED(close_contactors);
}

bool Board_Are_Contactors_Closed(void) {
	//TODO: implement function
	return false;
}

#ifndef TEST_HARDWARE
void Board_Get_Mode_Request(const CONSOLE_OUTPUT_T * console_output, BMS_INPUT_T* bms_input) {
	//TODO: implement function
	// if (Chip_GPIO_GetPinState(LPC_GPIO, BAL_SW)) {
    //     bms_input->mode_request = BMS_SSM_MODE_BALANCE;
    //     bms_input->balance_mV = 3300;
    // } else if (Chip_GPIO_GetPinState(LPC_GPIO, CHRG_SW)) {
    //     bms_input->mode_request = BMS_SSM_MODE_CHARGE;
    // } else if (Chip_GPIO_GetPinState(LPC_GPIO, DISCHRG_SW)) {
    //     bms_input->mode_request = BMS_SSM_MODE_DISCHARGE;
    // } else {
    //     bms_input->mode_request = BMS_SSM_MODE_STANDBY;
    // }
	if (console_output -> valid_mode_request) {
        bms_input->mode_request = console_output->mode_request;
        bms_input->balance_mV = console_output->balance_mV;
    } else {
        bms_input->mode_request = BMS_SSM_MODE_STANDBY; // [TODO] Change this
    }
}
#endif



bool Board_LTC6804_Validate_Configuration(uint32_t msTicks) {
#ifdef TEST_HARDWARE
	return false;
#else
	Board_Print("Initializing LTC6804. Verifying..");
    if (!LTC6804_VerifyCFG(&ltc6804_config, &ltc6804_state, msTicks)) {
        Board_Print(".FAIL. ");
        return false;
    } else {
        Board_Print(".PASS. ");
        return true;
    }
#endif
}

void Board_Init_Chip(void) {

}

void Board_Init_EEPROM(void) {

}

void Board_GPIO_Init(void) {
#ifndef TEST_HARDWARE
    // [TODO] verify that pins don't collide
    //  move pin selections to preprocesser defines
	Chip_GPIO_Init(LPC_GPIO);
	Chip_GPIO_WriteDirBit(LPC_GPIO, LED0, true);
    Chip_GPIO_WriteDirBit(LPC_GPIO, LED1, true);
    Board_Headroom_Init();

    Chip_GPIO_WriteDirBit(LPC_GPIO, BAL_SW, false);
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_BAL_SW, IOCON_MODE_PULLUP);
    Chip_GPIO_WriteDirBit(LPC_GPIO, CHRG_SW, false);
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_CHRG_SW, IOCON_MODE_PULLUP);
    Chip_GPIO_WriteDirBit(LPC_GPIO, DISCHRG_SW, false);
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_DISCHRG_SW, IOCON_MODE_PULLUP);
    
    //SSP for EEPROM
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO2_2, (IOCON_FUNC2 | IOCON_MODE_INACT));    /* MISO1 */ 
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO2_3, (IOCON_FUNC2 | IOCON_MODE_INACT));    /* MOSI1 */
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO2_1, (IOCON_FUNC2 | IOCON_MODE_INACT));    /* SCK1 */

    //SSP for LTC6804
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_8, (IOCON_FUNC1 | IOCON_MODE_INACT));    /* MISO0 */ 
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_9, (IOCON_FUNC1 | IOCON_MODE_INACT));    /* MOSI0 */
    Chip_IOCON_PinMuxSet(LPC_IOCON, IOCON_PIO0_6, (IOCON_FUNC2 | IOCON_MODE_INACT));    /* SCK0 */
    Chip_IOCON_PinLocSel(LPC_IOCON, IOCON_SCKLOC_PIO0_6);
#endif

}



void Board_Init_Timers(void) {
#ifndef TEST_HARDWARE
    // Timer 32_0 initialization
    Chip_TIMER_Init(LPC_TIMER32_0);
    Chip_TIMER_Reset(LPC_TIMER32_0);
    Chip_TIMER_MatchEnableInt(LPC_TIMER32_0, 0);
    Chip_TIMER_SetMatch(LPC_TIMER32_0, 0, Hertz2Ticks(LTC_CELL_VOLTAGE_FREQ));
    Chip_TIMER_ResetOnMatchEnable(LPC_TIMER32_0, 0);
#endif
}

void Board_Enable_Timers(void) { //[TODO] removeme
#ifndef TEST_HARDWARE
    NVIC_ClearPendingIRQ(TIMER_32_0_IRQn);
    NVIC_EnableIRQ(TIMER_32_0_IRQn);
    Chip_TIMER_Enable(LPC_TIMER32_0);
#endif
}

void Board_LTC6804_Init(PACK_CONFIG_T * pack_config, uint32_t * cell_voltages_mV, uint32_t msTicks) {
#ifdef TEST_HARDWARE
	return;
#else
	if (!ltc6804_initialized) {
	    ltc6804_config.pSSP = LPC_SSP0;
	    ltc6804_config.baud = LTC6804_BAUD;
	    ltc6804_config.cs_gpio = 0;
	    ltc6804_config.cs_pin = 2;

	    ltc6804_config.num_modules = pack_config->num_modules;
	    ltc6804_config.module_cell_count = pack_config->module_cell_count;

	    ltc6804_config.min_cell_mV = pack_config->cell_min_mV;
	    ltc6804_config.max_cell_mV = pack_config->cell_max_mV;

	    ltc6804_config.adc_mode = LTC6804_ADC_MODE_NORMAL;
	    
	    ltc6804_state.xf = &ltc6804_xf_setup;
	    ltc6804_state.tx_buf = ltc6804_tx_buf;
	    ltc6804_state.rx_buf = ltc6804_rx_buf;
	    ltc6804_state.cfg = ltc6804_cfg;
	    ltc6804_state.bal_list = ltc6804_bal_list;

	    ltc6804_adc_res.cell_voltages_mV = cell_voltages_mV;

	    ltc6804_owt_res.failed_wire = 0;
	    ltc6804_owt_res.failed_module = 0;

	    LTC6804_Init(&ltc6804_config, &ltc6804_state, msTicks);
	    ltc6804_get_cell_voltages = false; // [TODO] Same as above

	    ltc6804_initialized = true;
	}
#endif
}

void Board_LTC6804_DeInit(void) {
#ifndef TEST_HARDWARE
	ltc6804_initialized = false;
#endif
}

void Board_Init_Drivers(void) {

}

//[TODO] check saftey
void Board_LTC6804_Get_Cell_Voltages(BMS_PACK_STATUS_T* pack_status, uint32_t msTicks) {
#ifdef TEST_HARDWARE
	return;
#else
	LTC6804_STATUS_T res = LTC6804_GetCellVoltages(&ltc6804_config, &ltc6804_state, &ltc6804_adc_res, msTicks);
	switch (res) {
    	case LTC6804_FAIL:
    		Board_Println("Get Vol FAIL");
    	case LTC6804_SPI_ERROR:
	    	Board_Println("Get Vol SPI_ERROR");
    	case LTC6804_PEC_ERROR:
    		Board_Println("Get Vol PEC_ERROR");
    		Error_Assert(ERROR_LTC6804_PEC,msTicks);
    	case LTC6804_PASS:
    		pack_status->pack_cell_min_mV = ltc6804_adc_res.pack_cell_min_mV;
        	pack_status->pack_cell_max_mV = ltc6804_adc_res.pack_cell_max_mV;
        	LTC6804_ClearCellVoltages(&ltc6804_config, &ltc6804_state, msTicks);
        	ltc6804_get_cell_voltages = false;
        	Error_Pass(ERROR_LTC6804_PEC);
    	case LTC6804_WAITING:
    	case LTC6804_WAITING_REFUP:
    		break;
    	default:
    		Board_Println("WTF");
    }
#endif
}

//[TODO] check saftey 
bool Board_LTC6804_CVST(uint32_t msTicks) {
#ifdef TEST_HARDWARE
	return false;
#else
	LTC6804_STATUS_T res;
    res = LTC6804_CVST(&ltc6804_config, &ltc6804_state, msTicks);

    switch (res) {
    	case LTC6804_FAIL:
    		Board_Println("CVST FAIL");
    		return false;
    	case LTC6804_SPI_ERROR:
	    	Board_Println("CVST SPI_ERROR");
	        return false;
    	case LTC6804_PEC_ERROR:
    		Board_Println("CVST PEC_ERROR");
    		Error_Assert(ERROR_LTC6804_PEC,msTicks);
        	return false;
    	case LTC6804_PASS:
    		Board_Println("CVST PASS");
    		Board_Enable_Timers();
    		Error_Pass(ERROR_LTC6804_PEC);
    		return true;
    	case LTC6804_WAITING:
    	case LTC6804_WAITING_REFUP:
    		return false;
    	default:
    		Board_Println("WTF");
    		return false;
    }

    return false;
#endif
}

//[TODO] add saftey
void Board_LTC6804_Update_Balance_States(bool *balance_req, uint32_t msTicks) {
#ifdef TEST_HARDWARE
	return;
#else
	LTC6804_STATUS_T res;
    res = LTC6804_UpdateBalanceStates(&ltc6804_config, &ltc6804_state, balance_req, msTicks);
    if (res == LTC6804_SPI_ERROR) {
        Board_Println("SetBalanceStates SPI_ERROR");
    }
#endif
}

// [TODO] Make work pls
bool Board_LTC6804_OpenWireTest(uint32_t msTicks) {
#ifdef TEST_HARDWARE
#else
	LTC6804_STATUS_T res;
    res = LTC6804_OpenWireTest(&ltc6804_config, &ltc6804_state, &ltc6804_owt_res, msTicks);

    switch (res) {
    	case LTC6804_FAIL:
    		Board_Print("OWT FAIL, mod=");
    		utoa(ltc6804_owt_res.failed_module, str, 10);
    		Board_Print(str);
    		Board_Print(" wire=");
    		utoa(ltc6804_owt_res.failed_wire, str, 10);
    		Board_Println(str);

    		return false;
    	case LTC6804_SPI_ERROR:
	    	Board_Println("OWT SPI_ERROR");
	        return false;
    	case LTC6804_PEC_ERROR:
    		Board_Println("OWT PEC_ERROR");
    		Error_Assert(ERROR_LTC6804_PEC,msTicks);
        	return false;
    	case LTC6804_PASS:
    		Board_Println("OWT PASS");
    		Board_Enable_Timers();
    		Error_Pass(ERROR_LTC6804_PEC);
    		return true;
    	case LTC6804_WAITING:
    	case LTC6804_WAITING_REFUP:
    		// Board_Println("*");
    		return false;
    	default:
    		Board_Println("WTF");
    		return false;
    }
#endif
}

