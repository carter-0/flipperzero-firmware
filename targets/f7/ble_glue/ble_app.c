#include "ble_app.h"

#include <core/check.h>
#include <ble/ble.h>
#include <interface/patterns/ble_thread/tl/hci_tl.h>
#include <interface/patterns/ble_thread/shci/shci.h>
#include <interface/patterns/ble_thread/tl/tl.h>
#include <ble/core/ble_std.h>
#include "gap.h"
#include "furi_ble/event_dispatcher.h"

#include <furi_hal.h>
#include <furi.h>

#define TAG "Bt"

PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_CmdPacket_t ble_app_cmd_buffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint32_t ble_app_nvm[BLE_NVM_SRAM_SIZE];

_Static_assert(
    sizeof(SHCI_C2_Ble_Init_Cmd_Packet_t) == 58,
    "Ble stack config structure size mismatch (check new config options - last updated for v.1.17.3)");

typedef struct {
    FuriMutex* hci_mtx;
    FuriSemaphore* hci_sem;
    // Sniffer callback
    BleGlueHciRawPacketCallback sniffer_cb;
    void* sniffer_cb_context;
} BleApp;

static BleApp* ble_app = NULL;

static void ble_app_hci_event_handler(void* pPayload);
static void ble_app_hci_status_not_handler(HCI_TL_CmdStatus_t status);

static const HCI_TL_HciInitConf_t hci_tl_config = {
    .p_cmdbuffer = (uint8_t*)&ble_app_cmd_buffer,
    .StatusNotCallBack = ble_app_hci_status_not_handler,
};

static const SHCI_C2_CONFIG_Cmd_Param_t config_param = {
    .PayloadCmdSize = SHCI_C2_CONFIG_PAYLOAD_CMD_SIZE,
    .Config1 = SHCI_C2_CONFIG_CONFIG1_BIT0_BLE_NVM_DATA_TO_SRAM,
    .BleNvmRamAddress = (uint32_t)ble_app_nvm,
    .EvtMask1 = SHCI_C2_CONFIG_EVTMASK1_BIT1_BLE_NVM_RAM_UPDATE_ENABLE,
};

static const SHCI_C2_Ble_Init_Cmd_Packet_t ble_init_cmd_packet = {
    .Header = {{0, 0, 0}}, // Header unused
    .Param = {
        .pBleBufferAddress = 0, // pBleBufferAddress not used
        .BleBufferSize = 0, // BleBufferSize not used
        .NumAttrRecord = CFG_BLE_NUM_GATT_ATTRIBUTES,
        .NumAttrServ = CFG_BLE_NUM_GATT_SERVICES,
        .AttrValueArrSize = CFG_BLE_ATT_VALUE_ARRAY_SIZE,
        .NumOfLinks = CFG_BLE_NUM_LINK,
        .ExtendedPacketLengthEnable = CFG_BLE_DATA_LENGTH_EXTENSION,
        .PrWriteListSize = CFG_BLE_PREPARE_WRITE_LIST_SIZE,
        .MblockCount = CFG_BLE_MBLOCK_COUNT,
        .AttMtu = CFG_BLE_MAX_ATT_MTU,
        .PeripheralSca = CFG_BLE_SLAVE_SCA,
        .CentralSca = CFG_BLE_MASTER_SCA,
        .LsSource = CFG_BLE_LSE_SOURCE,
        .MaxConnEventLength = CFG_BLE_MAX_CONN_EVENT_LENGTH,
        .HsStartupTime = CFG_BLE_HSE_STARTUP_TIME,
        .ViterbiEnable = CFG_BLE_VITERBI_MODE,
        .Options = CFG_BLE_OPTIONS,
        .HwVersion = 0,
        .max_coc_initiator_nbr = 32,
        .min_tx_power = 0,
        .max_tx_power = 0,
        .rx_model_config = 1,
        /* New stack (13.3->15.0) */
        .max_adv_set_nbr = 1, // Only used if SHCI_C2_BLE_INIT_OPTIONS_EXT_ADV is set
        .max_adv_data_len = 1650, // Only used if SHCI_C2_BLE_INIT_OPTIONS_EXT_ADV is set
        .tx_path_compens = 0, // RF TX Path Compensation, * 0.1 dB
        .rx_path_compens = 0, // RF RX Path Compensation, * 0.1 dB
        .ble_core_version = SHCI_C2_BLE_INIT_BLE_CORE_5_4,
        /*15.0->17.0*/
        .Options_extension = SHCI_C2_BLE_INIT_OPTIONS_ENHANCED_ATT_NOTSUPPORTED |
                             SHCI_C2_BLE_INIT_OPTIONS_APPEARANCE_READONLY,
    }};

