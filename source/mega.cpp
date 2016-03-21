#include <citrus/app.hpp>
#include <citrus/hid.hpp>
#include <citrus/core.hpp>

#include <mbedtls/base64.h>
#include <mbedtls/aes.h>

extern "C" {
#include <jsmn.h>
}

#include "mega.h"
#include "common.h"

using namespace ctr;

static u8 *aeskey, *aesiv;
static httpcContext context;


bool onProgress(u64 pos, u64 size);

uint64_t swap_uint64( uint64_t val ) {
    val = ((val << 8) & 0xFF00FF00FF00FF00ULL ) | ((val >> 8) & 0x00FF00FF00FF00FFULL );
    val = ((val << 16) & 0xFFFF0000FFFF0000ULL ) | ((val >> 16) & 0x0000FFFF0000FFFFULL );
    return (val << 32) | (val >> 32);
}

int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
	if (tok->type == JSMN_STRING && (int) strlen(s) == tok->end - tok->start &&
			strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
		return 0;
	}
	return -1;
}

int decodeMegaFileName(char *filename, char *buf) {
	int i, r, c;
	int len = strlen(buf);
	size_t olen;
	unsigned char zeroiv[16]={0};
	char *jsonbuf;
	jsmn_parser p;
	jsmntok_t t[128]; // We expect no more than 128 tokens

	for(c = 0; c < len + ((len * 3) & 0x03); c++){
		if(buf[c] == '-')
			buf[c] = '+';
		else if(buf[c] == '_')
			buf[c] = '/';
		if (c >= len)
			buf[c] = '=';
	}

	strcpy(filename, buf); // store in our return filename for temp space

	mbedtls_base64_decode(NULL, 0, &olen, (const unsigned char*)filename, strlen(filename));
	buf = (char*)realloc(buf, olen);
	mbedtls_base64_decode((unsigned char*)buf, olen, &olen, (const unsigned char*)filename, strlen(filename));

	mbedtls_aes_context aes;
	mbedtls_aes_setkey_dec( &aes, aeskey, 128 );
	jsonbuf = (char*)malloc(olen);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, olen, &zeroiv[0], (unsigned char*) buf, (unsigned char*) jsonbuf);
	mbedtls_aes_free( &aes );
	if(strncmp("MEGA", jsonbuf, 4)!=0){
		printf("MEGA magic string not found.\nDecryption key bad/missing?\n");
		return 1;
	}
	jsonbuf+=4; // Bypass header.

	r = jsmn_parse(&p, jsonbuf, strlen(jsonbuf), t, sizeof(t)/sizeof(t[0]));
	if (r < 0) {
		printf("Failed to parse JSON: %d\n", r);
		return 1;
	}

	if (r < 1 || t[0].type != JSMN_OBJECT) {
		printf("Object expected\n");
		return 1;
	}

	for (i = 1; i < r; i++) {
		if (jsoneq(jsonbuf, &t[i], "n") == 0) {
			strncpy(filename,jsonbuf + t[i+1].start,t[i+1].end-t[i+1].start);
			filename[t[i+1].end-t[i+1].start] = (char)NULL;
			i++;
		}
	}

	free(jsonbuf-4); // Free the original jsonbuf 
	return 0;
}

int parseMegaFileResponse(char *jsonString, char *filename, u32 *size, char* url) {
	int i, r;
	char *buf;
	jsmn_parser p;
	jsmntok_t t[128]; /* We expect no more than 128 tokens */

	jsmn_init(&p);
	r = jsmn_parse(&p, jsonString, strlen(jsonString), t, sizeof(t)/sizeof(t[0]));
	if (r < 0) {
		printf("Failed to parse JSON: %d\n", r);
		return 1;
	}

	/* Assume the top-level element is an object */
	if (r < 1 || t[0].type != JSMN_ARRAY) {
		printf("Array expected\n");
		return 1;
	}

	/* Loop over all keys of the root object */
	for (i = 2; i < r; i++) {
		if (jsoneq(jsonString, &t[i], "s") == 0) { // size
			*size = strtoul(jsonString + t[i+1].start, NULL, 10);
			i++;
		} else if (jsoneq(jsonString, &t[i], "at") == 0) { // filename
			// This will be base64 encoded, allocate with padding.
			buf=(char*)malloc(t[i+1].end-t[i+1].start + 1 + ((t[i+1].end-t[i+1].start * 3) & 0x03) );
			memset(buf,0,t[i+1].end-t[i+1].start + 1 + ((t[i+1].end-t[i+1].start * 3) & 0x03));
			strncpy(buf,jsonString + t[i+1].start,t[i+1].end-t[i+1].start);
			buf[t[i+1].end-t[i+1].start + ((t[i+1].end-t[i+1].start * 3) & 0x03)] = (char)NULL;
			decodeMegaFileName(filename, buf);
			free(buf);
			i++;
		} else if (jsoneq(jsonString, &t[i], "g") == 0) { // url
                        strncpy(url,jsonString + t[i+1].start,t[i+1].end-t[i+1].start);
			url[t[i+1].end-t[i+1].start] = (char)NULL;
			i++;
		}
	}
	return 0;
}

