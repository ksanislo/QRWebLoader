#include <3ds.h>

#include "camera.h"
#include "common.h"

void configCamera(){
	CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetWhiteBalance(SELECT_OUT1, WHITE_BALANCE_AUTO);
	CAMU_SetContrast(SELECT_OUT1, CONTRAST_NORMAL);
	CAMU_SetAutoExposure(SELECT_OUT1, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
//	CAMU_SetAutoExposureWindow(SELECT_OUT1, 100, 20, 200, 200);
//	CAMU_SetAutoWhiteBalanceWindow(SELECT_OUT1, 100, 20, 400, 200);
	CAMU_SetAutoExposureWindow(SELECT_OUT1, 0, 0, 400, 240);
	CAMU_SetAutoWhiteBalanceWindow(SELECT_OUT1, 0, 0, 400, 240);
	CAMU_SetTrimming(PORT_CAM1, false);
        CAMU_Activate(SELECT_OUT1);
}

void takePicture(u16 *buf){
	u32 bufSize;
	Handle camReceiveEvent = 0;

	CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT);
	CAMU_SetTransferBytes(PORT_CAM1, bufSize, WIDTH, HEIGHT);
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	CAMU_SetReceiving(&camReceiveEvent, (u8*)buf, PORT_CAM1, WIDTH * HEIGHT * 2, (s16) bufSize);
	svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT);
	CAMU_StopCapture(PORT_CAM1);
	svcCloseHandle(camReceiveEvent);
}

