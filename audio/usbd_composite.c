// audio/usbd_composite.c — a composite USB device: CDC (console/control) + Audio-IN (4-ch record).
//
// ⚠ WHY THIS SHAPE. The pedal needs BOTH the serial console (libDaisy's CDC — the logger and the
// command set ride it) AND the audio interface, on the one OTG-FS port. ST's device core registers a
// single class, so this is a single composite class that DISPATCHES each USB callback to the right
// half: CDC requests/endpoints go straight to ST's `USBD_CDC` vtable (so CDC_Transmit_FS, the logger
// and UsbRx keep working unchanged, at classId 0), and audio requests/EP 0x83 go to the tt_audio_*
// ops (usbd_audio_in.c). No libDaisy fork: the device is created on libDaisy's own hUsbDeviceFS
// (usb_audio.cpp), the FS FIFO is re-carved there, and this file only supplies the class + descriptor.
//
// Endpoint / interface map (see tt_audio_stream.h):
//   IF0 CDC-comm  EP 0x82 (int IN)      IF1 CDC-data  EP 0x81 (bulk IN) / 0x01 (bulk OUT)
//   IF2 AudioCtl  (no EP)               IF3 AudioStr  EP 0x83 (iso IN, async)
#include "tt_audio_stream.h"
#include "usbd_core.h"
#include "usbd_cdc.h"       // extern USBD_ClassTypeDef USBD_CDC (ST's CDC class vtable)

#define TT_COMPOSITE_CFG_SIZ  193U     // = 0xC1 (CDC 75 + UAC2 audio 118), computed below

// ---- the composite configuration descriptor -------------------------------------------------------
__ALIGN_BEGIN static uint8_t s_cfg[TT_COMPOSITE_CFG_SIZ] __ALIGN_END = {
    // ---- Configuration (9) ----
    0x09, USB_DESC_TYPE_CONFIGURATION, LOBYTE(TT_COMPOSITE_CFG_SIZ), HIBYTE(TT_COMPOSITE_CFG_SIZ),
    0x04,                   // bNumInterfaces (CDC comm+data, Audio ctl+stream)
    0x01, 0x00, 0xC0, 0x32, // cfg value / iConfig / self-powered / 100 mA

    // ================= CDC function (IAD + IF0 comm + IF1 data) =================
    // ---- IAD: CDC (8) ----
    0x08, 0x0B, 0x00, 0x02, 0x02, 0x02, 0x01, 0x00,   // first IF0, 2 IFs, CDC/ACM/AT

    // ---- IF0: CDC Communication (9) ----
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,             // CDC header, bcdCDC 1.10
    0x05, 0x24, 0x01, 0x00, 0x01,             // call management: bDataInterface = 1
    0x04, 0x24, 0x02, 0x02,                   // ACM: line coding supported
    0x05, 0x24, 0x06, 0x00, 0x01,             // union: control IF0, sub IF1
    0x07, USB_DESC_TYPE_ENDPOINT, 0x82, 0x03, 0x08, 0x00, 0x10,  // EP0x82 int IN, 8B, bInterval 16

    // ---- IF1: CDC Data (9) ----
    0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, USB_DESC_TYPE_ENDPOINT, 0x01, 0x02, 0x40, 0x00, 0x00,  // EP0x01 bulk OUT 64
    0x07, USB_DESC_TYPE_ENDPOINT, 0x81, 0x02, 0x40, 0x00, 0x00,  // EP0x81 bulk IN  64

    // ================= Audio function — USB Audio Class 2.0 (clock-source port) =================
    // ⚠ UAC2 so macOS does SMOOTH async sample-rate conversion (UAC1 dropped a sample every ~2 s).
    // The Clock Source entity (ID3) is the key addition; the host reads the rate from it (handled in
    // usbd_audio_in.c tt_audio_setup). Interface protocol is 0x20 (IP_VERSION_02_00) throughout.
    // ---- IAD: Audio (8) ----  AUDIO / (subclass 0) / IP 2.00
    0x08, 0x0B, AUDIO_AC_INTERFACE, 0x02, 0x01, 0x00, 0x20, 0x00,

    // ---- IF2: AudioControl alt 0 (9) ----  AUDIOCONTROL, protocol 0x20
    0x09, USB_DESC_TYPE_INTERFACE, AUDIO_AC_INTERFACE, 0x00, 0x00, 0x01, 0x01, 0x20, 0x00,
    // AC header (9): bcdADC 2.00, bCategory I/O box, wTotalLength 46 (0x2E), bmControls 0
    0x09, 0x24, 0x01, 0x00, 0x02, 0x08, 0x2E, 0x00, 0x00,
    // Clock Source (8): ID3, internal FIXED clock (async, not SOF-synced), freq control read-only
    0x08, 0x24, 0x0A, AUDIO_CLOCK_ID, 0x01, 0x01, 0x00, 0x00,
    // Input Terminal (17): ID1 Microphone (0x0201), clock=ID3, 4 ch
    0x11, 0x24, 0x02, AUDIO_IT_ID, 0x01, 0x02, 0x00, AUDIO_CLOCK_ID, AUDIO_CHANNELS,
    0x00, 0x00, 0x00, 0x00,   // bmChannelConfig
    0x00,                     // iChannelNames
    0x00, 0x00,               // bmControls / iTerminal
    0x00,
    // Output Terminal (12): ID2 USB streaming (0x0101), from IT ID1, clock=ID3
    0x0C, 0x24, 0x03, AUDIO_OT_ID, 0x01, 0x01, 0x00, AUDIO_IT_ID, AUDIO_CLOCK_ID, 0x00, 0x00, 0x00,

    // ---- IF3: AudioStreaming alt 0 (idle) (9) ----  protocol 0x20
    0x09, USB_DESC_TYPE_INTERFACE, AUDIO_AS_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x20, 0x00,
    // ---- IF3: AudioStreaming alt 1 (9) ----
    0x09, USB_DESC_TYPE_INTERFACE, AUDIO_AS_INTERFACE, 0x01, 0x01, 0x01, 0x02, 0x20, 0x00,
    // AS general (16): terminal link = OT id2, PCM format, 4 ch
    0x10, 0x24, 0x01, AUDIO_OT_ID, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, AUDIO_CHANNELS,
    0x00, 0x00, 0x00, 0x00, 0x00,
    // Format type I (6): subslot = AUDIO_SUBFRAME bytes, resolution = AUDIO_BITS
    0x06, 0x24, 0x02, 0x01, AUDIO_SUBFRAME, AUDIO_BITS,
    // Std iso IN endpoint (7): 0x83, iso + async
    0x07, USB_DESC_TYPE_ENDPOINT, AUDIO_IN_EP, 0x05,
    LOBYTE(AUDIO_MAX_PACKET), HIBYTE(AUDIO_MAX_PACKET), 0x01,
    // CS AS iso data endpoint (8)
    0x08, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
};

