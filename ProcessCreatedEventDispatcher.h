#pragma once
#include <functional>
#include <vector>

#include <Wbemidl.h>
#include <wrl.h>
#include <string>
using namespace Microsoft::WRL;

class ProcessCreatedEventDispatcher : public IWbemObjectSink {

public:
    ProcessCreatedEventDispatcher();
    ~ProcessCreatedEventDispatcher();

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    HRESULT STDMETHODCALLTYPE Indicate(LONG lObjectCount, IWbemClassObject __RPC_FAR* __RPC_FAR* apObjArray) override;
    HRESULT STDMETHODCALLTYPE SetStatus(LONG lFlags, HRESULT hResult, BSTR strParam, IWbemClassObject __RPC_FAR* pObjParam) override;

    using NewProcessCreatedListener = void(std::wstring processName, std::wstring processId);

    std::vector<std::function<NewProcessCreatedListener>> NewProcessCreatedListeners{};
private:
    LONG m_lRef{};
    ComPtr<IWbemServices> pSvc{};
    ComPtr<IWbemLocator> pLoc{};
    ComPtr<IUnsecuredApartment> pUnsecApp{};
    ComPtr<IUnknown> pStubUnk{};
    ComPtr<IWbemObjectSink> pStubSink{};
};