// Balanced DUEL, private NG_ actor and NGP_ projectile only.
// Native attack damage, guide timing, speed, collision volume and lifespan are unchanged.
typedef void (__thiscall* NgSightFn)(BYTE*);
typedef float* (__thiscall* NgWorldFn)(BYTE*);
typedef BYTE* (__thiscall* NgTargetFn)(BYTE*,BYTE*);
typedef BYTE* (__thiscall* NgFindFn)(BYTE*,const char*);
typedef void (__cdecl* NgAnglesFn)(float*,const float*);
typedef void (__thiscall* NgNormalizeFn)(float*);
typedef void (__cdecl* NgDirtyFn)(BYTE*);
static BYTE* ngModule;
static NgSightFn ngNativeSight;
static const char* NgDefinition(BYTE* node)
{
    BYTE* d=node ? *(BYTE**)(node+0x16c) : NULL;
    return d ? *(const char**)(d+0x34) : NULL;
}
static bool NgPrefix(const char* name,const char* prefix)
{
    if(!name || strnlen_s(name,512)>=512) return false;
    const char* b=name;
    for(const char* p=name;*p;++p) if(*p=='/' || *p=='\\') b=p+1;
    return strncmp(b,prefix,strlen(prefix))==0;
}
static bool NgApply(BYTE* projectile)
{
    if(!duelBalance || balScene<58 || balScene>=1000 || GetModuleHandleW(battleModule)!=(HMODULE)ngModule) return false;
    __try {
        const char* def=NgDefinition(projectile);
        if(!NgPrefix(def,"NGP_") || strstr(def,"MAKEN")) return false;
        BYTE* owner=*(BYTE**)(projectile+0x6f8);
        if(!owner || !NgPrefix(NgDefinition(owner),"NG_")) return false;
        BYTE* manager=*(BYTE**)(ngModule+0x11e3b0);
        BYTE* target=manager ? ((NgTargetFn)(ngModule+0x39d60))(manager,owner) : NULL;
        if(!target || *(int*)(owner+0x558)<=0 || *(int*)(target+0x558)<=0) return false;
        BYTE* aim=((NgFindFn)(ngModule+0x15a00))(target,"lockOn");
        if(!aim) return false;
        float origin[16],dest[16];
        memcpy(origin,((NgWorldFn)(ngModule+0x15930))(projectile),sizeof origin);
        memcpy(dest,((NgWorldFn)(ngModule+0x15930))(aim),sizeof dest);
        // Native sight uses origin-target (vector subtraction 0x2760), local -Z forward.
        float delta[4]={origin[12]-dest[12],origin[13]-dest[13],origin[14]-dest[14],0};
        float angles[4]={};
        if(!_finite(delta[0]) || !_finite(delta[1]) || !_finite(delta[2]) ||
           delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]<1.0f) return false;
        ((NgAnglesFn)(ngModule+0xfa50))(angles,delta);
        ((NgNormalizeFn)(ngModule+0xfbc0))(angles);
        if(!_finite(angles[0]) || !_finite(angles[1])) return false;
        projectile[0x94]&=0xfc;
        ((NgDirtyFn)(ngModule+0x15540))(projectile);
        memcpy(projectile+0x14,angles,sizeof angles);
        Log("NERO_GROUND_AIM node=%p owner=%p target=%p def=%s origin=%.3f,%.3f,%.3f aim=%.3f,%.3f,%.3f angles=%.6f,%.6f",projectile,owner,target,def,origin[12],origin[13],origin[14],dest[12],dest[13],dest[14],angles[0],angles[1]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { Log("NERO_GROUND_AIM exception; native fallback");return false; }
}
static void __fastcall NgSight(BYTE* projectile,void*)
{
    if(!NgApply(projectile)) ngNativeSight(projectile);
}
static void InstallNeroGroundAim(HMODULE module)
{
    BYTE* base=(BYTE*)module;
    if(!base) return;
    BYTE* call=base+0x38dc4;
    if(*call!=0xe8) { Log("NERO_GROUND_AIM install rejected opcode");return; }
    BYTE* target=call+5+*(LONG*)(call+1);
    if(target==(BYTE*)&NgSight) return;
    if(target!=base+0x314a0) { Log("NERO_GROUND_AIM install rejected target");return; }
    // Version identity: original function prologue and sole observed call site.
    const BYTE prologue[]={0x81,0xec,0x30,0x01,0x00,0x00,0x53,0x8b,0xd9};
    if(memcmp(base+0x314a0,prologue,sizeof prologue)) { Log("NERO_GROUND_AIM install rejected function");return; }
    DWORD old;
    if(!VirtualProtect(call+1,4,PAGE_EXECUTE_READWRITE,&old)) return;
    ngModule=base;ngNativeSight=(NgSightFn)(base+0x314a0);
    *(LONG*)(call+1)=(LONG)((BYTE*)&NgSight-(call+5));
    DWORD ignored;VirtualProtect(call+1,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);
    Log("NERO_GROUND_AIM installed private-only call=38dc4 native=314a0");
}
