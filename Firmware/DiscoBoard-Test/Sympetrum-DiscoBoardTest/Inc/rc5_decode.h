#ifndef __RC5_DECODE_H
#define __RC5_DECODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f0xx.h"

#include <stdbool.h>
   typedef struct
   {
      __IO uint8_t FieldBit;   /*!< Field bit */
      __IO uint8_t ToggleBit;  /*!< Toggle bit field */
      __IO uint8_t Address;    /*!< Address field */
      __IO uint8_t Command;    /*!< Command field */
   } RC5_Frame_TypeDef;

   void RC5_Decode_Init(void);
   bool RC5_Decode(RC5_Frame_TypeDef *rc5_frame);

   //used internally to decode incoming IR data
   void RC5_ResetPacket(void);
   void RC5_DataSampling(uint16_t rawPulseLength, uint8_t edge);


#ifdef __cplusplus
}
#endif

#endif /* __RC5_DECODE_H */

