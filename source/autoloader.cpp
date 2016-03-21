#include <citrus/app.hpp>
#include <citrus/core.hpp>
#include <citrus/hid.hpp>

#include "http.h"
#include "autoloader.h"
#include "common.h"

using namespace ctr;

int useAutoloader(char *url, app::App app){
	Result ret=0;
	FILE *fd = fopen(AUTOLOADER_FILE, "wb");
	if(fd == NULL) return -1;
	fwrite(url,sizeof(char),strlen(url),fd);
	fclose(fd);

	app.titleId = AUTOLOADER_TITLEID;

	if(app::installed(app)){ 
		app::launch(app);
	} else {
		strcpy(url,(char*)AUTOLOADER_URL);
			ret = http_getinfo(url, &app);
			if(ret!=0)return ret;

			ret = http_download(url, &app);
			if(ret!=0)return ret;

			app::launch(app);
	}

	while (core::running()){
		hid::poll();
		if (hid::pressed(hid::BUTTON_START))
			break;
	}

	return 0;
}

