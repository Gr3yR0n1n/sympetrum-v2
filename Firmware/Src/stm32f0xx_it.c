/*
 * The root of all ISRs for the entire program live in this file.
 */
#include "stm32f0xx_hal.h"
#include "stm32f0xx.h"
#include "stm32f0xx_it.h"
#include "stm32f0xx_hal_tim.h"
#include "stm32f0xx_hal_tim_ex.h"

#include "rc5_encode.h"
#include "rc5_decode.h"

#include "iprintf.h"


//TODO find a better way to pass these in
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim17;

//TODO move these out of this file (into RC5?)?
static uint32_t ICValue2 = 0;
static uint8_t pol;

/**
 * @brief This function handles System tick timer.
 */
void SysTick_Handler(void)
{
   HAL_IncTick();
   HAL_SYSTICK_IRQHandler();
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
   iprintf("EXTI0 ");
   //Check if EXTI_Line0 is asserted
   /*
   if(EXTI_GetITStatus(EXTI_Line0) != RESET)
   {
      iprintf("Button");
   }
   //we need to clear line pending bit manually
   */
   //EXTI_ClearITPendingBit(EXTI_Line0);
}

/*
 * Handle the bit clock ISR for sending IR.
 */
void TIM16_IRQHandler(void)
{
   //figure out the next stage of the outgoing signal
   RC5_Encode_SignalGenerate();

   /* Clear TIM16 update interrupt */
   __HAL_TIM_CLEAR_FLAG(&htim16, TIM_FLAG_UPDATE);
}

/*
 * Handle the ISR used when decoding incoming IR.
 */
void TIM3_IRQHandler(void)
{
   /* Clear the TIM2 Update pending bit (but doesn't clear the flag)*/
   __HAL_TIM_CLEAR_IT(&htim3, TIM_FLAG_UPDATE);

   // track the time between ALL edges of the incoming signal.
   if(__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_CC1))
   {
      __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_CC1);

      ICValue2 = HAL_TIM_ReadCapturedValue(&htim3, TIM_CHANNEL_1);

      //get current polarity and assume we just saw the opposite edge
      pol = (GPIO_PIN_SET == HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_6));

      RC5_DataSampling(ICValue2, pol);
   }
   //check for IR bit timeout
   else if(__HAL_TIM_GET_FLAG(&htim3, TIM_FLAG_UPDATE))
   {
      /* Clears the IR_TIM's pending flags*/
      __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);

      RC5_ResetPacket(); 
   }
}