__ALIGN_BEGIN static uint8_t s_devqual[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
    USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02, 0xEF, 0x02, 0x01, USB_MAX_EP0_SIZE, 0x01, 0x00,
};

// ---- dispatcher: route each callback to CDC (ST's vtable) or audio (tt_audio_*) -------------------
static uint8_t Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    uint8_t r = USBD_CDC.Init(pdev, cfgidx);   // opens CDC EPs, allocates its classdata at classId 0
    tt_audio_init();                           // audio EP opens later on SET_INTERFACE alt 1
    return r;
}

static uint8_t DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    tt_audio_deinit(pdev);
    return USBD_CDC.DeInit(pdev, cfgidx);
}

static uint8_t Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    const uint8_t ifn  = (uint8_t)(req->wIndex & 0xFFU);
    const uint8_t type = req->bmRequest & USB_REQ_TYPE_MASK;

    // Interface-recipient standard/class requests aimed at the audio interfaces go to the audio half.
    if((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE
       && (ifn == AUDIO_AS_INTERFACE || ifn == AUDIO_AC_INTERFACE)
       && (type == USB_REQ_TYPE_STANDARD || type == USB_REQ_TYPE_CLASS))
    {
        return tt_audio_setup(pdev, req);
    }
    return USBD_CDC.Setup(pdev, req);          // everything else is CDC's (incl. its class requests)
}

static uint8_t DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    if(epnum == (AUDIO_IN_EP & 0x0FU)) { tt_audio_datain(pdev); return USBD_OK; }
    return USBD_CDC.DataIn(pdev, epnum);       // CDC data IN (0x81) / cmd IN (0x82)
}

static uint8_t DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    return USBD_CDC.DataOut(pdev, epnum);      // CDC data OUT (0x01)
}

static uint8_t EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    // ⚠ Required for CDC SET_LINE_CODING's data stage — without it the host can't open the serial port.
    return USBD_CDC.EP0_RxReady ? USBD_CDC.EP0_RxReady(pdev) : (uint8_t)USBD_OK;
}

static uint8_t EP0_TxSent(USBD_HandleTypeDef *pdev)
{
    return USBD_CDC.EP0_TxSent ? USBD_CDC.EP0_TxSent(pdev) : (uint8_t)USBD_OK;
}

static uint8_t SOF(USBD_HandleTypeDef *pdev)
{
    tt_audio_sof();   // count host frames while streaming (diagnostic: host frame rate vs transmits)
    return USBD_CDC.SOF ? USBD_CDC.SOF(pdev) : (uint8_t)USBD_OK;
}

static uint8_t IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    (void)epnum;
    tt_audio_isoincomplete(pdev);
    return USBD_OK;
}

static uint8_t *GetFSCfg(uint16_t *length)    { *length = sizeof(s_cfg);     return s_cfg; }
static uint8_t *GetHSCfg(uint16_t *length)    { *length = sizeof(s_cfg);     return s_cfg; }
static uint8_t *GetOtherCfg(uint16_t *length) { *length = sizeof(s_cfg);     return s_cfg; }
static uint8_t *GetDevQual(uint16_t *length)  { *length = sizeof(s_devqual); return s_devqual; }

USBD_ClassTypeDef USBD_TT_Composite = {
    Init,
    DeInit,
    Setup,
    EP0_TxSent,
    EP0_RxReady,
    DataIn,
    DataOut,
    SOF,
    IsoINIncomplete,
    NULL,                 // IsoOUTIncomplete
    GetHSCfg,
    GetFSCfg,
    GetOtherCfg,
    GetDevQual,
};
