#ifndef __RC5_DECODE_H
#define __RC5_DECODE_H

#include "stm32f0xx.h"

#include <stdbool.h>
#include <stdint.h>

/*
FIXME rm
typedef struct
{
   __IO uint8_t FieldBit;   //!< Field bit
   __IO uint8_t ToggleBit;  //!< Toggle bit field
   __IO uint8_t Address;    //!< Address field
   __IO uint8_t Command;    //!< Command field
} RC5_Frame_TypeDef;
*/

union IRMessage {
   uint32_t raw;
   //TODO struct with fields
};

void RC5_Decode_Init(void);
bool RC5_Decode(union IRMessage * const frame);

//used internally to decode incoming IR data
void RC5_ResetPacket(void);
void RC5_DataSampling(uint16_t rawPulseLength, uint8_t edge);

void RC5_DecodeDisable(void);
void RC5_DecodeEnable(void);

#endif /* __RC5_DECODE_H */

