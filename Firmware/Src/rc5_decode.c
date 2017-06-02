/**
 * TODO Theory of operation
 */

#include "rc5_decode.h"
#include "iprintf.h"

#include "stm32f0xx_hal.h"
#include "stm32f0xx_hal_tim.h"

#include <stdint.h>
#include <stdbool.h>

#define RC5_1T_TIME                          0x00
#define RC5_2T_TIME                          0x01
#define RC5_WRONG_TIME                       0xFF
#define RC5_TIME_OUT_US                      3600
#define RC5_T_US                             900     /*!< Half bit period */
#define RC5_T_TOLERANCE_US                   270    /*!< Tolerance time */
#define RC5_NUMBER_OF_VALID_PULSE_LENGTH     2
//FIXME rm
//13 bits (not 14) to allow 1 to be lost to syncing
//32 bits + 1 to be lost to syncing
#define RC5_PACKET_BIT_COUNT                 32      /*!< Total bits */

/* Packet struct for reception*/
#define RC5_PACKET_STATUS_EMPTY              1<<0

#define TIM_PRESCALER          47                       /* !< TIM prescaler */

//FIXME rename
typedef struct
{
   __IO uint32_t data;     /*!< RC5 data */
   __IO uint8_t  status;   /*!< RC5 status */
   __IO uint8_t  lastBit;  /*!< RC5 last bit */
   __IO uint8_t  bitCount; /*!< RC5 bit count */
} tRC5_packet;

enum RC5_lastBitType
{
   RC5_ZER,
   RC5_ONE,
   RC5_NAN,
   RC5_INV
};
typedef enum RC5_lastBitType tRC5_lastBitType;

/* Logic table for rising edge: every line has values corresponding to previous bit.
   In columns are actual bit values for given bit time. */
const tRC5_lastBitType RC5_logicTableRisingEdge[2][2] =
{
   {RC5_ZER ,RC5_INV}, /* lastbit = ZERO */
   {RC5_NAN ,RC5_ZER}, /* lastbit = ONE  */
};

/* Logic table for falling edge: every line has values corresponding to previous bit. 
   In columns are actual bit values for given bit time. */
const tRC5_lastBitType RC5_logicTableFallingEdge[2][2] =
{
   {RC5_NAN ,RC5_ONE},  /* lastbit = ZERO */
   {RC5_ONE ,RC5_INV},  /* lastbit = ONE  */
};

//FIXME encapsulate this
//not static so IT can see it
TIM_HandleTypeDef htim3;

__IO bool RC5FrameReceived = false; /*!< RC5 Frame state */ 
__IO tRC5_packet   RC5TmpPacket;          /*!< First empty packet */

//FIXME rename
/* RC5  bits time definitions */
static uint16_t  RC5MinT = 0;
static uint16_t  RC5MaxT = 0;
static uint16_t  RC5Min2T = 0;
static uint16_t  RC5Max2T = 0;
static uint32_t TIMCLKValueKHz = 0; /*!< Timer clock */
static uint16_t RC5TimeOut = 0;
static uint32_t RC5_Data = 0;
//RC5_Frame_TypeDef RC5_FRAME;

static uint8_t RC5_GetPulseLength (uint16_t pulseLength);
static void RC5_modifyLastBit(tRC5_lastBitType bit);
static void RC5_WriteBit(uint8_t bitVal);
static uint32_t TIM_GetCounterCLKValue(void);

/**
 * @brief  Initialize the RC5 decoder module ( Time range)
 * @param  None
 * @retval None
 */
