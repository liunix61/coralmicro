#include "apps/EdgetpuDfu/dfu.h"
#include "third_party/nxp/rt1176-sdk/components/osa/fsl_os_abstraction.h"
#include "third_party/nxp/rt1176-sdk/devices/MIMXRT1176/utilities/debug_console/fsl_debug_console.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/host/class/usb_host_dfu.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/host/usb_host_devices.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/host/usb_host_ehci.h"
#include "third_party/nxp/rt1176-sdk/middleware/usb/host/usb_host_hci.h"

#include <algorithm>
#include <cstdio>

static usb_host_instance_t* gHostInstance = nullptr;
static usb_device_handle gDFUDeviceHandle;
static usb_host_interface_handle gDFUInterfaceHandle;
static usb_host_class_handle gDFUClassHandle;
static usb_host_dfu_status_t gDFUStatus;
static int gDFUBytesTransferred = 0;
static int gDFUBytesToTransfer = apex_latest_single_ep_bin_len;
static int gDFUCurrentBlockNum = 0;
static uint8_t *gDFUReadBackData = nullptr;
static OSA_MSGQ_HANDLE_DEFINE(gDFUMsgQueue, 1, sizeof(uint32_t));

enum dfu_state {
    DFU_STATE_UNATTACHED = 0,
    DFU_STATE_ATTACHED,
    DFU_STATE_SET_INTERFACE,
    DFU_STATE_GET_STATUS,
    DFU_STATE_TRANSFER,
    DFU_STATE_ZERO_LENGTH_TRANSFER,
    DFU_STATE_READ_BACK,
    DFU_STATE_GET_STATUS_READ,
    DFU_STATE_DETACH,
    DFU_STATE_CHECK_STATUS,
    DFU_STATE_COMPLETE,
    DFU_STATE_ERROR,
};

static void USB_DFUSetNextState(enum dfu_state next_state) {
    OSA_MsgQPut(gDFUMsgQueue, &next_state);
}

usb_status_t USB_DFUHostEvent(usb_host_handle host_handle, usb_device_handle device_handle,
                              usb_host_configuration_handle config_handle,
                              uint32_t event_code) {
    usb_host_configuration_t *configuration_ptr;
    usb_host_interface_t *interface_ptr;
    int id;
    gHostInstance = (usb_host_instance_t*)host_handle;
    switch (event_code) {
        case kUSB_HostEventAttach:
            configuration_ptr = (usb_host_configuration_t*)config_handle;
            for (int i = 0; i < configuration_ptr->interfaceCount; ++i) {
                interface_ptr = &configuration_ptr->interfaceList[i];
                id = interface_ptr->interfaceDesc->bInterfaceClass;
                if (id != USB_HOST_DFU_CLASS_CODE) {
                    continue;
                }

                id = interface_ptr->interfaceDesc->bInterfaceSubClass;
                if (id == USB_HOST_DFU_SUBCLASS_CODE) {
                    gDFUDeviceHandle = device_handle;
                    gDFUInterfaceHandle = interface_ptr;
                    break;
                }
            }
            return (gDFUDeviceHandle != nullptr) ? kStatus_USB_Success : kStatus_USB_NotSupported;
        case kUSB_HostEventEnumerationDone:
            // TODO: check if we're already dfuing, if handles are valid.
            USB_DFUSetNextState(DFU_STATE_ATTACHED);
            return kStatus_USB_Success;
        case kUSB_HostEventDetach:
            USB_DFUSetNextState(DFU_STATE_UNATTACHED);
            printf("Detached DFU\r\n");
            return kStatus_USB_Success;
        default:
            return kStatus_USB_Success;
    }
}

static void USB_DFUSetInterfaceCallback(void *param,
                                        uint8_t *data,
                                        uint32_t data_length,
                                        usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUSetInterface\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }
    USB_DFUSetNextState(DFU_STATE_GET_STATUS);
}

static void USB_DFUGetStatusCallback(void *param,
                                     uint8_t *data,
                                     uint32_t data_length,
                                     usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUGetStatus\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }

    if (gDFUBytesTransferred < gDFUBytesToTransfer) {
        USB_DFUSetNextState(DFU_STATE_TRANSFER);
    } else {
        USB_DFUSetNextState(DFU_STATE_ZERO_LENGTH_TRANSFER);
    }
}

static void USB_DFUTransferCallback(void *param,
                                    uint8_t *data,
                                    uint32_t data_length,
                                    usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUTransfer\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }

    gDFUCurrentBlockNum++;
    gDFUBytesTransferred += data_length;
    if (gDFUCurrentBlockNum % 10 == 0 || gDFUBytesTransferred == gDFUBytesToTransfer) {
        printf("Transferred %d bytes\r\n", gDFUBytesTransferred);
    }
    USB_DFUSetNextState(DFU_STATE_GET_STATUS);
}

static void USB_DFUZeroLengthTransferCallback(void *param,
                                              uint8_t *data,
                                              uint32_t data_length,
                                              usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUZeroLengthTransfer\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }

    gDFUCurrentBlockNum = 0;
    gDFUBytesTransferred = 0;
    USB_DFUSetNextState(DFU_STATE_READ_BACK);
}

static void USB_DFUReadBackCallback(void *param,
                                    uint8_t *data,
                                    uint32_t data_length,
                                    usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUReadBack\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }

    gDFUCurrentBlockNum++;
    gDFUBytesTransferred += data_length;
    if (gDFUCurrentBlockNum % 10 == 0 || gDFUBytesTransferred == gDFUBytesToTransfer) {
        printf("Read back %d bytes\r\n", gDFUBytesTransferred);
    }
    USB_DFUSetNextState(DFU_STATE_GET_STATUS_READ);
}

