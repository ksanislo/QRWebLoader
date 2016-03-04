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

#define SSLOPTION_NOVERIFY 1<<9
#define WIDTH 640
#define HEIGHT 480
#define WAIT_TIMEOUT 300000000ULL
#define AUTOLOADER_FILE "web-updater.url"
#define AUTOLOADER_URL "http://3ds.intherack.com/files/web-updater.cia"
#define AUTOLOADER_TITLEID 0x000400000b198200
#define TITLEID 0x000400000b198900

extern "C" {
u32 __stacksize__ = 0x40000;
}

using namespace ctr;

bool onProgress(u64 pos, u64 size){
	printf("pos: %" PRId64 "-%" PRId64 "\n", pos, size);
	hid::poll();
	return !hid::pressed(hid::BUTTON_B);
}


Result http_getinfo(char *url, app::App *app){
	Result ret=0;
	u8* buf;
	u32 statuscode=0;
	httpcContext context;

	app->mediaType = fs::SD;
	app->size = 0;
	app->titleId = 0x0000000000000000;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
	if(ret!=0){
		goto stop;
	}

	// We should /probably/ make sure Range: is supported
	// before we try to do this, but these 8 bytes are the titleId
	ret = httpcAddRequestHeaderField(&context, (char*)"Range", (char*)"bytes=11292-11299");
	if(ret!=0){
		goto stop;
	}

	// This disables the SSL certificate checks.
	ret = httpcSetSSLOpt(&context, SSLOPTION_NOVERIFY);
	if(ret!=0){
		goto stop;
	}

	ret = httpcBeginRequest(&context);
	if(ret!=0){
		goto stop;
	}

	ret = httpcGetResponseStatusCode(&context, &statuscode, 0);
	if(ret!=0){
		goto stop;
	}

	if(statuscode==301||statuscode==302){
		ret = httpcGetResponseHeader(&context, (char*)"Location", (char*)url, QUIRC_MAX_PAYLOAD-1);
		if(ret!=0){
			goto stop;
		}

		ret=http_getinfo(url, app);
		goto stop;
	}

	if(statuscode==206){
		buf = (u8*)malloc(8); // Allocate u8*8 == u64
		if(buf==NULL){
			goto stop;
		}
		memset(buf, 0, 8); // Zero out

		ret = httpcDownloadData(&context, buf, 8, NULL);

		// Safely convert our 8 byte string into a u64
		app->titleId = ((u64)buf[0]<<56|(u64)buf[1]<<48|(u64)buf[2]<<40|(u64)buf[3]<<32|(u64)buf[4]<<24|(u64)buf[5]<<16|(u64)buf[6]<<8|(u64)buf[7]);
		free(buf);

		buf = (u8*)malloc(64);
		if(buf==NULL){
			goto stop;
		}

		ret = httpcGetResponseHeader(&context, (char*)"Content-Range", (char*)buf, 64);
		if(ret==0){
			char *ptr = strchr((const char *)buf, 47);
			app->size = atoll(&ptr[1]);
		}

		free(buf);
	} else {
		printf("HTTP status: %lu", statuscode);
	}

	stop:
	ret = httpcCloseContext(&context);

	return ret;
}

Result http_download(char *url, app::App *app){
	Result ret=0;
	httpcContext context;
	u32 statuscode=0;
	u32 contentsize=0, downloadsize=0;
	char *buf;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
	if(ret!=0){
		goto stop;
	}

	ret = httpcAddRequestHeaderField(&context, (char*)"Accept-Encoding", (char*)"gzip, deflate");
	if(ret!=0){
		goto stop;
	}

	// This disables the SSL certificate checks.
	ret = httpcSetSSLOpt(&context, 1<<9);
	if(ret!=0){
		goto stop;
	}

	ret = httpcBeginRequest(&context);
	if(ret!=0){
		goto stop;
	}

	ret = httpcGetResponseStatusCode(&context, &statuscode, 0);
	if(ret!=0){
		goto stop;
	}

	if(statuscode==200){
		ret = httpcGetDownloadSizeState(&context, &downloadsize, &contentsize);
		if(ret!=0){
			goto stop;
		}

		buf = (char*)malloc(16);
		if(buf==NULL){
			goto stop;
		}
		memset(buf, 0, 16);

		ret = httpcGetResponseHeader(&context, (char*)"Content-Encoding", (char*)buf, 16);
		if(ret==0){
			goto stop;
		}

		printf("Content-Encoding: %s\n", buf);

		app::install(app->mediaType, &context, app->size, &onProgress);

		free(buf);
	} else {
		printf("HTTP status: %lu", statuscode);
	}

	stop:
	ret = httpcCloseContext(&context);

	return ret;
}


