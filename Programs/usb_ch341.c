/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2021 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include "prologue.h"

#include <string.h>
#include <errno.h>

#include "log.h"
#include "usb_serial.h"
#include "usb_ch341.h"

struct UsbSerialDataStruct {
  char version[2];

  struct {
    uint8_t prescaler;
    uint8_t divisor;
  } baud;

  struct {
    uint8_t lcr1;
    uint8_t lcr2;
    uint8_t mcr;
  } control;

  struct {
    uint8_t msr;
    uint8_t lsr;
  } status;
};

typedef struct {
  uint16_t factor;
  uint8_t flags;
} CH341_PrescalerEntry;

static const CH341_PrescalerEntry CH341_prescalerTable[] = {
  { .factor = 00001, // 1
    .flags = USB_CH341_PSF_BYPASS_2x | USB_CH341_PSF_BYPASS_8x | USB_CH341_PSF_BYPASS_64x
  },

  { .factor = 00002, // 2
    .flags = USB_CH341_PSF_BYPASS_8x | USB_CH341_PSF_BYPASS_64x
  },

  { .factor = 00010, // 8
    .flags = USB_CH341_PSF_BYPASS_2x | USB_CH341_PSF_BYPASS_64x
  },

  { .factor = 00020, // 16
    .flags = USB_CH341_PSF_BYPASS_64x
  },

  { .factor = 00100, // 64
    .flags = USB_CH341_PSF_BYPASS_2x | USB_CH341_PSF_BYPASS_8x
  },

  { .factor = 00200, // 128
    .flags = USB_CH341_PSF_BYPASS_8x
  },

  { .factor = 01000, // 512
    .flags = USB_CH341_PSF_BYPASS_2x
  },

  { .factor = 02000, // 1024
    .flags = 0
  },
};

static const uint8_t CH341_prescalerCOUNT = ARRAY_COUNT(CH341_prescalerTable);

static inline unsigned long
usbTransformValue_CH341 (uint16_t factor, unsigned long value) {
  return (((2UL * USB_CH341_FREQUENCY) / (factor * value)) + 1UL) / 2UL;
}

static unsigned int
usbCalculateBaud_CH341 (uint8_t prescaler, uint8_t divisor) {
  const CH341_PrescalerEntry *ps = CH341_prescalerTable;
  const CH341_PrescalerEntry *const end = ps + CH341_prescalerCOUNT;

  while (ps < end) {
    if (ps->flags == prescaler) {
      return usbTransformValue_CH341(
        ps->factor, (USB_CH341_DIVISOR_MINUEND - divisor)
      );
    }

    ps += 1;
  }

  return 0;
}

static void
usbLogVersion_CH341 (const UsbSerialData *usd) {
  logBytes(LOG_CATEGORY(SERIAL_IO),
    "CH341 version", usd->version, sizeof(usd->version)
  );
}

static void
usbLogBaud_CH341 (const UsbSerialData *usd) {
  unsigned int baud = usbCalculateBaud_CH341(
    usd->baud.prescaler, usd->baud.divisor
  );

  logMessage(LOG_CATEGORY(SERIAL_IO),
    "CH341 baud: PS:%02X DIV:%02X Baud:%u",
    usd->baud.prescaler, usd->baud.divisor, baud
  );
}

static void
usbLogStatus_CH341 (const UsbSerialData *usd) {
  logMessage(LOG_CATEGORY(SERIAL_IO),
    "CH341 status: MSR:%02X LSR:%02X",
    usd->status.msr, usd->status.lsr
  );
}

static int
usbControlRead_CH341 (
  UsbDevice *device, uint8_t request,
  uint16_t value, uint16_t index,
  unsigned char *buffer, size_t size
) {
  logMessage(LOG_CATEGORY(SERIAL_IO),
    "CH341 control read: %02X %04X %04X",
    request, value, index
  );

  ssize_t result = usbControlRead(device,
    USB_CH341_CONTROL_RECIPIENT, USB_CH341_CONTROL_TYPE,
    request, value, index, buffer, size,
    USB_CH341_CONTROL_TIMEOUT
  );

  if (result == -1) return 0;
  logBytes(LOG_CATEGORY(SERIAL_IO), "CH341 control response", buffer, result);
  if (result == size) return 1;

  logMessage(LOG_WARNING,
    "short CH341 control response: %"PRIsize" < %"PRIssize,
    result, size
  );

  return 0;
}

static int
usbReadRegisters_CH341 (
  UsbDevice *device,
  uint8_t register1, uint8_t *value1,
  uint8_t register2, uint8_t *value2
) {
  unsigned char buffer[2];

  int ok = usbControlRead_CH341(
    device, USB_CH341_REQ_READ_REGISTERS,
    (register2 << 8) | register1,
    0, buffer, sizeof(buffer)
  );

  if (ok) {
    *value1 = buffer[0];
    *value2 = buffer[1];
  }

  return ok;
}

