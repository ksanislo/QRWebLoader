#include <citrus/app.hpp>
#include <citrus/core.hpp>
#include <citrus/fs.hpp>
#include <citrus/gpu.hpp>
#include <citrus/hid.hpp>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>

extern "C" {
#include <quirc.h>
}

#define SSLOPTION_NOVERIFY 1<<9
#define WIDTH 400
#define HEIGHT 240
#define WAIT_TIMEOUT 300000000ULL
#define AUTOLOADER_FILE "autoloader.url"
#define AUTOLOADER_URL "http://3ds.intherack.com/files/AutoLoader.cia"
#define AUTOLOADER_TITLEID 0x000400000b198200
#define TITLEID 0x000400000b198900

extern "C" {
u32 __stacksize__ = 0x40000;
}

void bhm_line(void *fb, int x1, int y1, int x2, int y2, u32 c);

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

	// Disable the SSL certificate checks.
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
		printf("HTTP status: %lu\n", statuscode);
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

	// Disable the SSL certificate checks.
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
			printf("Content-Encoding: %s\n", buf);
		}

		app::install(app->mediaType, &context, app->size, &onProgress);

		free(buf);
	} else {
		printf("HTTP status: %lu\n", statuscode);
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
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	CAMU_SetReceiving(&camReceiveEvent, (u8*)buf, PORT_CAM1, WIDTH * HEIGHT * 2, (s16) bufSize);
	svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT);
	CAMU_StopCapture(PORT_CAM1);
	svcCloseHandle(camReceiveEvent);
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
	int i, j, draw_x, draw_y;
	for(j = 0; j < height; j++) {
		for(i = 0; i < width; i++) {
			draw_y = y + height-1 - j;
			draw_x = x + i;
			u32 v = (draw_y + draw_x * height) * 3;
			u16 data = img_16[j * width + i];
			uint8_t b = ((data >> 11) & 0x1F) << 3;
			uint8_t g = ((data >> 5) & 0x3F) << 2;
			uint8_t r = (data & 0x1F) << 3;
			fb_8[ v ] = r;
			fb_8[v+1] = g;
			fb_8[v+2] = b;
		}
	}
}

void putpixel(void *fb, int x, int y, u32 c) {
	u8 *fb_8 = (u8*) fb;
	u32 v = ((HEIGHT - y) + (x * HEIGHT)) * 3;
	fb_8[ v ] = (((c) >>  0) & 0xFF);
	fb_8[v+1] = (((c) >>  8) & 0xFF);
	fb_8[v+2] = (((c) >> 16) & 0xFF);
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

	if(app.titleId>>48 != 0x4){ // 3DS titleId
		printf("Not a 3DS .cia file.\n");
		return -1;
	}

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

/*BRESENHAAM ALGORITHM FOR LINE DRAWING*/
void bhm_line(void *fb,int x1,int y1,int x2,int y2,u32 c)
{
	int x,y,dx,dy,dx1,dy1,px,py,xe,ye,i;
	dx=x2-x1;
	dy=y2-y1;
	dx1=fabs(dx);
	dy1=fabs(dy);
	px=2*dy1-dx1;
	py=2*dx1-dy1;
	if(dy1<=dx1){
		if(dx>=0){
			x=x1;
			y=y1;
			xe=x2;
		} else {
			x=x2;
			y=y2;
			xe=x1;
		}
		putpixel(fb,x,y,c);
		for(i=0;x<xe;i++){
			x=x+1;
			if(px<0){
				px=px+2*dy1;
			} else {
				if((dx<0 && dy<0) || (dx>0 && dy>0)){
					y=y+1;
				} else {
					y=y-1;
				}
				px=px+2*(dy1-dx1);
			}
			putpixel(fb,x,y,c);
		}
	} else {
		if(dy>=0){
			x=x1;
			y=y1;
			ye=y2;
		} else {
			x=x2;
			y=y2;
			ye=y1;
		}
		putpixel(fb,x,y,c);
		for(i=0;y<ye;i++){
			y=y+1;
			if(py<=0){
				py=py+2*dx1;
			} else {
				if((dx<0 && dy<0) || (dx>0 && dy>0)){
					x=x+1;
				} else {
					x=x-1;
				}
				py=py+2*(dx1-dy1);
			}
			putpixel(fb,x,y,c);
		}
	}
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
	CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
	CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
	CAMU_SetNoiseFilter(SELECT_OUT1, true);
	CAMU_SetWhiteBalance(SELECT_OUT1, WHITE_BALANCE_AUTO);
	CAMU_SetContrast(SELECT_OUT1, CONTRAST_NORMAL);
	CAMU_SetAutoExposure(SELECT_OUT1, true);
	CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
	CAMU_SetAutoExposureWindow(SELECT_OUT1, 100, 20, 200, 200);
	CAMU_SetAutoWhiteBalanceWindow(SELECT_OUT1, 100, 20, 400, 200);
	CAMU_SetTrimming(PORT_CAM1, false);
        CAMU_Activate(SELECT_OUT1);
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

