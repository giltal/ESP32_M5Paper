#ifndef __SYSTEMINIT_H
#define __SYSTEMINIT_H

#include "m5paper_port.h"

void SysInit_Start(void);
void SysInit_Loading(void *pvParameters);
void SysInit_UpdateInfo(std::string info);

#endif  //__SYSTEMINIT_H