static int
usbReadVersion_CH341 (UsbDevice *device) {
  UsbSerialData *usd = usbGetSerialData(device);
  const size_t size = sizeof(usd->version);
  unsigned char version[size];

  int ok = usbControlRead_CH341(
    device, USB_CH341_REQ_READ_VERSION, 0, 0, version, size
  );

  if (ok) {
    memcpy(usd->version, version, size);
    usbLogVersion_CH341(usd);
  }

  return ok;
}

static int
usbReadBaud_CH341 (UsbDevice *device) {
  UsbSerialData *usd = usbGetSerialData(device);

  int ok = usbReadRegisters_CH341(device,
    USB_CH341_REG_PRESCALER, &usd->baud.prescaler,
    USB_CH341_REG_DIVISOR, &usd->baud.divisor
  );

  if (ok) usbLogBaud_CH341(usd);
  return ok;
}

static int
usbReadStatus_CH341 (UsbDevice *device) {
  UsbSerialData *usd = usbGetSerialData(device);

  int ok = usbReadRegisters_CH341(device,
    USB_CH341_REG_MSR, &usd->status.msr,
    USB_CH341_REG_LSR, &usd->status.lsr
  );

  if (ok) {
    usd->status.msr ^= 0XFF;
    usd->status.lsr ^= 0XFF;
    usbLogStatus_CH341(usd);
  }

  return ok;
}

static int
usbControlWrite_CH341 (
  UsbDevice *device, uint8_t request,
  uint16_t value, uint16_t index
) {
  logMessage(LOG_CATEGORY(SERIAL_IO),
    "CH341 control write: %02X %04X %04X",
    request, value, index
  );

  ssize_t result = usbControlWrite(device,
    USB_CH341_CONTROL_RECIPIENT, USB_CH341_CONTROL_TYPE,
    request, value, index, NULL, 0,
    USB_CH341_CONTROL_TIMEOUT
  );

  return result != -1;
}

static int
usbWriteRegisters_CH341 (
  UsbDevice *device,
  uint8_t register1, uint8_t value1,
  uint8_t register2, uint8_t value2
) {
  return usbControlWrite_CH341(
    device, USB_CH341_REQ_WRITE_REGISTERS,
    (register2 << 8) | register1,
    (value2 << 8) | value1
  );
}

static int
usbGetBaudParameters (
  unsigned int wanted, unsigned int *actual,
  uint8_t *prescaler, uint8_t *divisor
) {
  const CH341_PrescalerEntry *ps = CH341_prescalerTable;
  const CH341_PrescalerEntry *const end = ps + CH341_prescalerCOUNT;

  const int NOT_FOUND = -1;
  int nearestDelta = NOT_FOUND;

  while (ps < end) {
    unsigned long psDivisor = usbTransformValue_CH341(ps->factor, wanted);

    if (psDivisor < ((ps->factor == 1)? 9: USB_CH341_DIVISOR_MINIMUM)) {
      break;
    }

    if (psDivisor <= USB_CH341_DIVISOR_MAXIMUM) {
      unsigned int baud = usbTransformValue_CH341(ps->factor, psDivisor);
      int delta = baud - wanted;
      if (delta < 0) delta = -delta;

      if ((nearestDelta == NOT_FOUND) || (delta <= nearestDelta)) {
        nearestDelta = delta;
        *actual = baud;
        *prescaler = ps->flags;
        *divisor = USB_CH341_DIVISOR_MINUEND - psDivisor;
      }
    }

    ps += 1;
  }

  return nearestDelta != NOT_FOUND;
}

static int
usbSetBaud_CH341 (UsbDevice *device, unsigned int baud) {
  if (baud < USB_CH341_BAUD_MINIMUM) return 0;
  if (baud > USB_CH341_BAUD_MAXIMUM) return 0;

  unsigned int actual;
  uint8_t prescaler;
  uint8_t divisor;

  if (!usbGetBaudParameters(baud, &actual, &prescaler, &divisor)) {
    return 0;
  }

  UsbSerialData *usd = usbGetSerialData(device);
  if ((prescaler == usd->baud.prescaler) && (divisor == usd->baud.divisor)) return 1;

  logMessage(LOG_CATEGORY(SERIAL_IO),
    "changing CH341 baud: %u -> %u",
    baud, actual
  );

  int ok = usbWriteRegisters_CH341(device,
    USB_CH341_REG_PRESCALER, (prescaler | USB_CH341_PSF_NO_WAIT),
    USB_CH341_REG_DIVISOR, divisor
  );

  if (ok) {
    usd->baud.prescaler = prescaler;
    usd->baud.divisor = divisor;
  }

  return ok;
}

static int
usbSetLCRs_CH341 (UsbDevice *device) {
  UsbSerialData *usd = usbGetSerialData(device);

  return usbWriteRegisters_CH341(device,
    USB_CH341_REG_LCR1, usd->control.lcr1,
    USB_CH341_REG_LCR2, usd->control.lcr2
  );
}

