#include "controls.h"

#include <endpointvolume.h>
#include <highlevelmonitorconfigurationapi.h>
#include <mmdeviceapi.h>
#include <physicalmonitorenumerationapi.h>
#include <wbemidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace op {

using Microsoft::WRL::ComPtr;

namespace {

struct BStr {
    BSTR value = nullptr;
    explicit BStr(const wchar_t* text) : value(SysAllocString(text)) {}
    ~BStr() { SysFreeString(value); }
    BStr(const BStr&) = delete;
    BStr& operator=(const BStr&) = delete;
    operator BSTR() const { return value; }
};

bool OpenEndpoint(EDataFlow flow, ComPtr<IAudioEndpointVolume>& slot) {
    if (slot) {
        float level = 0;
        if (SUCCEEDED(slot->GetMasterVolumeLevelScalar(&level))) return true;
        slot.Reset();
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return false;
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device))) return false;
    return SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(slot.ReleaseAndGetAddressOf())));
}

bool ReadScalar(IAudioEndpointVolume* endpoint, float& level, bool& muted) {
    float value = 0;
    BOOL mute = FALSE;
    if (!endpoint || FAILED(endpoint->GetMasterVolumeLevelScalar(&value))) return false;
    endpoint->GetMute(&mute);
    level = std::clamp(value, 0.0f, 1.0f);
    muted = mute == TRUE;
    return true;
}

bool WriteScalar(IAudioEndpointVolume* endpoint, float level) {
    if (!endpoint) return false;
    endpoint->SetMute(FALSE, nullptr);
    return SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(std::clamp(level, 0.0f, 1.0f), nullptr));
}

// External monitors usually answer the physical-monitor API. Laptop panels usually answer WMI.
int ReadPhysical(HMONITOR monitor) {
    DWORD count = 0;
    if (!monitor || !GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0 || count > 8) return -1;
    std::vector<PHYSICAL_MONITOR> monitors(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, monitors.data())) return -1;
    DWORD minimum = 0, current = 0, maximum = 0;
    int percent = -1;
    if (GetMonitorBrightness(monitors[0].hPhysicalMonitor, &minimum, &current, &maximum) && maximum > minimum) {
        percent = (int)std::lround((current - minimum) * 100.0 / (maximum - minimum));
        percent = std::clamp(percent, 0, 100);
    }
    DestroyPhysicalMonitors(count, monitors.data());
    return percent;
}

bool WritePhysical(HMONITOR monitor, int percent) {
    DWORD count = 0;
    if (!monitor || !GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0 || count > 8) return false;
    std::vector<PHYSICAL_MONITOR> monitors(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, monitors.data())) return false;
    DWORD minimum = 0, current = 0, maximum = 0;
    bool wrote = false;
    if (GetMonitorBrightness(monitors[0].hPhysicalMonitor, &minimum, &current, &maximum) && maximum > minimum) {
        const DWORD value = minimum + (maximum - minimum) * (DWORD)std::clamp(percent, 0, 100) / 100;
        wrote = SetMonitorBrightness(monitors[0].hPhysicalMonitor, value) == TRUE;
    }
    DestroyPhysicalMonitors(count, monitors.data());
    return wrote;
}

bool UseWmi(bool write, int& percent) {
    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&locator))))
        return false;
    ComPtr<IWbemServices> services;
    BStr ns(L"ROOT\\WMI");
    if (FAILED(locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services))) return false;
    CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    if (!write) {
        ComPtr<IEnumWbemClassObject> enumerator;
        BStr language(L"WQL");
        BStr query(L"SELECT * FROM WmiMonitorBrightness");
        if (FAILED(services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                       &enumerator)))
            return false;
        ComPtr<IWbemClassObject> object;
        ULONG got = 0;
        if (enumerator->Next(1500, 1, &object, &got) != S_OK || !object) return false;
        VARIANT current;
        VariantInit(&current);
        const HRESULT hr = object->Get(L"CurrentBrightness", 0, &current, nullptr, nullptr);
        if (SUCCEEDED(hr) && current.vt == VT_UI1) percent = current.bVal;
        else if (SUCCEEDED(hr) && current.vt == VT_I4) percent = current.lVal;
        else {
            VariantClear(&current);
            return false;
        }
        VariantClear(&current);
        percent = std::clamp(percent, 0, 100);
        return true;
    }

    ComPtr<IEnumWbemClassObject> enumerator;
    BStr language(L"WQL");
    BStr query(L"SELECT * FROM WmiMonitorBrightnessMethods");
    if (FAILED(services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                                   &enumerator)))
        return false;
    ComPtr<IWbemClassObject> object;
    ULONG got = 0;
    if (enumerator->Next(1500, 1, &object, &got) != S_OK || !object) return false;
    VARIANT path;
    VariantInit(&path);
    if (FAILED(object->Get(L"__PATH", 0, &path, nullptr, nullptr)) || path.vt != VT_BSTR) {
        VariantClear(&path);
        return false;
    }
    ComPtr<IWbemClassObject> klass, definition, input;
    BStr className(L"WmiMonitorBrightnessMethods");
    if (FAILED(services->GetObject(className, 0, nullptr, &klass, nullptr))) {
        VariantClear(&path);
        return false;
    }
    if (FAILED(klass->GetMethod(L"WmiSetBrightness", 0, &definition, nullptr)) ||
        FAILED(definition->SpawnInstance(0, &input))) {
        VariantClear(&path);
        return false;
    }
    VARIANT timeout;
    VariantInit(&timeout);
    timeout.vt = VT_I4;
    timeout.lVal = 0;
    VARIANT bright;
    VariantInit(&bright);
    bright.vt = VT_UI1;
    bright.bVal = (BYTE)std::clamp(percent, 0, 100);
    input->Put(L"Timeout", 0, &timeout, 0);
    input->Put(L"Brightness", 0, &bright, 0);
    BStr method(L"WmiSetBrightness");
    const HRESULT hr = services->ExecMethod(path.bstrVal, method, 0, nullptr, input.Get(), nullptr, nullptr);
    VariantClear(&path);
    return SUCCEEDED(hr);
}

}  // namespace

