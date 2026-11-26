// Reconnects a Bluetooth audio device through the KS filters of its audio endpoints. See bt_reconnect.h.
#include "bt_reconnect.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>  // also declares IKsControl and KSPROPERTY

#include <set>

namespace {

// Defined here so that neither initguid.h nor an import library has to provide them.
const GUID kClsidEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
const GUID kIidEnumerator = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
const GUID kIidTopology = { 0x2a07407e, 0x6497, 0x4a18, { 0x97, 0x87, 0x32, 0xf7, 0x9b, 0xd0, 0xd9, 0x8f } };
const GUID kIidKsControl = { 0x28f54685, 0x06fd, 0x11d2, { 0xb2, 0x7a, 0x00, 0xa0, 0xc9, 0x22, 0x31, 0x96 } };
// KSPROPSETID_BtAudio; property 0 is KSPROPERTY_ONESHOT_RECONNECT (1 would disconnect).
const GUID kBtAudio = { 0x7fa06c40, 0xb8f6, 0x4c7e, { 0x85, 0x56, 0xe8, 0xc3, 0x3a, 0x12, 0xe5, 0x4d } };
constexpr ULONG kOneShotReconnect = 0;
// PKEY_Device_ContainerId: an audio endpoint carries the container id of its Bluetooth device.
const PROPERTYKEY kContainerId = { { 0x8c7ed206, 0x3f8a, 0x4827, { 0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c } }, 2 };

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
};

bool InContainer(IMMDevice* endpoint, const GUID& container) {
    Com<IPropertyStore> props;
    if (FAILED(endpoint->OpenPropertyStore(STGM_READ, &props)) || !props.p) return false;
    PROPVARIANT v;
    PropVariantInit(&v);
    bool same = SUCCEEDED(props->GetValue(kContainerId, &v)) && v.vt == VT_CLSID && v.puuid && IsEqualGUID(*v.puuid, container);
    PropVariantClear(&v);
    return same;
}

// The device id of the KS filter behind an endpoint, or empty.
std::wstring FilterOf(IMMDevice* endpoint) {
    Com<IDeviceTopology> topology;
    Com<IConnector> connector;
    LPWSTR id = nullptr;
    std::wstring filter;
    if (SUCCEEDED(endpoint->Activate(kIidTopology, CLSCTX_ALL, nullptr, (void**)&topology)) && topology.p &&
        SUCCEEDED(topology->GetConnector(0, &connector)) && connector.p &&
        SUCCEEDED(connector->GetDeviceIdConnectedTo(&id)) && id)
        filter = id;
    CoTaskMemFree(id);
    return filter;
}

int ReconnectContainer(const GUID& container) {
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDeviceCollection> endpoints;
    UINT count = 0;
    // Unplugged only: a connected (active) endpoint is left alone.
    if (FAILED(CoCreateInstance(kClsidEnumerator, nullptr, CLSCTX_ALL, kIidEnumerator, (void**)&enumerator)) || !enumerator.p ||
        FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_UNPLUGGED, &endpoints)) || !endpoints.p ||
        FAILED(endpoints->GetCount(&count)))
        return 0;
    int sent = 0;
    std::set<std::wstring> asked;  // one request per filter, however many endpoints it has
    for (UINT i = 0; i < count; i++) {
        Com<IMMDevice> endpoint;
        if (FAILED(endpoints->Item(i, &endpoint)) || !endpoint.p || !InContainer(endpoint.p, container)) continue;
        std::wstring filter = FilterOf(endpoint.p);
        if (filter.empty() || !asked.insert(filter).second) continue;
        Com<IMMDevice> device;
        Com<IKsControl> ks;
        if (FAILED(enumerator->GetDevice(filter.c_str(), &device)) || !device.p ||
            FAILED(device->Activate(kIidKsControl, CLSCTX_ALL, nullptr, (void**)&ks)) || !ks.p)
            continue;
        KSPROPERTY prop{};
        prop.Set = kBtAudio;
        prop.Id = kOneShotReconnect;
        prop.Flags = KSPROPERTY_TYPE_GET;
        ULONG returned = 0;
        if (SUCCEEDED(ks->KsProperty(&prop, sizeof(prop), nullptr, 0, &returned))) sent++;
    }
    return sent;
}

}  // namespace

int bt_reconnect::Reconnect(const std::wstring& key) {
    GUID container;
    if (key.rfind(L"bt:", 0) != 0 || FAILED(IIDFromString(key.c_str() + 3, &container))) return 0;
    // The poll thread may already be in a WinRT apartment (RoInitialize in the drivers): use that one then.
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return 0;
    int sent = ReconnectContainer(container);  // every interface is released before CoUninitialize
    if (SUCCEEDED(init)) CoUninitialize();
    return sent;
}