static int
usbUpdateLCR1_CH341 (UsbDevice *device, uint8_t mask, uint8_t value) {
  UsbSerialData *usd = usbGetSerialData(device);
  return usbUpdateByte(&usd->control.lcr1, mask, value);
}

static int
usbUpdateDataBits_CH341 (UsbDevice *device, unsigned int dataBits) {
  const uint8_t mask = USB_CH341_LCR1_DATA_BITS_MASK;
  uint8_t value = 0;

  switch (dataBits) {
    case 5: value |= USB_CH341_LCR1_DATA_BITS_5; break;
    case 6: value |= USB_CH341_LCR1_DATA_BITS_6; break;
    case 7: value |= USB_CH341_LCR1_DATA_BITS_7; break;
    case 8: value |= USB_CH341_LCR1_DATA_BITS_8; break;

    default:
      logUnsupportedDataBits(dataBits);
      return 0;
  }

  return usbUpdateLCR1_CH341(device, mask, value);
}

static int
usbUpdateStopBits_CH341 (UsbDevice *device, SerialStopBits stopBits) {
  const uint8_t mask = USB_CH341_LCR1_STOP_BITS_MASK;
  uint8_t value = 0;

  switch (stopBits) {
    case SERIAL_STOP_1:
      value |= USB_CH341_LCR1_STOP_BITS_1;
      break;

    case SERIAL_STOP_2:
      value |= USB_CH341_LCR1_STOP_BITS_2;
      break;

    default:
      logUnsupportedStopBits(stopBits);
      return 0;
  }

  return usbUpdateLCR1_CH341(device, mask, value);
}

static int
usbUpdateParity_CH341 (UsbDevice *device, SerialParity parity) {
  const uint8_t mask = USB_CH341_LCR1_PARITY_MASK;
  uint8_t value = 0;

  if (parity != SERIAL_PARITY_NONE) {
    value |= USB_CH341_LCR1_PARITY_ENABLE;

    switch (parity) {
      case SERIAL_PARITY_EVEN:
        value |= USB_CH341_LCR1_PARITY_EVEN;
        break;

      case SERIAL_PARITY_ODD:
        value |= USB_CH341_LCR1_PARITY_ODD;
        break;

      case SERIAL_PARITY_SPACE:
        value |= USB_CH341_LCR1_PARITY_SPACE;
        break;

      case SERIAL_PARITY_MARK:
        value |= USB_CH341_LCR1_PARITY_MARK;
        break;

      default:
        logUnsupportedParity(parity);
        return 0;
    }
  }

  return usbUpdateLCR1_CH341(device, mask, value);
}

static int
usbSetDataFormat_CH341 (UsbDevice *device, unsigned int dataBits, SerialStopBits stopBits, SerialParity parity) {
  int changed = usbUpdateDataBits_CH341(device, dataBits)
             || usbUpdateStopBits_CH341(device, stopBits)
             || usbUpdateParity_CH341(device, parity)
             ;

  if (!changed) return 1;
  return usbSetLCRs_CH341(device);
}

static int
usbSetMCR_CH341 (UsbDevice *device) {
  UsbSerialData *usd = usbGetSerialData(device);

  return usbControlWrite_CH341(
    device, USB_CH341_REQ_WRITE_MCR, ~usd->control.mcr, 0
  );
}

static int
usbInitializeSerial_CH341 (UsbDevice *device) {
  return usbControlWrite_CH341(device, USB_CH341_REQ_INITIALIZE_SERIAL, 0, 0);
}

static int
usbEnableAdapter_CH341 (UsbDevice *device) {
  usbReadVersion_CH341(device);

  typedef int Function (UsbDevice *device);

  static Function *const functions[] = {
    &usbInitializeSerial_CH341,
    &usbReadBaud_CH341,
    &usbSetLCRs_CH341,
    &usbSetMCR_CH341,
    &usbReadStatus_CH341,
    NULL
  };

  Function *const *function = functions;

  while (*function) {
    if (!(*function)(device)) return 0;
    function += 1;
  }

  return 1;
}

static int
usbMakeData_CH341 (UsbDevice *device, UsbSerialData **serialData) {
  UsbSerialData *usd;

  if ((usd = malloc(sizeof(*usd)))) {
    memset(usd, 0, sizeof(*usd));

    usd->control.lcr1 = USB_CH341_LCR1_DATA_BITS_8 
                      | USB_CH341_LCR1_TRANSMIT_ENABLE
                      | USB_CH341_LCR1_RECEIVE_ENABLE
                      ;

    *serialData = usd;
    return 1;
  } else {
    logMallocError();
  }

  return 0;
}

static void
usbDestroyData_CH341 (UsbSerialData *usd) {
  free(usd);
}

const UsbSerialOperations usbSerialOperations_CH341 = {
  .name = "CH341",     

  .enableAdapter = usbEnableAdapter_CH341,     
  .makeData = usbMakeData_CH341,     
  .destroyData = usbDestroyData_CH341,     

  .setBaud = usbSetBaud_CH341,
  .setDataFormat = usbSetDataFormat_CH341,
};