int fetchMegaData(void *buf, u32 bufSize, u32 *bufFill){
        Result ret = 0;
	u32 downloadpos = 0;
	u64 *aesiv_64 = (u64*) aesiv;

	char *startptr, *endptr;
	u32 startpos = 0;
	u32 decodepos = 0;
	size_t chunksize = 0;

	size_t offset=0;

	unsigned char stream_block[16];
	u8 *dlbuf;
	
	u8 *contentrange = (u8*)malloc(256);
	if(httpcGetResponseHeader(&context, (char*)"Content-Range", (char*)contentrange, 256)==0){ 
		startptr = strchr((char *)contentrange, ' ');
		startptr++[0] = (char)NULL; // end string
		endptr = strchr((char *)startptr, '-');
		endptr[0] = (char)NULL; // end string
		startpos = atol(startptr);
	}
	free(contentrange);

	ret = httpcGetDownloadSizeState(&context, &downloadpos, NULL);
	if(ret!=0){
		goto stop;
	}
	startpos += downloadpos;	

	dlbuf = (u8*)malloc(bufSize);
	memset(dlbuf, 0, bufSize);

	ret = httpcDownloadData(&context, dlbuf, bufSize, bufFill);
	if(ret!=0 && ret != (s32)HTTPC_RESULTCODE_DOWNLOADPENDING){
		goto stop;
	}

	mbedtls_aes_context aes;
	mbedtls_aes_setkey_enc( &aes, aeskey, 128 );

	aesiv_64[1] = swap_uint64((u64)startpos/16); // Set our IV block location.
	offset = startpos % 16; // Set our starting block offset

	if(offset != 0){ // If we have an offset, we need to pre-fill stream_block
		mbedtls_aes_crypt_ecb( &aes, MBEDTLS_AES_ENCRYPT, aesiv, stream_block );
		aesiv_64[1] = swap_uint64(((u64)startpos/16) + 1); // Bump counter
	}

	for (decodepos = 0;  decodepos < *bufFill ; decodepos+=0x1000) { // AES decrypt in 4K blocks
		chunksize = (((*bufFill - decodepos) < 0x1000) ? (*bufFill - decodepos) : 0x1000 );
		mbedtls_aes_crypt_ctr( &aes, chunksize, &offset, aesiv, stream_block, dlbuf+decodepos, (unsigned char*)buf+decodepos );
		if (decodepos + chunksize == *bufFill) break;
	}

	mbedtls_aes_free( &aes );
	free(dlbuf);

stop:
	return ret;
}

int decodeMegaFileKey(char* str)
{
	u64 *aeskey_64 = (u64*) aeskey;
	u64 *aesiv_64 = (u64*) aesiv;
	int len = strlen(str);
	int newlen = len + ((len * 3) & 0x03);
	int i;
	size_t olen;
	u8 *buf;
	u64 *buf_64;

	//Remove URL base64 encoding, and pad with =
	for(i = 0; i < newlen; i++){
		if(str[i] == '-')
			str[i] = '+';
		else if(str[i] == '_')
			str[i] = '/';

		if (i >= len)
			str[i] = '=';
	}

	buf = (u8*)malloc(256/8);
	int ret = mbedtls_base64_decode((unsigned char*)buf, (256/8), &olen, (const unsigned char*)str, newlen);
	buf_64 = (u64*) buf;
	aeskey_64[0] = buf_64[0] ^ buf_64[2];
	aeskey_64[1] = buf_64[1] ^ buf_64[3];
	aesiv_64[0] = buf_64[2];
	aesiv_64[1] = 0;
	free(buf);
	return ret;
}