void RC5_Decode_Init(void)
{ 
   TIM_ClockConfigTypeDef sClockSourceConfig;
   TIM_SlaveConfigTypeDef sSlaveConfig;
   TIM_MasterConfigTypeDef sMasterConfig;
   TIM_IC_InitTypeDef sConfigIC;

   //calculate timeouts
   TIMCLKValueKHz = TIM_GetCounterCLKValue()/1000;
   RC5TimeOut = TIMCLKValueKHz * (RC5_TIME_OUT_US / 1000);
   iprintf("Value KHz = %d\r\n", TIMCLKValueKHz);
   iprintf("RC5 timeout = %d\r\n", RC5TimeOut);

   htim3.Instance = TIM3;
   htim3.Init.Prescaler = 47;
   htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
   htim3.Init.Period = RC5TimeOut;
   htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
   htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
   if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
   if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   if (HAL_TIM_IC_Init(&htim3) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
   sSlaveConfig.InputTrigger = TIM_TS_TI1FP1;
   sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
   sSlaveConfig.TriggerFilter = 0;
   if (HAL_TIM_SlaveConfigSynchronization(&htim3, &sSlaveConfig) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
   sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_ENABLE;
   if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
   sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
   sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
   sConfigIC.ICFilter = 0;
   if (HAL_TIM_IC_ConfigChannel(&htim3, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
   {
      iprintf("ERROR\r\n");
   }

   /* Enable TIM Update Event Interrupt Request */
   __HAL_TIM_CLEAR_FLAG(&htim3, TIM_FLAG_UPDATE);
   __HAL_TIM_ENABLE_IT(&htim3, TIM_FLAG_UPDATE);

   /* Bit time range */
   RC5MinT = (RC5_T_US - RC5_T_TOLERANCE_US) * TIMCLKValueKHz / 1000;
   RC5MaxT = (RC5_T_US + RC5_T_TOLERANCE_US) * TIMCLKValueKHz / 1000;
   RC5Min2T = (2 * RC5_T_US - RC5_T_TOLERANCE_US) * TIMCLKValueKHz / 1000;
   RC5Max2T = (2 * RC5_T_US + RC5_T_TOLERANCE_US) * TIMCLKValueKHz / 1000;

   iprintf("MinT = %d, MaxT = %d\r\n", RC5MinT, RC5MaxT);
   iprintf("Min2T = %d, Max2T = %d\r\n", RC5Min2T, RC5Max2T);

   /* Default state */
   RC5_ResetPacket();

   HAL_TIM_IC_Start_IT(&htim3, TIM_CHANNEL_1);
}

/**
 * Temporarily disable the RX pipeline (for when we are transmitting).
 */
void RC5_DecodeDisable(void) {
   HAL_NVIC_DisableIRQ(TIM3_IRQn);
   RC5_ResetPacket();
}

void RC5_DecodeEnable(void) {
   RC5_ResetPacket();
   HAL_NVIC_EnableIRQ(TIM3_IRQn);
}

bool RC5_Decode(union IRMessage * const frame)
{
   /* If frame received */
   if(RC5FrameReceived && frame)
   {
      /*
      RC5_Data = RC5TmpPacket.data;

      // RC5 frame field decoding
      rc5_frame->Address = (RC5TmpPacket.data >> 6) & 0x1F;
      rc5_frame->Command = (RC5TmpPacket.data) & 0x3F; 
      rc5_frame->FieldBit = (RC5TmpPacket.data >> 12) & 0x1;
      rc5_frame->ToggleBit = (RC5TmpPacket.data >> 11) & 0x1;

      // Check if command ranges between 64 to 127:Upper Field
      if (rc5_frame->FieldBit == 0x00)
      {
         rc5_frame->Command =  (1<<6)| rc5_frame->Command; 
      }

      // Display RC5 message
      iprintf("Command:%s\r\n", rc5_Commands[rc5_frame->Command]);
      iprintf("Device:%s\r\n", rc5_devices[rc5_frame->Address]);
      */

      frame->raw = RC5TmpPacket.data;

      /*
      //FIXME rm
      frame->raw = 0;
      int j;
      for(j = 0; j < 32; j++) {
         frame->raw |= (RC5TmpPacket.data & (32 - j)) << j;
      }
      */

      iprintf("Packet RX: 0x%x\n", frame->raw);

      // Default state
      RC5FrameReceived = false;

      RC5_ResetPacket();

      return true;
   }
   //FIXME rm
   else {
      /*
      //iprintf("(%d)Pkt:0x%x\n", RC5TmpPacket.bitCount, RC5TmpPacket.data);
      iprintf("%d|", RC5TmpPacket.bitCount);
      if(RC5TmpPacket.bitCount == 0) {
         iprintf("Pkt:0x%x\n", RC5TmpPacket.data);
      }
      */
   }
   return false;
}

/**
 * @brief  Set the incoming packet structure to default state.
 * @param  None
 * @retval None
 */
void RC5_ResetPacket(void)
{
   RC5TmpPacket.data = 0;
   RC5TmpPacket.bitCount = RC5_PACKET_BIT_COUNT - 1;
   RC5TmpPacket.lastBit = RC5_ONE;
   RC5TmpPacket.status = RC5_PACKET_STATUS_EMPTY;
}

/**
 * @brief  Identify the RC5 data bits.
 * @param  rawPulseLength: low/high pulse duration
 * @param  edge: '1' for Rising  or '0' for falling edge
 * @retval None
 */
void RC5_DataSampling(uint16_t rawPulseLength, uint8_t edge)
{
   uint8_t pulse;
   tRC5_lastBitType tmpLastBit;

   if(RC5FrameReceived) {
      return;
   }

   //comment out for useful printing
#define iprintf(...)

   /* Decode the pulse length in protocol units */
   pulse = RC5_GetPulseLength(rawPulseLength);

   iprintf("|%d:", rawPulseLength);

   /* On Rising Edge */
   if (edge == 1)
   {
      iprintf("r");

      if (pulse <= RC5_2T_TIME) 
      {
         /* Bit determination by the rising edge */
         tmpLastBit = RC5_logicTableRisingEdge[RC5TmpPacket.lastBit][pulse];
         RC5_modifyLastBit (tmpLastBit);
      }
      else
      {
         iprintf("R");

         RC5_ResetPacket();
      }
   } 
   else     /* On Falling Edge */
   {
      iprintf("f");

      /* If this is the first falling edge - don't compute anything */
      if (RC5TmpPacket.status & RC5_PACKET_STATUS_EMPTY)
      {
         iprintf("\r\nF");

         //NOTE: This throws away the first bit! That's good! It's used to sync
         //the timers.
         RC5TmpPacket.status &= (uint8_t)~RC5_PACKET_STATUS_EMPTY;
      }
      else	
      {
         if (pulse <= RC5_2T_TIME) 
         { 
            /* Bit determination by the falling edge */
            tmpLastBit = RC5_logicTableFallingEdge[RC5TmpPacket.lastBit][pulse];
            RC5_modifyLastBit(tmpLastBit);
         }
         else
         {
            iprintf("R");

            RC5_ResetPacket();
         }
      }
   }

#undef iprintf
}

/**
 * @brief  Convert raw pulse length expressed in timer ticks to protocol bit times.
 * @param  pulseLength:pulse duration
 * @retval bit time value
 */
static uint8_t RC5_GetPulseLength (uint16_t pulseLength)
{
   /* Valid bit time */
   if ((pulseLength > RC5MinT) && (pulseLength < RC5MaxT))
   {
      /* We've found the length */
      return (RC5_1T_TIME);	/* Return the correct value */
   }
   else if ((pulseLength > RC5Min2T) && (pulseLength < RC5Max2T))
   {
      /* We've found the length */
      return (RC5_2T_TIME);/* Return the correct value */
   }
   return RC5_WRONG_TIME;/* Error */
}

/**
 * @brief  perform checks if the last bit was not incorrect. 
 * @param  bit: where bit can be  RC5_NAN or RC5_INV or RC5_ZER or RC5_ONE
 * @retval None
 */
static void RC5_modifyLastBit(tRC5_lastBitType bit)
{
   if (bit != RC5_NAN)
   {
      if (RC5TmpPacket.lastBit != RC5_INV)
      { 
         /* Restore the last bit */
         RC5TmpPacket.lastBit = bit;

         /* Insert one bit into the RC5 Packet */
         RC5_WriteBit(RC5TmpPacket.lastBit);
      }
      else 
      {
         RC5_ResetPacket();
      }
   }
}

/**
 * @brief  Insert one bit into the final data word. 
 * @param  bitVal: bit value 'RC5_ONE' or 'RC5_ZER'
 * @retval None
 */
static void RC5_WriteBit(uint8_t bitVal)
{
   /* First convert RC5 symbols to ones and zeros */
   if (bitVal == RC5_ONE)
   {
      //iprintf("1");
      bitVal = 1;
   }
   else if (bitVal == RC5_ZER)
   {
      //iprintf("0");
      bitVal = 0;
   }
   else
   {
      //iprintf("R %d]", RC5TmpPacket.bitCount);
      RC5_ResetPacket();
      return;
   }

   /* Write this particular bit to data field */
   RC5TmpPacket.data |= bitVal;

   /* Test the bit number determined */
   if (RC5TmpPacket.bitCount != 0)  /* If this is not the last bit */
   {
      /* Shift the data field */
      RC5TmpPacket.data = RC5TmpPacket.data << 1;
      /* And decrement the bitCount */
      RC5TmpPacket.bitCount--;
   } 
   else
   {
      RC5FrameReceived = true;

      /*
      //FIXME rm
      iprintf("\r\nGOT ONE: 0x%d\r\n", RC5TmpPacket.data);
      RC5_ResetPacket();
      RC5FrameReceived = false;
      */
   }
}

/**
 * @brief  Identify TIM clock
 * @param  None
 * @retval Timer clock
 */
static uint32_t TIM_GetCounterCLKValue(void)
{
   uint32_t apbprescaler = 0, apbfrequency = 0;
   uint32_t timprescaler = 0;
   uint32_t pfLatency;
   RCC_ClkInitTypeDef  RCC_ClkInitStruct;

   /* This function fills the RCC_ClockFreq structure with the current
      frequencies of different on chip clocks */
   HAL_RCC_GetClockConfig(&RCC_ClkInitStruct, &pfLatency);


   /* Get the clock prescaler of APB1 */
   apbprescaler = ((RCC->CFGR >> 8) & 0x7);
   apbfrequency = HAL_RCC_GetPCLK1Freq();
   timprescaler = TIM_PRESCALER;

   /* If APBx clock div >= 4 */
   if (apbprescaler >= 4)
   {
      return ((apbfrequency * 2)/(timprescaler + 1));
   }
   else
   {
      return (apbfrequency/(timprescaler+ 1));
   }
}

