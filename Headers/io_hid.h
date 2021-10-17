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

#ifndef BRLTTY_INCLUDED_IO_HID
#define BRLTTY_INCLUDED_IO_HID

#include "hid_types.h"
#include "async_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct HidDeviceStruct HidDevice;

typedef struct {
  const char *manufacturerName;
  const char *productDescription;
  const char *serialNumber;
  HidDeviceIdentifier vendorIdentifier;
  HidDeviceIdentifier productIdentifier;
} HidUSBFilter;

extern void hidInitializeUSBFilter (HidUSBFilter *filter);
extern HidDevice *hidOpenUSBDevice (const HidUSBFilter *filter);

typedef struct {
  const char *deviceAddress;
  const char *deviceName;
  HidDeviceIdentifier vendorIdentifier;
  HidDeviceIdentifier productIdentifier;
} HidBluetoothFilter;

extern void hidInitializeBluetoothFilter (HidBluetoothFilter *filter);
extern HidDevice *hidOpenBluetoothDevice (const HidBluetoothFilter *filter);

typedef struct {
  struct {
    HidDeviceIdentifier vendor;
    HidDeviceIdentifier product;
  } identifiers;

  struct {
    const char *manufacturerName;
    const char *productDescription;
    const char *serialNumber;
  } usb;

  struct {
    const char *deviceAddress;
    const char *deviceName;
  } bluetooth;

  struct {
    unsigned char wantUSB:1;
    unsigned char wantBluetooth:1;
  } flags;
} HidFilter;

extern void hidInitializeFilter (HidFilter *filter);

extern int hidSetFilterIdentifiers (
  HidFilter *filter, const char *vendor, const char *product
);

extern int hidOpenDeviceWithFilter (
  HidDevice **device, const HidFilter *filter
);

extern int hidOpenDeviceWithParameters (
  HidDevice **device, const char *string
);

extern void hidCloseDevice (HidDevice *device);

extern HidItemsDescriptor *hidGetItems (HidDevice *device);
extern int hidGetDeviceIdentifiers (HidDevice *device, HidDeviceIdentifier *vendor, HidDeviceIdentifier *product);

extern int hidGetReport (HidDevice *device, unsigned char *buffer, size_t size);
extern int hidSetReport (HidDevice *device, const unsigned char *report, size_t size);

extern int hidGetFeature (HidDevice *device, unsigned char *buffer, size_t size);
extern int hidSetFeature (HidDevice *device, const unsigned char *feature, size_t size);

extern int hidWriteData (HidDevice *device, const unsigned char *data, size_t size);
extern int hidMonitorInput (HidDevice *device, AsyncMonitorCallback *callback, void *data);
extern int hidAwaitInput (HidDevice *device, int timeout);

extern ssize_t hidReadData (
  HidDevice *device, unsigned char *buffer, size_t size,
  int initialTimeout, int subsequentTimeout
);

extern const char *hidGetDeviceAddress (HidDevice *device);
extern const char *hidGetDeviceName (HidDevice *device);

extern const char *hidGetHostPath (HidDevice *device);
extern const char *hidGetHostDevice (HidDevice *device);

extern int hidParseDeviceIdentifier (HidDeviceIdentifier *identifier, const char *string);
extern int hidMatchString (const char *actualString, const char *testString);

extern const char *hidMakeDeviceIdentifier (
  HidDevice *device, char *buffer, size_t size
);

#define HID_DEVICE_QUALIFIER "hid"
extern int isHidDeviceIdentifier (const char **identifier);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BRLTTY_INCLUDED_IO_HID */