void takePicture(u16 *buf){
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

void writePictureToIntensityMap(void *fb, void *img, u16 width, u16 height){
	u8 *fb_8 = (u8*) fb;
	u16 *img_16 = (u16*) img;
	for(u32 i = 0; i < width * height; i++){
		u16 data = img_16[i];
		uint8_t b = ((data >> 11) & 0x1F) << 3;
		uint8_t g = ((data >> 5) & 0x3F) << 2;
		uint8_t r = (data & 0x1F) << 3;
		fb_8[i] = (r + g + b) / 3;
	}
}

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

int useAutoloader(char *url, app::App app){
	Result ret=0;
	FILE *fd = fopen(AUTOLOADER_FILE, "wb");
	if(fd == NULL) return -1;
	fwrite(url,sizeof(char),strlen(url),fd);
	fclose(fd);

	app.titleId = AUTOLOADER_TITLEID;

	if(app::installed(app)){ 
		ctr::app::launch(app);
	} else {
		strcpy(url,(char*)AUTOLOADER_URL);
			ret = http_getinfo(url, &app);
			if(ret!=0)return ret;

			ret = http_download(url, &app);
			if(ret!=0)return ret;

			ctr::app::launch(app);
	}

	while (core::running()){
		hid::poll();
		if (hid::pressed(hid::BUTTON_START))
			break;
	}

	return 0;
}


int doWebInstall (char *url){
	app::App app;
	Result ret=0;
	
	printf("Processing %s\n",url);

	ret = http_getinfo(url, &app);
	if(ret!=0)return ret;

	printf("titleId: 0x%llx\n", app.titleId);
	if (app.titleId == TITLEID) printf("This .cia matches our titleId, direct\ninstall and uninstall disabled.\n");
	printf("Press B to cancel\n");
	if (app.titleId != AUTOLOADER_TITLEID) printf("      Y to use Autoloader\n");
	if (app.titleId != TITLEID && app::installed(app)) printf("      X to uninstall\n");
	if (app.titleId != TITLEID) printf("      A to install\n");

	while (core::running()){
		hid::poll();

		if (hid::pressed(hid::BUTTON_X) && app.titleId != TITLEID && app::installed(app)){
			printf("Uninstalling...");
			app::uninstall(app);
			printf("done.\n");
		}

		if (hid::pressed(hid::BUTTON_Y)){
			useAutoloader(url, app);
			return 0;
		}

		if (hid::pressed(hid::BUTTON_A) && app.titleId != TITLEID && ! app::installed(app)){
			ret = http_download(url, &app);
			if(ret!=0)return ret;

			printf("titleId: 0x%llx\nInstall finished.\n", app.titleId);
			return ret;
		}

		if (hid::pressed(hid::BUTTON_B))
			break;
	}
	return 0;
}

int main(int argc, char **argv){
	core::init(argc);
	httpcInit(0x1000);
	camInit();

	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);
	memset(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x30, 400 * 240 * 3);
	
	gpu::swapBuffers(true);
	memset(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), 0x30, 400 * 240 * 3);
	
	gpu::swapBuffers(true);


	consoleInit(GFX_BOTTOM,NULL);
	gfxSet3D(false);


	printf("Initializing camera...");
	
	gpu::swapBuffers(true);
	CAMU_SetSize(SELECT_OUT1, SIZE_VGA, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetAutoExposure(SELECT_OUT1, false);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, false);
	//CAMU_SetEffect(SELECT_OUT1, EFFECT_NONE, CONTEXT_A);
	CAMU_SetTrimming(PORT_CAM1, false);
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
		writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), camBuf, 40, 0, WIDTH, HEIGHT);

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

			err = quirc_decode(&code, &data);
			if (!err){
				doWebInstall((char*)data.payload);
				printf("Watching for QR codes...\nPress START to exit.\n");
			}
			// else	printf("DECODE FAILED: %s\n", quirc_strerror(err));
	
		}

		// Flush and swap framebuffers
		
		gpu::swapBuffers(true);
	}

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