bool ble_app_init(void) {
    SHCI_CmdStatus_t status;
    ble_app = malloc(sizeof(BleApp));
    // Allocate semafore and mutex for ble command buffer access
    ble_app->hci_mtx = furi_mutex_alloc(FuriMutexTypeNormal);
    ble_app->hci_sem = furi_semaphore_alloc(1, 0);
    ble_app->sniffer_cb = NULL;
    ble_app->sniffer_cb_context = NULL;

    // Initialize Ble Transport Layer
    hci_init(ble_app_hci_event_handler, (void*)&hci_tl_config);

    do {
        // Configure NVM store for pairing data
        if((status = SHCI_C2_Config((SHCI_C2_CONFIG_Cmd_Param_t*)&config_param))) {
            FURI_LOG_E(TAG, "Failed to configure 2nd core: %d", status);
            break;
        }

        // Start ble stack on 2nd core
        if((status = SHCI_C2_BLE_Init((SHCI_C2_Ble_Init_Cmd_Packet_t*)&ble_init_cmd_packet))) {
            FURI_LOG_E(TAG, "Failed to start ble stack: %d", status);
            break;
        }

        if((status = SHCI_C2_SetFlashActivityControl(FLASH_ACTIVITY_CONTROL_SEM7))) {
            FURI_LOG_E(TAG, "Failed to set flash activity control: %d", status);
            break;
        }
    } while(false);

    return status == SHCI_Success;
}

void ble_app_get_key_storage_buff(uint8_t** addr, uint16_t* size) {
    *addr = (uint8_t*)ble_app_nvm;
    *size = sizeof(ble_app_nvm);
}

void ble_app_set_hci_raw_packet_cb(BleGlueHciRawPacketCallback callback, void* context) {
    if(ble_app) {
        ble_app->sniffer_cb = callback;
        ble_app->sniffer_cb_context = context;
    }
}

void ble_app_deinit(void) {
    furi_check(ble_app);

    furi_mutex_free(ble_app->hci_mtx);
    furi_semaphore_free(ble_app->hci_sem);
    free(ble_app);
    ble_app = NULL;
    memset(&ble_app_cmd_buffer, 0, sizeof(ble_app_cmd_buffer));
}

///////////////////////////////////////////////////////////////////////////////
// AN5289, 4.9

void hci_cmd_resp_release(uint32_t flag) {
    UNUSED(flag);
    furi_check(ble_app);
    furi_check(furi_semaphore_release(ble_app->hci_sem) == FuriStatusOk);
}

void hci_cmd_resp_wait(uint32_t timeout) {
    furi_check(ble_app);
    furi_check(furi_semaphore_acquire(ble_app->hci_sem, timeout) == FuriStatusOk);
}

///////////////////////////////////////////////////////////////////////////////

