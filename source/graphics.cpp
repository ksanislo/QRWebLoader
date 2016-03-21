#include <math.h>

#include "common.h"
#include "graphics.h"

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
	u32 v = ((HEIGHT - y - 1) + (x * HEIGHT)) * 3;
	fb_8[ v ] = (((c) >>  0) & 0xFF);
	fb_8[v+1] = (((c) >>  8) & 0xFF);
	fb_8[v+2] = (((c) >> 16) & 0xFF);
}

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

