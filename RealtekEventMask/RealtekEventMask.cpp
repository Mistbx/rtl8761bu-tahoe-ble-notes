#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSNumber.h>
#include "Mask.hpp"

class EXPORT RealtekEventMask : public IOService {
    OSDeclareDefaultStructors(RealtekEventMask)
public:
    bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(RealtekEventMask, IOService)

static RealtekEventMask *telemetry;
static volatile SInt32 targetRequests;
static volatile SInt32 maskRequests;
static volatile SInt32 patchedRequests;
static uint32_t routeState; // 0=not called, 1=registered, 2=routed, 3=failed
static uint32_t routeError;
static mach_vm_address_t originalRequest;
static const char *paths[] = {"/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily"};
static KernelPatcher::KextInfo usbInfo {
    "com.apple.iokit.IOUSBHostFamily", paths, 1, {true, true}, {}, KernelPatcher::KextInfo::Unloaded
};

static void publishState() {
    if (!telemetry)
        return;
    telemetry->setProperty("RouteState", routeState, 32);
    telemetry->setProperty("RouteError", routeError, 32);
    telemetry->setProperty("TargetRequests", static_cast<uint32_t>(targetRequests), 32);
    telemetry->setProperty("MaskRequests", static_cast<uint32_t>(maskRequests), 32);
    telemetry->setProperty("PatchedRequests", static_cast<uint32_t>(patchedRequests), 32);
}
bool RealtekEventMask::start(IOService *provider) {
    if (!IOService::start(provider) || !ADDPR(startSuccess))
        return false;
    telemetry = this;
    setProperty("VersionInfo", "0.2.3");
    setProperty("BuildTarget", "macOS 26.7.3 / 25G416; OCTEST only");
    setProperty("Boundary", "IOUSBHostDevice::internalDeviceRequest");
    publishState();
    registerService();
    return true;
}

static bool propertyEquals(const IORegistryEntry *entry, const char *key, uint32_t expected) {
    auto property = entry->IORegistryEntry::copyProperty(key);
    auto number = OSDynamicCast(OSNumber, property);
    const bool matches = number && number->unsigned32BitValue() == expected;
    if (property)
        property->release();
    return matches;
}

struct AsyncMaskTransfer {
    IOUSBHostCompletion completion;
    IOBufferMemoryDescriptor *buffer;
};

static void maskTransferComplete(void *, void *context, IOReturn status, uint32_t transferred) {
    auto transfer = static_cast<AsyncMaskTransfer *>(context);
    if (telemetry) {
        telemetry->setProperty("MaskUSBCompletion", static_cast<uint32_t>(status), 32);
        telemetry->setProperty("MaskUSBTransferred", transferred, 32);
    }
    transfer->completion.action(transfer->completion.owner, transfer->completion.parameter,
                                status, transferred);
    transfer->buffer->release();
    IOFree(transfer, sizeof(*transfer));
}

static IOReturn requestHook(IOUSBHostDevice *device, IOService *provider,
    StandardUSB::DeviceRequest &request, void *data, IOMemoryDescriptor *descriptor,
    unsigned int &length, IOUSBHostCompletion *completion, unsigned int timeout) {
    const auto original = FunctionCast(requestHook, originalRequest);
    // Keep ordinary USB requests on the original path before touching device state.
    if ((request.bmRequestType != 0x20 && request.bmRequestType != 0x21) ||
        (request.bRequest != 0 && request.bRequest != 0xE0) || request.wValue != 0 ||
        request.wIndex != 0 || USBToHost16(request.wLength) != 11 ||
        !propertyEquals(device, "idVendor", 0x0BDA) ||
        !propertyEquals(device, "idProduct", 0xA728))
        return original(device, provider, request, data, descriptor, length, completion, timeout);

    OSIncrementAtomic(&targetRequests);
    if (telemetry) {
        telemetry->setProperty("LastSetup", &request, sizeof(request));
        telemetry->setProperty("LastDescriptorLength", descriptor ? descriptor->getLength() : 0, 64);
        telemetry->setProperty("LastCandidateAsync", completion != nullptr);
    }
    publishState();

    uint8_t packet[11] {};
    if (data && !descriptor) {
        memcpy(packet, data, sizeof(packet));
    } else if (!data && descriptor && descriptor->getLength() == sizeof(packet) &&
               (descriptor->getDirection() & kIODirectionOut) &&
               descriptor->prepare(kIODirectionOut) == kIOReturnSuccess) {
        const auto count = descriptor->readBytes(0, packet, sizeof(packet));
        descriptor->complete(kIODirectionOut);
        if (count != sizeof(packet))
            return original(device, provider, request, data, descriptor, length, completion, timeout);
    } else {
        return original(device, provider, request, data, descriptor, length, completion, timeout);
    }

    if (telemetry)
        telemetry->setProperty("LastCandidateOpcode", packet[0] | (packet[1] << 8), 32);

    uint8_t before[8];
    memcpy(before, packet + 3, sizeof(before));
    if (!enableReadyEvents(packet, sizeof(packet)))
        return original(device, provider, request, data, descriptor, length, completion, timeout);

    OSIncrementAtomic(&maskRequests);
    auto replacement = IOBufferMemoryDescriptor::withBytes(packet, sizeof(packet), kIODirectionOut);
    void *outbound = replacement ? replacement->getBytesNoCopy() : nullptr;
    const bool verified = outbound && memcmp(outbound, packet, sizeof(packet)) == 0;
    if (telemetry) {
        telemetry->setProperty("MaskSetup", &request, sizeof(request));
        telemetry->setProperty("MaskBefore", before, sizeof(before));
        telemetry->setProperty("MaskWriteVerified", verified);
    }
    if (!verified) {
        if (replacement)
            replacement->release();
        publishState();
        return original(device, provider, request, data, descriptor, length, completion, timeout);
    }

    if (telemetry)
        telemetry->setProperty("MaskAfter", static_cast<uint8_t *>(outbound) + 3, sizeof(before));
    IOUSBHostCompletion wrapped {};
    AsyncMaskTransfer *transfer = nullptr;
    if (completion) {
        if (completion->action)
            transfer = static_cast<AsyncMaskTransfer *>(IOMalloc(sizeof(AsyncMaskTransfer)));
        if (!transfer) {
            replacement->release();
            publishState();
            return original(device, provider, request, data, descriptor, length, completion, timeout);
        }
        transfer->completion = *completion;
        transfer->buffer = replacement;
        wrapped = {completion->owner, maskTransferComplete, transfer};
    }
    if (telemetry)
        telemetry->setProperty("MaskAsync", completion != nullptr);
    OSIncrementAtomic(&patchedRequests);
    const auto result = original(device, provider, request, nullptr, replacement, length,
                                 completion ? &wrapped : nullptr, timeout);
    // A successful asynchronous submission transfers cleanup to the callback,
    // which can already have run by the time the original call returns.
    if (!completion || result != kIOReturnSuccess) {
        replacement->release();
        if (transfer)
            IOFree(transfer, sizeof(*transfer));
    }
    if (telemetry) {
        telemetry->setProperty("MaskUSBReturn", static_cast<uint32_t>(result), 32);
        if (!completion) {
            telemetry->setProperty("MaskUSBCompletion", static_cast<uint32_t>(result), 32);
            telemetry->setProperty("MaskUSBTransferred", length, 32);
        }
    }
    publishState();
    return result;
}

static void initialise() {
    routeState = 1;
    lilu.onKextLoadForce(&usbInfo, 1,
        [](void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            if (index != usbInfo.loadIndex || originalRequest)
                return;
            constexpr const char *symbol =
                "__ZN15IOUSBHostDevice21internalDeviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj";
            // Verify the position-independent prologue observed in the 25G416 kernel collection.
            constexpr uint8_t prologue[] = {
                0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x83, 0xec, 0x38,
                0x44, 0x8b, 0x55, 0x18, 0x4c, 0x8b, 0x5d, 0x10
            };
            auto entry = patcher.solveSymbol<const uint8_t *>(index, symbol, address, size);
            if (!entry || memcmp(entry, prologue, sizeof(prologue)) != 0) {
                routeState = 3;
                routeError = entry ? 0x100 : static_cast<uint32_t>(patcher.getError());
                publishState();
                return;
            }
            KernelPatcher::RouteRequest route {symbol, requestHook, originalRequest};
            const bool routed = patcher.routeMultiple(index, &route, 1, address, size);
            routeState = routed ? 2 : 3;
            routeError = static_cast<uint32_t>(patcher.getError());
            publishState();
        });
}
static const char *off[] = {"-rtlmaskoff"};
PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME), parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal, off, arrsize(off), nullptr, 0, nullptr, 0,
    KernelVersion::Tahoe, KernelVersion::Tahoe, initialise
};
