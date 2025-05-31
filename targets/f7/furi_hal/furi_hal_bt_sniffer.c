#include "furi_hal_bt.h"
#include <ble/core/auto/ble_gap_aci.h>

extern struct {
    bool   active;
    FuriHalBtSnifferPacketCallback callback;
    void*  context;
} furi_hal_bt_sniffer_state;

/* This function is called by the stack every time it hears an advert */
void hci_le_advertising_report_event(uint8_t             num_reports,
                                     Advertising_Report_t reports[])
{
    FURI_LOG_I("FURI_HAL_BT_SNIFFER", "hci_le_advertising_report_event"); // TODO(debug): remove
    if(!furi_hal_bt_sniffer_state.active ||
       !furi_hal_bt_sniffer_state.callback) return;

    for(uint8_t i = 0; i < num_reports; i++) {
        const uint8_t* data = reports[i].Data;
        uint8_t        len  = reports[i].Length_Data;
        int8_t         rssi = reports[i].RSSI;

        /* Forward raw payload + RSSI to the user-supplied callback */
        furi_hal_bt_sniffer_state.callback(data, len, rssi,
                                           furi_hal_bt_sniffer_state.context);
    }
}
