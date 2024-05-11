#include "ProcessCreatedEventDispatcher.h"

# pragma comment(lib, "wbemuuid.lib")

#include <iostream>
#include <functional>
#include <string>
#include <vector>

#define _WIN32_DCOM
#include <Windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <wrl.h>

using namespace std;
using namespace Microsoft::WRL;

ProcessCreatedEventDispatcher::ProcessCreatedEventDispatcher() {
    HRESULT hres;
    // Step 1: --------------------------------------------------
    // Initialize COM. ------------------------------------------

    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres)) {
        cout << "Failed to initialize COM library. Error code = 0x" << hex << hres << endl;
        return; // Program has failed.
    }

    // Step 2: --------------------------------------------------
    // Set general COM security levels --------------------------
    // Note: If you are using Windows 2000, you need to specify -
    // the default authentication credentials for a user by using
    // a SOLE_AUTHENTICATION_LIST structure in the pAuthList ----
    // parameter of CoInitializeSecurity ------------------------

    hres = CoInitializeSecurity(NULL,
        -1, // COM negotiates service
        NULL, // Authentication services
        NULL, // Reserved
        RPC_C_AUTHN_LEVEL_DEFAULT, // Default authentication 
        RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
        NULL, // Authentication info
        EOAC_NONE, // Additional capabilities 
        NULL // Reserved
    );


    if (FAILED(hres)) {
        cout << "Failed to initialize security. Error code = 0x" << hex << hres << endl;
        CoUninitialize();
        return; // Program has failed.
    }

    // Step 3: ---------------------------------------------------
    // Obtain the initial locator to WMI -------------------------
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)pLoc.GetAddressOf());

    if (FAILED(hres)) {
        cout << "Failed to create IWbemLocator object. " << "Err code = 0x" << hex << hres << endl;
        CoUninitialize();
        return; // Program has failed.
    }

    // Step 4: ---------------------------------------------------
    // Connect to WMI through the IWbemLocator::ConnectServer method

    // Connect to the local root\cimv2 namespace
    // and obtain pointer pSvc to make IWbemServices calls.
    hres = pLoc->ConnectServer(_bstr_t(L"root\\CIMV2"),
        NULL,
        NULL,
        0,
        NULL,
        0,
        0,
        &pSvc
    );

    if (FAILED(hres)) {
        cout << "Could not connect. Error code = 0x" << hex << hres << endl;
        pLoc->Release();
        CoUninitialize();
        return; // Program has failed.
    }

    cout << "Connected to root\\CIMV2 WMI namespace" << endl;


    // Step 5: --------------------------------------------------
    // Set security levels on the proxy -------------------------

    hres = CoSetProxyBlanket(pSvc.Get(), // Indicates the proxy to set
        RPC_C_AUTHN_WINNT, // RPC_C_AUTHN_xxx 
        RPC_C_AUTHZ_NONE, // RPC_C_AUTHZ_xxx 
        NULL, // Server principal name 
        RPC_C_AUTHN_LEVEL_CALL, // RPC_C_AUTHN_LEVEL_xxx 
        RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
        NULL, // client identity
        EOAC_NONE // proxy capabilities 
    );

    if (FAILED(hres)) {
        cout << "Could not set proxy blanket. Error code = 0x" << hex << hres << endl;
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return; // Program has failed.
    }

    // Step 6: -------------------------------------------------
    // Receive event notifications -----------------------------

    // Use an unsecured apartment for security
    hres = CoCreateInstance(CLSID_UnsecuredApartment, NULL, CLSCTX_LOCAL_SERVER, IID_IUnsecuredApartment, (void**)&pUnsecApp);

    this->ProcessCreatedEventDispatcher::AddRef();

    pUnsecApp->CreateObjectStub(this, &pStubUnk);

    pStubUnk->QueryInterface(IID_IWbemObjectSink, &pStubSink);

    _bstr_t WQL = L"Select * From __InstanceCreationEvent Within 1 "
        L"Where TargetInstance ISA 'Win32_Process' ";

    // The ExecNotificationQueryAsync method will call
    // The EventQuery::Indicate method when an event occurs
    hres = pSvc->ExecNotificationQueryAsync(_bstr_t("WQL"), WQL, WBEM_FLAG_SEND_STATUS, NULL, pStubSink.Get());

    // Check for errors.
    if (FAILED(hres)) {
        printf("ExecNotificationQueryAsync failed with = 0x%X\n", hres);
        pSvc->Release();
        pLoc->Release();
        pUnsecApp->Release();
        pStubUnk->Release();
        this->ProcessCreatedEventDispatcher::Release();
        pStubSink->Release();
        CoUninitialize();
        return;
    }
}

