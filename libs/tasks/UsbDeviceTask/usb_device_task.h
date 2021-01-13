#ifndef _LIBS_TASKS_USBDEVICETASK_USBDEVICETASK_H_
#define _LIBS_TASKS_USBDEVICETASK_USBDEVICETASK_H_

#include "libs/usb/descriptors.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/include/usb.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/device/usb_device.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/output/source/device/class/usb_device_class.h"

#include <functional>
#include <vector>

namespace valiant {

using usb_set_handle_callback = std::function<void(class_handle_t)>;
using usb_handle_event_callback = std::function<bool(uint32_t, void*)>;
class UsbDeviceTask {
  public:
    UsbDeviceTask();
    UsbDeviceTask(const UsbDeviceTask&) = delete;
    UsbDeviceTask &operator=(const UsbDeviceTask&) = delete;

    bool Init(usb_device_class_config_struct_t* config, usb_set_handle_callback sh_cb, usb_handle_event_callback he_cb);
    static UsbDeviceTask* GetSingleton() {
        static UsbDeviceTask task;
        return &task;
    }
    void UsbDeviceTaskFn();

    usb_device_handle device_handle() { return device_handle_; }
  private:
    DeviceDescriptor device_descriptor_ = {
        sizeof(DeviceDescriptor), 0x01, 0x0200,
        0xEF, 0x02, 0x01, 0x40,
        0x18d1, 0x93FF, 0x0001,
        1, 2, 3, 1
    };
    CompositeDescriptor composite_descriptor_ = {
        {
            sizeof(ConfigurationDescriptor),
            0x02,
            sizeof(CompositeDescriptor),
            2, // num_interfaces
            1,
            0,
            0x80, // kUsb11AndHigher
            250, // kUses500mA
        }, // ConfigurationDescriptor
        {
            sizeof(InterfaceAssociationDescriptor),
            0x0B,
            0, // first iface num
            2, // total num ifaces
            0x02, 0x02, 0x01, 0,
        }, // InterfaceAssociationDescriptor
        {
            {
                sizeof(InterfaceDescriptor),
                0x04,
                0, // iface num
                0, 1, 0x02, 0x02, 0x01, 0,
            }, // InterfaceDescriptor
            {
                sizeof(CdcHeaderFunctionalDescriptor),
                0x24,
                0x00, 0x0110,
            }, // CdcHeaderFunctionalDescriptor
            {
                sizeof(CdcCallManagementFunctionalDescriptor),
                0x24,
                0x01, 0, 1,
            }, // CdcCallManagementFunctionalDescriptor
            {
                sizeof(CdcAcmFunctionalDescriptor),
                0x24,
                0x02, 2,
            }, // CdcAcmFunctionalDescriptor
            {
                sizeof(CdcUnionFunctionalDescriptor),
                0x24,
                0x06, 0, 1
            }, // CdcUnionFunctionalDescriptor
            {
                sizeof(EndpointDescriptor),
                0x05,
                2 | 0x80, 0x03, 8, 0x10,
            }, // EndpointDescriptor

            {
                sizeof(InterfaceDescriptor),
                0x04,
                1,
                0, 2, 0x0A, 0x00, 0x00, 0,
            }, // InterfaceDescriptor
            {
                sizeof(EndpointDescriptor),
                0x05,
                1 & 0x7F, 0x02, 512, 0,
            }, // EndpointDescriptor
            {
                sizeof(EndpointDescriptor),
                0x05,
                1 | 0x80, 0x02, 512, 0,
            }, // EndpointDescriptor
        }, // CdcClassDescriptor
    };

    LangIdDescriptor lang_id_desc_ = {
        sizeof(LangIdDescriptor),
        0x03,
        0x0409,
    };

    static usb_status_t StaticHandler(usb_device_handle device_handle, uint32_t event, void *param);
    usb_status_t Handler(usb_device_handle device_handle, uint32_t event, void *param);

    std::vector<usb_device_class_config_struct_t> configs_;
    usb_device_class_config_list_struct_t config_list_;
    usb_device_handle device_handle_;

    std::vector<usb_set_handle_callback> set_handle_callbacks_;
    std::vector<usb_handle_event_callback> handle_event_callbacks_;
};

}  // namespace valiant

#endif  // _LIBS_TASKS_USBDEVICETASK_USBDEVICETASK_H_