static void ble_app_hci_event_handler(void* pPayload) {
    furi_check(ble_app);

    tHCI_UserEvtRxParam* pParam = (tHCI_UserEvtRxParam*)pPayload;
    hci_event_pckt* hci_event_pckt_ptr = (hci_event_pckt*)&pParam->pckt->evtserial.evt;
    bool sniffer_handled_event = false;

    if(ble_app->sniffer_cb && furi_hal_bt_is_sniffer_active()) {
        if(hci_event_pckt_ptr->evt == HCI_LE_META_EVT_CODE) {
            evt_le_meta_event* meta_evt = (evt_le_meta_event*)hci_event_pckt_ptr->data;
            if(meta_evt->subevent == HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE) {
                uint8_t num_reports = meta_evt->data[0];
                uint8_t* current_report_ptr = &meta_evt->data[1]; // Point after Num_Reports

                for(uint8_t i = 0; i < num_reports; ++i) {
                    // Structure of each report:
                    // Event_Type (1 octet)
                    // Address_Type (1 octet)
                    // Address (6 octets)
                    // Data_Length (1 octet)
                    // Data (Data_Length octets)
                    // RSSI (1 octet)
                    uint8_t data_len = current_report_ptr[1 + 1 + 6]; // Index of Data_Length
                    uint8_t* adv_data = &current_report_ptr[1 + 1 + 6 + 1]; // Start of Data
                    int8_t rssi = (int8_t)current_report_ptr[1 + 1 + 6 + 1 + data_len]; // RSSI

                    ble_app->sniffer_cb(adv_data, data_len, rssi, ble_app->sniffer_cb_context);

                    // Advance pointer to the next report
                    current_report_ptr += (1 + 1 + 6 + 1 + data_len + 1);
                }
                sniffer_handled_event = true;
            } else if(meta_evt->subevent == HCI_LE_EXTENDED_ADVERTISING_REPORT_SUBEVT_CODE) {
                // For extended advertising, parsing is more complex due to variable report structures.
                // This is a simplified placeholder. Refer to Bluetooth Core Spec Vol 4, Part E, 7.7.65.13
                // hci_le_extended_advertising_report_event_rp0* ext_adv_report = (hci_le_extended_advertising_report_event_rp0*)meta_evt->data;
                // For now, log and skip detailed parsing for brevity.
                FURI_LOG_D(TAG, "Sniffer: HCI_LE_EXTENDED_ADVERTISING_REPORT_EVENT received");
                // Example: if (ext_adv_report->Num_Reports > 0) {
                //    const uint8_t* data_ptr = ext_adv_report->Reports[0].Data; // This is pseudocode, actual struct is complex
                //    uint8_t data_len = ext_adv_report->Reports[0].Data_Length;
                //    int8_t rssi = ext_adv_report->Reports[0].RSSI;
                //    ble_app->sniffer_cb(data_ptr, data_len, rssi, ble_app->sniffer_cb_context);
                // }
                sniffer_handled_event = true;
            }
        }
        // NOTE: If aci_hal_rx_start produces a different, vendor-specific event for raw packets,
        // it would need to be handled here (e.g., checking hci_event_pckt_ptr->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE
        // and then the specific ecode).
    }

    BleEventFlowStatus event_flow_status = BleEventFlowEnable;
    if(!sniffer_handled_event || !furi_hal_bt_is_sniffer_active()) { // If sniffer didn't handle or isn't exclusively handling
        event_flow_status = ble_event_dispatcher_process_event((void*)&(pParam->pckt->evtserial));
    }

    if(event_flow_status != BleEventFlowDisable) {
        pParam->status = HCI_TL_UserEventFlow_Enable;
    } else {
        pParam->status = HCI_TL_UserEventFlow_Disable;
    }
}

static void ble_app_hci_status_not_handler(HCI_TL_CmdStatus_t status) {
    if(status == HCI_TL_CmdBusy) {
        furi_hal_power_insomnia_enter();
        furi_mutex_acquire(ble_app->hci_mtx, FuriWaitForever);
    } else if(status == HCI_TL_CmdAvailable) {
        furi_mutex_release(ble_app->hci_mtx);
        furi_hal_power_insomnia_exit();
    }
}

void SVCCTL_ResumeUserEventFlow(void) {
    hci_resume_flow();
}