int prepareMegaInstall (char *url, app::App *app){
	Result ret=0;

	char *ptr, *locptr, *keyptr;
	u8 *req, *buf;
	u32 bufLen;
	int reqlen;
	u32 filesize;
	char *filename;
	char megafileid[128];

	// Allocate space for 128 bit AES key and 128 bit AES IV
	aeskey = (u8*)malloc(128 / 8);
	aesiv = (u8*)malloc(128 / 8);

	// Allocate URL length+4 bytes since we may need to pad with =
	buf = (u8*)malloc(strlen(url)+4);
	strcpy((char*)buf, url);
	ptr = strchr((const char *)buf, '#');
	if (ptr[1] != '!'){
		printf("URL not supported\n");
		goto stop;
	}
	locptr = strchr((const char *)ptr, '!');
	locptr++[0] = (char)NULL; // end prev string
	keyptr = strchr((const char *)locptr, '!');
	keyptr++[0] = (char)NULL; // end prev string

	strncpy(megafileid, locptr, sizeof(megafileid));

	// Decode the URL for our AES key
	decodeMegaFileKey(keyptr);

	req = (u8*)malloc(256);
	reqlen = sprintf((char*)req, "[{\"a\":\"g\",\"g\":1,\"p\":\"%s\"}]", megafileid);

	httpcOpenContext(&context, HTTPC_METHOD_POST, (char*)"https://g.api.mega.co.nz/cs", 0);
	httpcSetSSLOpt(&context, 1<<9);
	httpcAddPostDataRaw(&context, (u32*)req, reqlen);

	httpcBeginRequest(&context);

	buf = (u8*)realloc(buf, 0x1000);
	ret = httpcDownloadData(&context, buf, 0x1000, &bufLen);
	free(req);

	filename = (char*)malloc(0x1000);
	parseMegaFileResponse((char*)buf, filename, &filesize, url);
	httpcCloseContext(&context);
	printf("file: %s\nsize: %lu\n", filename, filesize);

	app->size = filesize;
	app->mediaType = fs::SD;

	free(filename);
stop:
	free(buf);
	return ret;
}

int getMegaInfo (char *url, app::App *app){
        Result ret=0;

	u8 *buf;
	u32 bufFill;
	u32 statuscode;

	ret=httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
	ret=httpcSetSSLOpt(&context, 1<<9);
	ret=httpcAddRequestHeaderField(&context, (char*)"Range", (char*)"bytes=11292-11299");
	ret=httpcBeginRequest(&context);
	httpcGetResponseStatusCode(&context, &statuscode, 0);

	buf = (u8*)malloc(8);
	fetchMegaData(buf, 8, &bufFill);
	app->titleId = ((u64)buf[0]<<56|(u64)buf[1]<<48|(u64)buf[2]<<40|(u64)buf[3]<<32|(u64)buf[4]<<24|(u64)buf[5]<<16|(u64)buf[6]<<8|(u64)buf[7]);
	httpcCloseContext(&context);
	free(buf);
	return ret;
}

int getMegaDownload (char *url, app::App *app){
        Result ret=0;
	u32 statuscode;

	ret=httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
	ret=httpcSetSSLOpt(&context, 1<<9);
	ret=httpcBeginRequest(&context);
	httpcGetResponseStatusCode(&context, &statuscode, 0); 
	app::install(app->mediaType, &fetchMegaData, app->size, &onProgress);
	httpcCloseContext(&context);
	return ret;
}

void cleanupMegaInstall() {
        free(aeskey);
        free(aesiv);
}

int doMegaInstall (char *url){
	app::App app;
	Result ret=0;

	printf("Processing %s\n",url);

	prepareMegaInstall(url, &app);

	ret = getMegaInfo(url, &app);
	if(ret!=0)return ret;

	if(app.titleId>>48 != 0x4){ // 3DS titleId
		printf("Not a 3DS .cia file.\n");
		return -1;
	}

	printf("titleId: 0x%016llx\n", app.titleId);
	if (app.titleId == TITLEID) printf("This .cia matches our titleId, direct\ninstall and uninstall disabled.\n");
	printf("Press B to cancel\n");
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

		if (hid::pressed(hid::BUTTON_A) && app.titleId != TITLEID && ! app::installed(app)){
			ret = getMegaDownload(url, &app);
			if(ret!=0)return ret;

			printf("titleId: 0x%016llx\nInstall finished.\n", app.titleId);
			return ret;
		}

		if (hid::pressed(hid::BUTTON_B))
			break;
	}
	return 0;
}

