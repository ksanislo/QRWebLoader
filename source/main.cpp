#include <citrus/app.hpp>
#include <citrus/core.hpp>
#include <citrus/fs.hpp>
#include <citrus/gpu.hpp>
#include <citrus/hid.hpp>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

extern "C" {
#include <quirc.h>
}

extern "C" {
u32 __stacksize__ = 0x40000;
}

using namespace ctr;

bool onProgress(u64 pos, u64 size) {
        printf("pos: %" PRId64 "-%" PRId64 "\n", pos, size);
        gpu::flushBuffer();
        hid::poll();
        return !hid::pressed(hid::BUTTON_B);
}

Result http_getinfo(char *url, app::App *app) {
	Result ret=0;
	u32 statuscode=0;
	httpcContext context;

        app->mediaType = fs::SD;
	app->size = 0;
	app->titleId = 0x0000000000000000;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
        if(ret!=0)return ret;

        // We should /probably/ make sure Range: is supported
	// before we try to do this, but these 8 bytes are the titleId
	ret = httpcAddRequestHeaderField(&context, (char*)"Range", (char*)"bytes=11292-11299");
        if(ret!=0)return ret;

	ret = httpcBeginRequest(&context);
	if(ret!=0)return ret;

	ret = httpcGetResponseStatusCode(&context, &statuscode, 0);
	if(ret!=0)return ret;

	if(statuscode!=206)return -2; // 206 Partial Content

	u8 *buf = (u8*)malloc(8); // Allocate u8*8 == u64
	if(buf==NULL)return -1;
	memset(buf, 0, 8); // Zero out

	ret=httpcDownloadData(&context, buf, 8, NULL);
        // Safely convert our 8 byte string into a u64
	app->titleId = ((u64)buf[0] << 56 | (u64)buf[1] << 48 | (u64)buf[2] << 40 | (u64)buf[3] << 32 | (u64)buf[4] << 24 | (u64)buf[5] << 16 | (u64)buf[6] << 8 | (u64)buf[7]);
	free(buf);

        buf = (u8*)malloc(64);

        if(httpcGetResponseHeader(&context, (char*)"Content-Range", (char*)buf, 64)==0){
		char *ptr = strchr((const char *)buf, 47);
		app->size = atoll(&ptr[1]);
        }
        free(buf);


	ret = httpcCloseContext(&context);
        if(ret!=0)return ret;

	return 0;
}

Result http_download(char *url, app::App *app) {
	Result ret=0;
	httpcContext context;
	u32 statuscode=0;
        u32 contentsize=0, downloadsize=0;
	char *buf;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
        if(ret!=0)return ret;

        ret = httpcAddRequestHeaderField(&context, (char*)"Accept-Encoding", (char*)"gzip, deflate");
        if(ret!=0)return ret;

	ret = httpcBeginRequest(&context);
	if(ret!=0)return ret;

	ret = httpcGetResponseStatusCode(&context, &statuscode, 0);
	if(ret!=0)return ret;

	if(statuscode!=200)return -2;

	ret=httpcGetDownloadSizeState(&context, &downloadsize, &contentsize);
	if(ret!=0)return ret;

        buf = (char*)malloc(16);
	if(buf==NULL)return -1;
	memset(buf, 0, 16);

	if(httpcGetResponseHeader(&context, (char*)"Content-Encoding", (char*)buf, 16)==0){
                printf("Content-Encoding: %s\n", buf);
        }

        app::install(app->mediaType, &context, app->size, &onProgress);

	free(buf);

	ret = httpcCloseContext(&context);
	if(ret!=0)return ret;

	return ret;
}

#define WIDTH 640
#define HEIGHT 480
#define WAIT_TIMEOUT 300000000ULL

void takePicture(u16 *buf) {
	u32 bufSize;
	Handle camReceiveEvent = 0;

	CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT);
	CAMU_SetTransferBytes(PORT_CAM1, bufSize, WIDTH, HEIGHT);
	CAMU_Activate(SELECT_OUT1);
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	CAMU_SetReceiving(&camReceiveEvent, (u8*)buf, PORT_CAM1, WIDTH * HEIGHT * 2, (s16) bufSize);
	svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT);
	CAMU_StopCapture(PORT_CAM1);
	svcCloseHandle(camReceiveEvent);
	CAMU_Activate(SELECT_NONE);
}

void writePictureToIntensityMap(void *fb, void *img, u16 width, u16 height) {
        u8 *fb_8 = (u8*) fb;
        u16 *img_16 = (u16*) img;
        for(u32 i = 0; i < width * height; i++) {
                u16 data = img_16[i];
                uint8_t b = ((data >> 11) & 0x1F) << 3;
                uint8_t g = ((data >> 5) & 0x3F) << 2;
                uint8_t r = (data & 0x1F) << 3;
                fb_8[i] = (r + g + b) / 3;
        }
}