static void USB_DFUGetStatusReadCallback(void *param,
                                         uint8_t *data,
                                         uint32_t data_length,
                                         usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUGetStatusRead\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }

    if (gDFUBytesTransferred < gDFUBytesToTransfer) {
        USB_DFUSetNextState(DFU_STATE_READ_BACK);
    } else {
        if (memcmp(apex_latest_single_ep_bin, gDFUReadBackData, apex_latest_single_ep_bin_len) != 0) {
            printf("Read back firmware does not match!\r\n");
            USB_DFUSetNextState(DFU_STATE_ERROR);
        } else {
            USB_DFUSetNextState(DFU_STATE_DETACH);
        }
        OSA_MemoryFree(gDFUReadBackData);
        gDFUReadBackData = nullptr;
        gDFUCurrentBlockNum = 0;
        gDFUBytesTransferred = 0;
    }
}

static void USB_DFUDetachCallback(void *param,
                                  uint8_t *data,
                                  uint32_t data_length,
                                  usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUDetach\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }
    USB_DFUSetNextState(DFU_STATE_CHECK_STATUS);
}

static void USB_DFUCheckStatusCallback(void *param,
                                       uint8_t *data,
                                       uint32_t data_length,
                                       usb_status_t status) {
    if (status != kStatus_USB_Success) {
        printf("Error in DFUCheckStatus\r\n");
        USB_DFUSetNextState(DFU_STATE_ERROR);
        return;
    }
    USB_DFUSetNextState(DFU_STATE_COMPLETE);
}

void USB_DFUTaskInit() {
    OSA_MsgQCreate((osa_msgq_handle_t)gDFUMsgQueue, 1U, sizeof(uint32_t));
}

void USB_DFUTask() {
    usb_status_t ret;
    uint32_t transfer_length;
    enum dfu_state next_state;
    if (OSA_MsgQGet(gDFUMsgQueue, &next_state, osaWaitForever_c) != KOSA_StatusSuccess) {
        return;
    }
    switch (next_state) {
        case DFU_STATE_UNATTACHED:
            break;
        case DFU_STATE_ATTACHED:
            ret = USB_HostDfuInit(gDFUDeviceHandle, &gDFUClassHandle);
            if (ret == kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_SET_INTERFACE);
            } else {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_SET_INTERFACE:
            ret = USB_HostDfuSetInterface(gDFUClassHandle, 
                                          gDFUInterfaceHandle,
                                          0,
                                          USB_DFUSetInterfaceCallback,
                                          nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_GET_STATUS:
            ret = USB_HostDfuGetStatus(gDFUClassHandle,
                                       (uint8_t*)&gDFUStatus,
                                       USB_DFUGetStatusCallback,
                                       nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_TRANSFER:
            transfer_length = std::min(256U /* get from descriptor */,
                                       apex_latest_single_ep_bin_len - gDFUBytesTransferred);
            ret = USB_HostDfuDnload(gDFUClassHandle,
                                    gDFUCurrentBlockNum,
                                    apex_latest_single_ep_bin + gDFUBytesTransferred,
                                    transfer_length,
                                    USB_DFUTransferCallback,
                                    0);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_ZERO_LENGTH_TRANSFER:
            ret = USB_HostDfuDnload(gDFUClassHandle, gDFUCurrentBlockNum, nullptr, 0,
                                    USB_DFUZeroLengthTransferCallback, nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_READ_BACK:
            if (!gDFUReadBackData) {
                gDFUReadBackData = (uint8_t*)OSA_MemoryAllocate(apex_latest_single_ep_bin_len);
            }
            transfer_length = std::min(256U /* get from descriptor */,
                                       apex_latest_single_ep_bin_len - gDFUBytesTransferred);
            ret = USB_HostDfuUpload(gDFUClassHandle, gDFUCurrentBlockNum, gDFUReadBackData + gDFUBytesTransferred,
                                    transfer_length, USB_DFUReadBackCallback, nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_GET_STATUS_READ:
            ret = USB_HostDfuGetStatus(gDFUClassHandle, (uint8_t*)&gDFUStatus, USB_DFUGetStatusReadCallback, nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_DETACH:
            ret = USB_HostDfuDetach(gDFUClassHandle, 1000 /* ms */, USB_DFUDetachCallback, nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_CHECK_STATUS:
            ret = USB_HostDfuGetStatus(gDFUClassHandle, (uint8_t*)&gDFUStatus, USB_DFUCheckStatusCallback, nullptr);
            if (ret != kStatus_USB_Success) {
                USB_DFUSetNextState(DFU_STATE_ERROR);
            }
            break;
        case DFU_STATE_COMPLETE:
            USB_HostDfuDeinit(gDFUDeviceHandle, gDFUClassHandle);
            gDFUClassHandle = nullptr;
            USB_HostEhciResetBus((usb_host_ehci_instance_t*)gHostInstance->controllerHandle);
            USB_HostTriggerReEnumeration(gDFUDeviceHandle);
            USB_DFUSetNextState(DFU_STATE_UNATTACHED);
            break;
        case DFU_STATE_ERROR:
            printf("DFU error\r\n");
            while (true) {
                OSA_TaskYield();
            }
            break;
        default:
            printf("Unhandled DFU state %d\r\n", next_state);
            while (true) {
                OSA_TaskYield();
            }
            break;
    }
}
