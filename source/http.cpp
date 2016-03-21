#include <citrus/app.hpp>
#include <citrus/core.hpp>
#include <citrus/hid.hpp>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "http.h"

extern "C" {
#include <quirc.h>
}

#include "autoloader.h"
#include "common.h"

using namespace ctr;

bool onProgress(u64 pos, u64 size);

static httpcContext context;

int fetchHttpData(void *buf, u32 bufSize, u32 *bufFill){
	return httpcDownloadData(&context, (u8*)buf, bufSize, bufFill);
}

Result http_getinfo(char *url, app::App *app){
	Result ret=0;
	u8* buf;
	u32 statuscode=0;

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
	u32 statuscode=0;
	char *buf;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
	if(ret!=0){
		goto stop;
	}

	/* No gzip until libarchive is implemented.
	ret = httpcAddRequestHeaderField(&context, (char*)"Accept-Encoding", (char*)"gzip, deflate");
	if(ret!=0){
		goto stop;
	}
	*/

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
		buf = (char*)malloc(16);
		if(buf==NULL){
			goto stop;
		}
		memset(buf, 0, 16);

		ret = httpcGetResponseHeader(&context, (char*)"Content-Encoding", (char*)buf, 16);
		if(ret==0){
			printf("Content-Encoding: %s\n", buf);
		}

		app::install(app->mediaType, &fetchHttpData, app->size, &onProgress);

		free(buf);
	} else {
		printf("HTTP status: %lu\n", statuscode);
	}

	stop:
	ret = httpcCloseContext(&context);

	return ret;
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

	printf("titleId: 0x%016llx\n", app.titleId);
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
			return 0;
		}

		if (hid::pressed(hid::BUTTON_Y)){
			useAutoloader(url, app);
			return 0;
		}

		if (hid::pressed(hid::BUTTON_A) && app.titleId != TITLEID && ! app::installed(app)){
			ret = http_download(url, &app);
			if(ret!=0)return ret;

			printf("titleId: 0x%016llx\nInstall finished.\n", app.titleId);
			return ret;
		}

		if (hid::pressed(hid::BUTTON_B))
			break;
	}
	return 0;
}

