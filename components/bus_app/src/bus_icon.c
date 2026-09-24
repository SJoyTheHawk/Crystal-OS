#include "lvgl.h"
enum { S=64 }; static lv_color_t pixels[S*S];
static int rr(int x,int y,int x0,int y0,int x1,int y1,int r){if(x<x0||x>x1||y<y0||y>y1)return 0;int cx=x<x0+r?x0+r:(x>x1-r?x1-r:x),cy=y<y0+r?y0+r:(y>y1-r?y1-r:y);int dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=r*r;}
void bus_icon_prepare(void){for(int y=0;y<S;y++)for(int x=0;x<S;x++){lv_color_t c=lv_color_hex(0x1E293B);if(rr(x,y,7,4,56,55,8))c=lv_color_hex(0xE31E24);if(rr(x,y,15,11,27,22,2)||rr(x,y,36,11,48,22,2)||rr(x,y,15,35,48,45,2))c=lv_color_hex(0x38BDF8);for(int i=0;i<2;i++){int cx=19+i*26,cy=54,dx=x-cx,dy=y-cy;if(dx*dx+dy*dy<=25)c=lv_color_hex(0x334155); } pixels[y*S+x]=c;}}
const lv_img_dsc_t bus_icon={.header={.cf=LV_IMG_CF_TRUE_COLOR,.always_zero=0,.reserved=0,.w=S,.h=S},.data_size=sizeof(pixels),.data=(const uint8_t*)pixels};
