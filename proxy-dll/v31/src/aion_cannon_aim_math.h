#pragma once
#include <math.h>
// Column-major native matrices. Only elevation changes; azimuth remains native.
struct AcVec {float x,y,z;};
static AcVec AcV(float x,float y,float z){AcVec v={x,y,z};return v;}
static AcVec AcAdd(AcVec a,AcVec b){return AcV(a.x+b.x,a.y+b.y,a.z+b.z);}
static AcVec AcSub(AcVec a,AcVec b){return AcV(a.x-b.x,a.y-b.y,a.z-b.z);}
static AcVec AcMul(AcVec a,float s){return AcV(a.x*s,a.y*s,a.z*s);}
static float AcDot(AcVec a,AcVec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static AcVec AcCross(AcVec a,AcVec b){return AcV(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);}
static float AcLen(AcVec a){return sqrtf(AcDot(a,a));}
static AcVec AcCol(const float* m,int c){return AcV(m[c*4],m[c*4+1],m[c*4+2]);}
static AcVec AcPoint(const float* m,AcVec v){return AcAdd(AcCol(m,3),AcAdd(AcMul(AcCol(m,0),v.x),AcAdd(AcMul(AcCol(m,1),v.y),AcMul(AcCol(m,2),v.z))));}
static AcVec AcRotate(AcVec v,AcVec axis,float angle){
 float c=cosf(angle),s=sinf(angle);
 return AcAdd(AcAdd(AcMul(v,c),AcMul(AcCross(axis,v),s)),AcMul(axis,AcDot(axis,v)*(1-c)));
}
static bool AcInverseVector(const float* p,AcVec w,AcVec* out){
 AcVec a=AcCol(p,0),b=AcCol(p,1),c=AcCol(p,2);
 float det=AcDot(a,AcCross(b,c));if(!isfinite(det)||fabsf(det)<0.000001f)return false;
 *out=AcV(AcDot(w,AcCross(b,c))/det,AcDot(w,AcCross(c,a))/det,AcDot(w,AcCross(a,b))/det);return true;
}
struct AcAim {float delta,pitchBefore,pitchAfter,forwardRange;AcVec angles;};
static bool AcSolve(const float* parent,const float* weapon,const float* muzzle,
                    AcVec target,AcVec scale,float weight,AcAim* out){
 for(int i=0;i<16;++i)if(!isfinite(parent[i])||!isfinite(weapon[i])||!isfinite(muzzle[i]))return false;
 if(!isfinite(target.x)||!isfinite(target.y)||!isfinite(target.z)||weight<0||weight>1)return false;
 if(fabsf(scale.x)<.0001f||fabsf(scale.y)<.0001f||fabsf(scale.z)<.0001f)return false;
 AcVec d=AcMul(AcCol(muzzle,2),-1);float dl=AcLen(d);if(dl<.0001f)return false;d=AcMul(d,1/dl);
 float h=sqrtf(d.x*d.x+d.z*d.z);if(h<.05f)return false;
 AcVec horizontal=AcV(d.x/h,0,d.z/h),axis=AcV(-d.z/h,0,d.x/h);
 AcVec pivot=AcCol(weapon,3),m=AcSub(AcCol(muzzle,3),pivot),t=AcSub(target,pivot);
 float mf=AcDot(m,horizontal),tf=AcDot(t,horizontal),range=AcDot(AcSub(target,AcCol(muzzle,3)),horizontal);
 float len=sqrtf(tf*tf+t.y*t.y);if(range<25||len<25)return false;
 float alpha=atan2f(d.y,h),offset=h*m.y-d.y*mf,ratio=offset/len;
 if(fabsf(ratio)>=.999f)return false;
 float desired=atan2f(t.y,tf)-asinf(ratio),delta=desired-alpha;
 // Reject impossible/rearward or extreme corrections instead of altering movement.
 if(!isfinite(delta)||fabsf(delta)>1.04719755f||fabsf(desired)>1.22173048f)return false;
 delta*=weight;
 float rot[9];const float scales[3]={scale.x,scale.y,scale.z};
 for(int c=0;c<3;++c){
  AcVec local;
  if(!AcInverseVector(parent,AcRotate(AcCol(weapon,c),axis,delta),&local))return false;
  local=AcMul(local,1/scales[c]);rot[c]=local.x;rot[3+c]=local.y;rot[6+c]=local.z;
 }
 for(int i=0;i<3;++i)for(int j=0;j<3;++j){
  float sum=0;for(int k=0;k<3;++k)sum+=rot[k*3+i]*rot[k*3+j];
  if(fabsf(sum-(i==j?1.0f:0.0f))>.002f)return false;
 }
 // Native Ry(y)*Rx(x)*Rz(z), including the singular representation.
 float sx=-rot[5];if(sx>1)sx=1;if(sx<-1)sx=-1;
 AcVec angles=AcV(asinf(sx),0,0);
 if(fabsf(cosf(angles.x))>.0002f){angles.y=atan2f(rot[2],rot[8]);angles.z=atan2f(rot[3],rot[4]);}
 else {angles.y=atan2f(-rot[6],rot[0]);angles.z=0;}
 out->angles=angles;out->delta=delta;out->pitchBefore=alpha;out->pitchAfter=alpha+delta;out->forwardRange=range;
 return true;
}
