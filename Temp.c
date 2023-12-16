#include "stm32f7xx_hal.h"
#include "stm32f7xx_hal_adc.h"
#include "Temp.h"

extern ADC_HandleTypeDef hadc3;

uint16_t ReadPot(int32_t *potValue){
	HAL_ADC_Start(&hadc3);
	HAL_ADC_PollForConversion(&hadc3, 1000);

  *potValue = HAL_ADC_GetValue(&hadc3);
	
	return 0;
}
