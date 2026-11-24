// Only the two managed helper applications use this path. The game and native helpers keep their launch behavior.
#include <wbemidl.h>
#include <oleauto.h>
#pragma comment(lib,"wbemuuid.lib")
#pragma comment(lib,"oleaut32.lib")
template<class T> struct CleanWmiPtr {
    T* p;
    CleanWmiPtr():p(NULL) {}
    ~CleanWmiPtr() { if(p)p->Release(); }
};
struct CleanWmiBstr {
    BSTR p;
    explicit CleanWmiBstr(const wchar_t* s):p(SysAllocString(s)) {}
    ~CleanWmiBstr() { SysFreeString(p); }
};
struct CleanWmiVariant {
    VARIANT v;
    CleanWmiVariant() { VariantInit(&v); }
    ~CleanWmiVariant() { VariantClear(&v); }
};
static HRESULT CleanWmiPutText(IWbemClassObject* obj,const wchar_t* key,const wchar_t* text)
{
    CleanWmiVariant value;value.v.vt=VT_BSTR;value.v.bstrVal=SysAllocString(text);
    return value.v.bstrVal ? obj->Put(key,0,&value.v,0) : E_OUTOFMEMORY;
}
static HRESULT CleanWmiPutInt(IWbemClassObject* obj,const wchar_t* key,LONG number)
{
    CleanWmiVariant value;value.v.vt=VT_I4;value.v.lVal=number;return obj->Put(key,0,&value.v,0);
}
static HRESULT CleanStartManagedHelper(const wchar_t* command,const wchar_t* directory,DWORD* pid,DWORD* status)
{
    *pid=0;*status=0xffffffff;
    const HRESULT initialized=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(FAILED(initialized) && initialized!=RPC_E_CHANGED_MODE)return initialized;
    const HRESULT result=[&]() -> HRESULT {
        CleanWmiPtr<IWbemLocator> locator;CleanWmiPtr<IWbemServices> services;
        CleanWmiPtr<IWbemClassObject> process,signature,input,startupClass,startup,output;
        CleanWmiBstr ns(L"ROOT\\CIMV2"),processClass(L"Win32_Process"),method(L"Create"),startupName(L"Win32_ProcessStartup");
        if(!ns.p || !processClass.p || !method.p || !startupName.p)return E_OUTOFMEMORY;
        HRESULT hr=CoCreateInstance(CLSID_WbemLocator,NULL,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&locator.p);
        if(FAILED(hr))return hr;
        hr=locator.p->ConnectServer(ns.p,NULL,NULL,NULL,0,NULL,NULL,&services.p);if(FAILED(hr))return hr;
        hr=CoSetProxyBlanket(services.p,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,NULL,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,NULL,EOAC_NONE);if(FAILED(hr))return hr;
        hr=services.p->GetObject(processClass.p,0,NULL,&process.p,NULL);if(FAILED(hr))return hr;
        hr=process.p->GetMethod(method.p,0,&signature.p,NULL);if(FAILED(hr))return hr;
        hr=signature.p->SpawnInstance(0,&input.p);if(FAILED(hr))return hr;
        hr=CleanWmiPutText(input.p,L"CommandLine",command);if(FAILED(hr))return hr;
        hr=CleanWmiPutText(input.p,L"CurrentDirectory",directory);if(FAILED(hr))return hr;
        hr=services.p->GetObject(startupName.p,0,NULL,&startupClass.p,NULL);if(FAILED(hr))return hr;
        hr=startupClass.p->SpawnInstance(0,&startup.p);if(FAILED(hr))return hr;
        hr=CleanWmiPutInt(startup.p,L"ShowWindow",SW_HIDE);if(FAILED(hr))return hr;
        CleanWmiVariant startupValue;startupValue.v.vt=VT_UNKNOWN;startupValue.v.punkVal=startup.p;startup.p->AddRef();
        hr=input.p->Put(L"ProcessStartupInformation",0,&startupValue.v,0);if(FAILED(hr))return hr;
        hr=services.p->ExecMethod(processClass.p,method.p,0,NULL,input.p,&output.p,NULL);if(FAILED(hr))return hr;
        if(!output.p)return E_UNEXPECTED;
        CleanWmiVariant returned,identifier;
        hr=output.p->Get(L"ReturnValue",0,&returned.v,NULL,NULL);if(FAILED(hr))return hr;
        if(returned.v.vt!=VT_I4 && returned.v.vt!=VT_UI4)return E_UNEXPECTED;
        *status=returned.v.ulVal;if(*status)return E_FAIL;
        hr=output.p->Get(L"ProcessId",0,&identifier.v,NULL,NULL);if(FAILED(hr))return hr;
        if(identifier.v.vt!=VT_I4 && identifier.v.vt!=VT_UI4)return E_UNEXPECTED;
        *pid=identifier.v.ulVal;return *pid ? S_OK : E_UNEXPECTED;
    }();
    if(SUCCEEDED(initialized))CoUninitialize();
    return result;
}
