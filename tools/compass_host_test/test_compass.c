/*
 * Host-side unit test for the BMM350 e-compass math and HUD renderer
 * (no ModusToolbox needed).
 *  - tilt compensated heading over yaw 0..345, pitch/roll -40..+40 deg
 *  - hard/soft iron calibration on a synthetic distorted sphere
 *  - rejection of a calibration without enough rotation
 *  - 359/0 wrap in the heading low-pass filter
 *  - writes hud_preview.ppm with five HUD states
 */
#define _DEFAULT_SOURCE
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "compass_math.h"
#include "compass_hud.h"
static int fails=0;
#define CHECK(c,...) do{ if(!(c)){fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n");} }while(0)
static float angdiff(float a,float b){ float d=fmodf(a-b+540.f,360.f)-180.f; return fabsf(d);}
/* rotate vector about axis */
static compass_vec3_t rotz(compass_vec3_t v,float deg){float r=deg*M_PI/180;compass_vec3_t o={v.x*cosf(r)-v.y*sinf(r),v.x*sinf(r)+v.y*cosf(r),v.z};return o;}
static compass_vec3_t rotx(compass_vec3_t v,float deg){float r=deg*M_PI/180;compass_vec3_t o={v.x,v.y*cosf(r)-v.z*sinf(r),v.y*sinf(r)+v.z*cosf(r)};return o;}
static compass_vec3_t roty(compass_vec3_t v,float deg){float r=deg*M_PI/180;compass_vec3_t o={v.x*cosf(r)+v.z*sinf(r),v.y,-v.x*sinf(r)+v.z*cosf(r)};return o;}
int main(){
  /* World ENU-like: X=north, Y=west, Z=up (right handed). Earth field: 30uT north, 35uT down (dip ~49deg) */
  compass_vec3_t Bw={30,0,-35}, Uw={0,0,9.81f}, fwd={1,0,0};
  for(int yaw=0; yaw<360; yaw+=15) for(int pitch=-40; pitch<=40; pitch+=20) for(int roll=-40; roll<=40; roll+=20){
    /* device rotated by yaw clockwise seen from above (heading increases) => world->body: rotate world vectors by +yaw about z (since y=west, CW from above = +rotation about z)... */
    compass_vec3_t b=rotz(Bw,(float)yaw), u=rotz(Uw,(float)yaw);
    b=roty(b,(float)pitch); u=roty(u,(float)pitch); b=rotx(b,(float)roll); u=rotx(u,(float)roll);
    float h=compass_tilt_compensated_heading(&u,&b,&fwd);
    CHECK(angdiff(h,(float)yaw)<0.5f,"yaw %d pitch %d roll %d -> %f",yaw,pitch,roll,h);
  }
  /* level no-accel */
  { compass_vec3_t b=rotz(Bw,90); float h=compass_tilt_compensated_heading(NULL,&b,&fwd); CHECK(angdiff(h,90)<0.5f,"noacc %f",h);}
  /* degenerate */
  { compass_vec3_t z={0,0,0}; CHECK(compass_tilt_compensated_heading(&z,&Bw,&fwd)<0,"freefall"); }
  /* calibration: distorted sphere */
  compass_cal_collector_t col; compass_cal_t cal; compass_cal_collector_start(&col);
  compass_vec3_t off={12,-30,45}, sc={1.2f,0.9f,1.0f};
  srand(1);
  for(int i=0;i<2000;i++){ float th=acosf(2.f*rand()/RAND_MAX-1.f), ph=2*M_PI*rand()/RAND_MAX; float r=46.1f;
    compass_vec3_t m={r*sinf(th)*cosf(ph)*sc.x+off.x, r*sinf(th)*sinf(ph)*sc.y+off.y, r*cosf(th)*sc.z+off.z}; compass_cal_collector_add(&col,&m);}
  CHECK(compass_cal_collector_finish(&col,&cal),"cal finish");
  printf("offset %.2f %.2f %.2f scale %.3f %.3f %.3f progress %u\n",cal.offset.x,cal.offset.y,cal.offset.z,cal.scale.x,cal.scale.y,cal.scale.z,compass_cal_collector_progress(&col));
  CHECK(fabsf(cal.offset.x-12)<1&&fabsf(cal.offset.y+30)<1&&fabsf(cal.offset.z-45)<1,"offset");
  CHECK(fabsf(cal.scale.x*1.2f-cal.scale.y*0.9f)<0.03f,"scale ratio");
  /* reject insufficient rotation */
  compass_cal_collector_start(&col); for(int i=0;i<100;i++){compass_vec3_t m={30+i*0.01f,0,-35}; compass_cal_collector_add(&col,&m);} CHECK(!compass_cal_collector_finish(&col,&cal),"reject");
  /* filter wrap */
  compass_heading_filter_t f={0}; float h=0; for(int i=0;i<50;i++){ h=compass_heading_filter_update(&f,(i%2)?359.f:1.f,0.25f);} CHECK(angdiff(h,0)<1.5f,"wrap filter %f",h);
  CHECK(!strcmp(compass_cardinal_8(350),"N")&&!strcmp(compass_cardinal_8(44),"NE")&&!strcmp(compass_cardinal_8(270),"W"),"cardinal");
  /* render previews */
  static uint16_t fb[832*COMPASS_HUD_BAND_HEIGHT*5];
  compass_hud_view_t v[5]={{COMPASS_HUD_MODE_HEADING,273.4f,0,true,true,false},{COMPASS_HUD_MODE_HEADING,8.0f,0,false,false,true},{COMPASS_HUD_MODE_CALIBRATING,141,62,false,true,false},{COMPASS_HUD_MODE_NO_SENSOR},{COMPASS_HUD_MODE_WAIT}};
  for(int i=0;i<5;i++) compass_hud_draw(fb+832*COMPASS_HUD_BAND_HEIGHT*i,832,800,&v[i]);
  FILE*o=fopen("hud_preview.ppm","wb"); fprintf(o,"P6 800 %d 255\n",COMPASS_HUD_BAND_HEIGHT*5);
  for(int y=0;y<COMPASS_HUD_BAND_HEIGHT*5;y++)for(int x=0;x<800;x++){uint16_t p=fb[y*832+x]; unsigned char c[3]={(p>>11)<<3,((p>>5)&63)<<2,(p&31)<<3}; if(y%COMPASS_HUD_BAND_HEIGHT==0){c[0]=c[1]=c[2]=80;} fwrite(c,1,3,o);} fclose(o);
  printf("%s (%d failures)\n",fails?"FAILED":"ALL PASS",fails); return fails!=0;
}