struct DeviceLevels::Impl {
    ComPtr<IAudioEndpointVolume> render;
    ComPtr<IAudioEndpointVolume> capture;
    ULONGLONG wmiTriedAt = 0;
    int wmiPercent = -1;
    bool brightDirty = false;
    HMONITOR brightMonitor = nullptr;
    int brightPercent = 0;
    ULONGLONG brightAppliedAt = 0;
};

DeviceLevels::DeviceLevels() : impl_(new Impl) {}

DeviceLevels::~DeviceLevels() { delete impl_; }

int* DeviceLevels::FindBrightness(HMONITOR monitor, bool create) {
    for (auto& entry : bright_) {
        if (entry.monitor == monitor) return &entry.percent;
    }
    if (!create) return nullptr;
    for (auto& entry : bright_) {
        if (!entry.monitor) {
            entry.monitor = monitor;
            entry.percent = -1;
            return &entry.percent;
        }
    }
    return nullptr;
}

const int* DeviceLevels::FindBrightness(HMONITOR monitor) const {
    for (const auto& entry : bright_) {
        if (entry.monitor == monitor) return &entry.percent;
    }
    return nullptr;
}

int DeviceLevels::Brightness(HMONITOR monitor) const {
    const int* value = FindBrightness(monitor);
    return value ? *value : -1;
}

bool DeviceLevels::Refresh(HMONITOR monitor) {
    const float volumeBefore = volume_;
    const float micBefore = mic_;
    const bool volumeMuteBefore = volumeMuted_;
    const bool micMuteBefore = micMuted_;
    const bool hadMic = hasMic_;
    const int brightBefore = Brightness(monitor);

    if (OpenEndpoint(eRender, impl_->render)) ReadScalar(impl_->render.Get(), volume_, volumeMuted_);
    hasMic_ = OpenEndpoint(eCapture, impl_->capture);
    if (hasMic_) ReadScalar(impl_->capture.Get(), mic_, micMuted_);

    if (!impl_->brightDirty) {
        int percent = ReadPhysical(monitor);
        if (percent < 0) {
            const ULONGLONG now = GetTickCount64();
            if (now - impl_->wmiTriedAt >= 8000) {
                impl_->wmiTriedAt = now;
                int wmi = -1;
                impl_->wmiPercent = UseWmi(false, wmi) ? wmi : -1;
            }
            percent = impl_->wmiPercent;
        }
        if (percent >= 0) {
            if (int* slot = FindBrightness(monitor, true)) *slot = percent;
        }
    }

    return volume_ != volumeBefore || mic_ != micBefore || volumeMuted_ != volumeMuteBefore ||
           micMuted_ != micMuteBefore || hasMic_ != hadMic || Brightness(monitor) != brightBefore;
}

void DeviceLevels::SetVolume(float unit) {
    volume_ = std::clamp(unit, 0.0f, 1.0f);
    volumeMuted_ = false;
    if (OpenEndpoint(eRender, impl_->render)) WriteScalar(impl_->render.Get(), volume_);
}

void DeviceLevels::SetMic(float unit) {
    mic_ = std::clamp(unit, 0.0f, 1.0f);
    micMuted_ = false;
    if (OpenEndpoint(eCapture, impl_->capture)) {
        hasMic_ = true;
        WriteScalar(impl_->capture.Get(), mic_);
    }
}

void DeviceLevels::SetBrightness(HMONITOR monitor, float unit) {
    const int percent = std::clamp((int)std::lround(std::clamp(unit, 0.0f, 1.0f) * 100.0f), 0, 100);
    if (int* slot = FindBrightness(monitor, true)) *slot = percent;
    const ULONGLONG now = GetTickCount64();
    impl_->brightDirty = true;
    impl_->brightMonitor = monitor;
    impl_->brightPercent = percent;
    if (now - impl_->brightAppliedAt < 70 && percent != 0 && percent != 100) return;
    Flush();
}

void DeviceLevels::Flush() {
    if (!impl_->brightDirty) return;
    const int percent = impl_->brightPercent;
    const HMONITOR monitor = impl_->brightMonitor;
    if (!WritePhysical(monitor, percent)) {
        int value = percent;
        if (!UseWmi(true, value)) return;
    }
    impl_->brightDirty = false;
    impl_->brightAppliedAt = GetTickCount64();
    impl_->wmiPercent = percent;
    impl_->wmiTriedAt = impl_->brightAppliedAt;
}

}  // namespace op
