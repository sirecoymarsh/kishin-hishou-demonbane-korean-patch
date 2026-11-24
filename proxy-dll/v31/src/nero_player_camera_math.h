#pragma once
#include <math.h>
#include <string.h>
struct NcVec { float x,y,z; };
static NcVec NcAdd(NcVec a,NcVec b){NcVec r={a.x+b.x,a.y+b.y,a.z+b.z};return r;}
static NcVec NcSub(NcVec a,NcVec b){NcVec r={a.x-b.x,a.y-b.y,a.z-b.z};return r;}
static NcVec NcMul(NcVec a,float s){NcVec r={a.x*s,a.y*s,a.z*s};return r;}
static float NcDot(NcVec a,NcVec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static NcVec NcCross(NcVec a,NcVec b){NcVec r={a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};return r;}
static float NcLength(NcVec a){return sqrtf(NcDot(a,a));}
static NcVec NcUnit(NcVec a){float s=NcLength(a);return s>0.0001f?NcMul(a,1.0f/s):NcVec{1,0,0};}
static bool NcFinite(float x){return x==x && x>-1000000.0f && x<1000000.0f;}
static bool NcValid(NcVec a){return NcFinite(a.x)&&NcFinite(a.y)&&NcFinite(a.z);}
static bool NcDefinitionFamily(const char* def,const char* family)
{
 if(!def)return false;
 char p[512];size_t i=0;
 for(;i<sizeof(p)-1 && def[i];++i){char c=def[i];p[i]=c=='\\'?'/':(c>='A'&&c<='Z'?char(c+'a'-'A'):c);}
 if(i==sizeof(p)-1)return false;
 p[i]=0;char part[32];
 // The only callers supply an exact two-letter robot directory.
 if(!family || strlen(family)!=2)return false;
 memcpy(part,"data/character/",15);part[15]=family[0];part[16]=family[1];part[17]='/';part[18]=0;
 return strstr(p,part)!=0;
}
static bool NcScope(bool balance,int source,int side,float mode,const char* def)
{return balance && source>=58 && source<1000 && side==0 && (mode==0.0f || mode==1.0f || mode==2.0f) && NcDefinitionFamily(def,"qm");}
struct NcPose { NcVec eye,right,up,forward,center,axis; float range; float matrix[16]; };
static void NcAtRange(NcVec center,NcVec axis,float range,NcPose* p)
{
 const NcVec worldUp={0,1,0};
 NcVec normal={-axis.z,0,axis.x};
 p->center=center;p->axis=axis;p->range=range;
 p->eye=NcAdd(center,NcAdd(NcMul(normal,range),NcMul(worldUp,range*0.28f)));
 p->forward=NcUnit(NcSub(center,p->eye));p->right=NcUnit(NcCross(worldUp,p->forward));p->up=NcCross(p->forward,p->right);
 const float m[16]={p->right.x,p->right.y,p->right.z,0,p->up.x,p->up.y,p->up.z,0,p->forward.x,p->forward.y,p->forward.z,0,p->eye.x,p->eye.y,p->eye.z,1};
 memcpy(p->matrix,m,sizeof m);
}
// R6 fits the union of both bodies rather than the midpoint of their origins.
// Framing budgets express a composition preference; native projection is unchanged.
static float NcFitRatio(const NcPose& p,NcVec center,NcVec half,float focal,NcVec side=NcVec{1,0,0},float budget=1.0f)
{
 float maxRatio=0;
 for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)for(int z=-1;z<=1;z+=2){
  const NcVec front={-side.z,0,side.x};
  NcVec v=NcAdd(center,NcAdd(NcMul(side,half.x*x),NcVec{front.x*half.z*z,half.y*y,front.z*half.z*z}));v=NcSub(v,p.eye);
  float depth=NcDot(v,p.forward);if(depth<=32.0f)return 4.0f;
  float a=fabsf(NcDot(v,p.right))*focal/(440.0f*depth*0.94f*budget);
  float b=fabsf(NcDot(v,p.up))*focal/(330.0f*depth*0.84f*budget);
  if(a>maxRatio)maxRatio=a;if(b>maxRatio)maxRatio=b;
 }
 return maxRatio;
}
static float NcRequiredRange(const NcPose& basis,NcVec center,NcVec half,float focal,NcVec side=NcVec{1,0,0},float budget=1.0f)
{
 const NcVec normal={-basis.axis.z,0.28f,basis.axis.x};
 const float depthPerRange=-NcDot(normal,basis.forward);
 if(depthPerRange<=0.001f)return 20001.0f;
 float required=0.0f;
 for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)for(int z=-1;z<=1;z+=2){
  const NcVec front={-side.z,0,side.x};
  NcVec v=NcAdd(center,NcAdd(NcMul(side,half.x*x),NcVec{front.x*half.z*z,half.y*y,front.z*half.z*z}));v=NcSub(v,basis.center);
  float depth=fabsf(NcDot(v,basis.right))*focal/(440.0f*0.94f*budget);
  float vertical=fabsf(NcDot(v,basis.up))*focal/(330.0f*0.84f*budget);
  if(vertical>depth)depth=vertical;if(depth<33.0f)depth=33.0f;
  float distance=(depth-NcDot(v,basis.forward))/depthPerRange;
  if(distance>required)required=distance;
 }
 return required;
}
struct NcEdges {float left,right,bottom,top,minDepth;};
static void NcAddBodyEdges(const NcPose& basis,NcVec body,NcVec half,NcVec side,float focal,NcEdges* e)
{
 const float kx=440.0f*0.94f/focal,ky=330.0f*0.84f/focal;
 const NcVec front={-side.z,0,side.x};
 for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)for(int z=-1;z<=1;z+=2){
  NcVec v=NcAdd(body,NcAdd(NcMul(side,half.x*x),NcVec{front.x*half.z*z,half.y*y,front.z*half.z*z}));v=NcSub(v,basis.center);
  float dx=NcDot(v,basis.right),dy=NcDot(v,basis.up),depth=NcDot(v,basis.forward);
  if(dx-kx*depth>e->left)e->left=dx-kx*depth;
  if(dx+kx*depth<e->right)e->right=dx+kx*depth;
  if(dy-ky*depth>e->bottom)e->bottom=dy-ky*depth;
  if(dy+ky*depth<e->top)e->top=dy+ky*depth;
  if(depth<e->minDepth)e->minDepth=depth;
 }
}
static bool NcCompose(NcVec self,NcVec other,NcVec otherHalf,float focal,
                      bool previous,NcVec oldAxis,float oldRange,float dt,NcPose* out,NcVec selfRight=NcVec{1,0,0})
{
 if(!NcValid(self)||!NcValid(other)||!NcValid(otherHalf)||!NcFinite(focal)||focal<64||focal>4096)return false;
 if(!NcValid(selfRight)||!NcFinite(dt)||dt<0)return false;selfRight.y=0;if(NcLength(selfRight)<0.1f)return false;selfRight=NcUnit(selfRight);
 if(previous && (!NcValid(oldAxis)||!NcFinite(oldRange)||oldRange<=0))return false;
 if(dt>0.25f)dt=0.25f;
 NcVec delta=NcSub(other,self);delta.y=0;float separation=NcLength(delta);
 NcVec axis=separation>80?NcUnit(delta):(previous?oldAxis:NcVec{0,0,-1});
 // The combat line is unoriented: keep the same spectator side when actors cross.
 if(previous){
  if(NcDot(axis,oldAxis)<0)axis=NcMul(axis,-1);
  float blend=dt*3.0f;if(blend>1)blend=1;if(blend<0)blend=0;
  axis=NcUnit(NcAdd(NcMul(oldAxis,1-blend),NcMul(axis,blend)));
 }
 const NcVec midpoint=NcMul(NcAdd(self,other),0.5f),ownHalf={200,250,280};
 NcPose basis={};NcAtRange(midpoint,axis,1100.0f,&basis);
 NcEdges e={-1000000.0f,1000000.0f,-1000000.0f,1000000.0f,1000000.0f};
 NcAddBodyEdges(basis,self,ownHalf,selfRight,focal,&e);
 NcAddBodyEdges(basis,other,otherHalf,NcVec{1,0,0},focal,&e);
 const float kx=440.0f*0.94f/focal,ky=330.0f*0.84f/focal;
 const float depthPerRange=sqrtf(1.0f+0.28f*0.28f);
 float depth=(e.left-e.right)/(2.0f*kx),heightDepth=(e.bottom-e.top)/(2.0f*ky);
 if(heightDepth>depth)depth=heightDepth;
 if(33.0f-e.minDepth>depth)depth=33.0f-e.minDepth;
 float target=depth/depthPerRange;if(target<460.0f)target=460.0f;target+=2.0f;
 // Shift only the view center in its own plane; actor/aim positions never change.
 NcVec center=NcAdd(midpoint,NcAdd(NcMul(basis.right,(e.left+e.right)*0.5f),NcMul(basis.up,(e.bottom+e.top)*0.5f)));
 NcAtRange(center,axis,target,&basis);
 // A small outer framing band allows a smooth retreat before a body reaches
 // the edge. Sudden knockback is clamped only as far back as the outer fit needs.
 const float outer=480.0f/440.0f;
 float guard=NcRequiredRange(basis,self,ownHalf,focal,selfRight,outer);
 float otherGuard=NcRequiredRange(basis,other,otherHalf,focal,NcVec{1,0,0},outer);
 if(otherGuard>guard)guard=otherGuard;if(guard<460.0f)guard=460.0f;guard+=2.0f;
 float range=target;
 if(previous){
  float speed=target>oldRange?8.0f:5.0f;
  float blend=1.0f-expf(-speed*dt);
  range=oldRange+(target-oldRange)*blend;
  if(range<guard)range=guard;
 }
 if(!NcFinite(range)||range>20000.0f)return false;
 NcPose p={};NcAtRange(center,axis,range,&p);
 if(NcFitRatio(p,self,ownHalf,focal,selfRight,outer)>1.0001f||NcFitRatio(p,other,otherHalf,focal,NcVec{1,0,0},outer)>1.0001f)return false;
 *out=p;return true;
}