ProcessCreatedEventDispatcher::~ProcessCreatedEventDispatcher() {
    auto Result = pSvc->CancelAsyncCall(pStubSink.Get());
    pSvc->Release();
    pLoc->Release();
    pUnsecApp->Release();
    pStubUnk->Release();
    this->ProcessCreatedEventDispatcher::Release();
    pStubSink->Release();
    CoUninitialize();
}

ULONG ProcessCreatedEventDispatcher::AddRef() {
    return InterlockedIncrement(&m_lRef);
}

ULONG ProcessCreatedEventDispatcher::Release() {
    LONG lRef = InterlockedDecrement(&m_lRef);
    if (lRef == 0)
        delete this;
    return lRef;
}

HRESULT ProcessCreatedEventDispatcher::QueryInterface(REFIID riid, void** ppv) {
    if (riid == IID_IUnknown || riid == IID_IWbemObjectSink) {
        *ppv = (IWbemObjectSink*)this;
        AddRef();
        return WBEM_S_NO_ERROR;
    }
    else return E_NOINTERFACE;
}


HRESULT ProcessCreatedEventDispatcher::Indicate(long lObjectCount, IWbemClassObject** apObjArray) {
    HRESULT hr = S_OK;
    _variant_t vtProp;

    for (int i = 0; i < lObjectCount; i++) {
        hr = apObjArray[i]->Get(_bstr_t(L"TargetInstance"), 0, &vtProp, 0, 0);
        if (!FAILED(hr)) {
            ComPtr<IUnknown> pUnk = static_cast<IUnknown*>(vtProp);
            IWbemClassObject* pObj = nullptr;
            hr = pUnk->QueryInterface(IID_IWbemClassObject, reinterpret_cast<void**>(&pObj));
            if (SUCCEEDED(hr)) {
                _variant_t cn, pid, name;

                hr = pObj->Get(L"Handle", 0, &cn, NULL, NULL);
                hr = pObj->Get(L"ProcessId", 0, &pid, NULL, NULL);
                hr = pObj->Get(L"Name", 0, &name, NULL, NULL);

                if (SUCCEEDED(hr)) {
                    if ((cn.vt == VT_NULL) || (cn.vt == VT_EMPTY))
                        std::cout << "Handle : " << ((cn.vt == VT_NULL) ? "NULL" : "EMPTY") << endl;
                    else if ((cn.vt & VT_ARRAY))
                        std::cout << "Handle : " << "Array types not supported (yet)" << endl;
                    else {
                        std::wstring WideProcessHandle = std::wstring(cn.bstrVal);
                        std::wstring WideProcessId = std::to_wstring(pid.lVal);
                        std::wstring WideProcessName = std::wstring(name.bstrVal);
                        
                        // Pass the process ID, process name, and handle to the listener
                        for (auto& NewProcessCreatedListener : NewProcessCreatedListeners) {
                            NewProcessCreatedListener(WideProcessName, WideProcessId);
                        }
                    }
                }
                VariantClear(&cn);
                VariantClear(&pid);
                VariantClear(&name);
            }
            pObj->Release();
        }
        VariantClear(&vtProp);
    }

    return WBEM_S_NO_ERROR;
}

HRESULT ProcessCreatedEventDispatcher::SetStatus(
    /* [in] */ LONG lFlags,
    /* [in] */ HRESULT hResult,
    /* [in] */ BSTR strParam,
    /* [in] */ IWbemClassObject __RPC_FAR* pObjParam
) {
    if (lFlags == WBEM_STATUS_COMPLETE) {
        printf("Call complete. hResult = 0x%X\n", hResult);
    }
    else if (lFlags == WBEM_STATUS_PROGRESS) {
        printf("Call in progress.\n");
    }

    return WBEM_S_NO_ERROR;
} // end of EventSin