/*
void downsampleVGAtoQVGA(void *fb, void *img) {
	u16 *fb_16 = (u16*) fb;
	u16 *img_16 = (u16*) img;
	for(x = 0; x < 640; x=+2) {
		for(y = 0; y < 480; y=+2) {
		fb_16[((x/2) * 240 + (y/2)) / 2] = img_16[x * 640 + y]
		}
	}

}
*/

void writePictureToFramebufferRGB565(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
        u8 *fb_8 = (u8*) fb;
        u16 *img_16 = (u16*) img;
        for(int j = 0; j < height; j++) {
                for(int i = 0; i < width; i++) {
                        int draw_y = y + (height - j)/2;
                        int draw_x = x + i/2;
                        u32 v = (draw_y + draw_x * (height/2)) * 3;
			u16 data = img_16[j * width + i];
                        uint8_t b = ((data >> 11) & 0x1F) << 3;
                        uint8_t g = ((data >> 5) & 0x3F) << 2;
                        uint8_t r = (data & 0x1F) << 3;
                        fb_8[v] = r;
                        fb_8[v+1] = g;
                        fb_8[v+2] = b;
                }
        }
}

int doWebInstall (char *url) {
	app::App app;
	Result ret=0;
	
	printf("Processing %s\n",url);
	gpu::flushBuffer();

	ret = http_getinfo(url, &app);
	if(ret!=0)return ret;

	printf("titleId: 0x%llx\n", app.titleId);
	printf("Press A to install\n      X to uninstall\n      B to cancel.\n");
	while (core::running()) {
		hid::poll();

		if (hid::pressed(hid::BUTTON_X)) {
			if(app.titleId != 0 && app::installed(app)) { // Check if we have a titleId to remove
				printf("Uninstalling...");
				gpu::flushBuffer();
				gpu::swapBuffers(true);
				app::uninstall(app);
				printf("done.\n");
				gpu::flushBuffer();
			} else {
				printf("titleId isn't installed\n");
				gpu::flushBuffer();
				gpu::swapBuffers(true);
			}
		}

		if (hid::pressed(hid::BUTTON_A)) {
			ret = http_download(url, &app);
			if(ret!=0)return ret;

			printf("titleId: 0x%llx\nInstall finished.\n", app.titleId);
			gpu::flushBuffer();
			return ret;
		}
			

		if (hid::pressed(hid::BUTTON_B))
			break;
			

	}
	return 0;
}

int main(int argc, char **argv)
{
        core::init(argc);
	httpcInit();
	camInit();

	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	consoleInit(GFX_BOTTOM,NULL);
	gfxSet3D(false);


	printf("Initializing camera...");
	gpu::flushBuffer();
	gpu::swapBuffers(true);
	CAMU_SetSize(SELECT_OUT1, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetAutoExposure(SELECT_OUT1, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
	CAMU_SetTrimming(PORT_CAM1, false);
	printf("done.\n");
	gpu::flushBuffer();
	gpu::swapBuffers(true);

	u16 *camBuf = (u16*)malloc(WIDTH * HEIGHT * 2);
	if(!camBuf) {
		printf("Failed to allocate memory!");
		return 0;
	}

	struct quirc *qr;

	qr = quirc_new();
	if (!qr) {
		printf("Failed to allocate memory");
		return 0;
	}

	if (quirc_resize(qr, WIDTH, HEIGHT) < 0) {
		printf("Failed to allocate video memory");
		return 0;
	}

	printf("Watching for QR codes...\nPress START to exit.\n");
	// Main loop
	while (core::running())
	{
		hid::poll();

		// Your code goes here

		if (hid::pressed(hid::BUTTON_START))
			break; // break in order to return to hbmenu

		takePicture(camBuf);
		//writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), camBuf, 0, 0, WIDTH, HEIGHT);

		int w=WIDTH, h=HEIGHT;
		u8 *image = (u8*)quirc_begin(qr, &w, &h);
		writePictureToIntensityMap(image, camBuf, WIDTH, HEIGHT);
		quirc_end(qr);

		int num_codes = quirc_count(qr);
		gpu::flushBuffer();
		for (int i = 0; i < num_codes; i++) {
			struct quirc_code code;
			struct quirc_data data;
			quirc_decode_error_t err;

			quirc_extract(qr, i, &code);

			err = quirc_decode(&code, &data);
			if (!err) {
				doWebInstall((char*)data.payload);
				printf("Watching for QR codes...\nPress START to exit.\n");
			}
			// else	printf("DECODE FAILED: %s\n", quirc_strerror(err));
	
		}

		// Flush and swap framebuffers
		gpu::flushBuffer();
		gpu::swapBuffers(true);
	}

	printf("Cleaning up...");
	gpu::flushBuffer();
	gpu::swapBuffers(true);
	quirc_destroy(qr);

	free(camBuf);

	// Exit services
	camExit();
	httpcExit();
	printf("done.\n");
	gpu::flushBuffer();

	core::exit();
	
	return 0;
}

