#include <citrus/core.hpp>
#include <citrus/app.hpp>
#include <citrus/gpu.hpp>
#include <citrus/hid.hpp>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include <quirc.h>
}

#include "http.h"
#include "mega.h"
#include "graphics.h"
#include "camera.h"
#include "common.h"

extern "C" {
u32 __stacksize__ = 0x40000;
}

using namespace ctr;

bool onProgress(u64 pos, u64 size){
	printf("pos: %" PRId64 "-%" PRId64 "\n", pos, size);
	hid::poll();
	return !hid::pressed(hid::BUTTON_B);
}

int main(int argc, char **argv){
	core::init(argc);
	httpcInit(0x1000);
	camInit();

	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	consoleInit(GFX_BOTTOM,NULL);
	gfxSet3D(false);

	printf("Initializing camera...");
	gpu::swapBuffers(true);
	configCamera();
	printf("done.\n");
	gpu::swapBuffers(true);

	u16 *camBuf = (u16*)malloc(WIDTH * HEIGHT * 2);
	if(!camBuf){
		printf("Failed to allocate memory!");
		return 0;
	}

	struct quirc *qr;

	qr = quirc_new();
	if (!qr){
		printf("Failed to allocate memory");
		return 0;
	}

	if (quirc_resize(qr, WIDTH, HEIGHT) < 0){
		printf("Failed to allocate video memory");
		return 0;
	}

	printf("Watching for QR codes...\nPress START to exit.\n");

	// Main loop
	while (core::running())
	{
		hid::poll();

		if (hid::pressed(hid::BUTTON_START))
			break; // break in order to return to hbmenu

		takePicture(camBuf);
		writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), camBuf, 0, 0, WIDTH, HEIGHT);
		gpu::swapBuffers(true);

		int w=WIDTH, h=HEIGHT;
		u8 *image = (u8*)quirc_begin(qr, &w, &h);
		writePictureToIntensityMap(image, camBuf, WIDTH, HEIGHT);
		quirc_end(qr);

		int num_codes = quirc_count(qr);
		
		for (int i = 0; i < num_codes; i++){
			struct quirc_code code;
			struct quirc_data data;
			quirc_decode_error_t err;

			quirc_extract(qr, i, &code);

			writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), camBuf, 0, 0, WIDTH, HEIGHT);
			for (int j = 0; j < 4; j++) {
				struct quirc_point *a = &code.corners[j];
				struct quirc_point *b = &code.corners[(j + 1) % 4];
				bhm_line(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), a->x, a->y, b->x, b->y, 0x0077FF77);
			}
			gpu::swapBuffers(true);

			err = quirc_decode(&code, &data);
			if (!err){
				CAMU_Activate(SELECT_NONE);
				if (strncmp((const char *)data.payload, "https://mega.nz/", 16)==0 || strncmp((const char *)data.payload, "https://mega.co.nz/", 19)==0)
					doMegaInstall((char*)data.payload);
				else 
					doWebInstall((char*)data.payload);
				CAMU_Activate(SELECT_OUT1);
				printf("Watching for QR codes...\nPress START to exit.\n");
			}
		}
	}

        CAMU_Activate(SELECT_NONE);

	printf("Cleaning up...");
	gpu::swapBuffers(true);
	quirc_destroy(qr);

	free(camBuf);

	// Exit services
	camExit();
	httpcExit();
	printf("done.\n");

	core::exit();
	
	return 0;
}

