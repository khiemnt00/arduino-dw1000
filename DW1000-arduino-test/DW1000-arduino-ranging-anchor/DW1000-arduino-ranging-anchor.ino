/*
 * Copyright (c) 2015 by Thomas Trojer <thomas@trojer.net>
 * Decawave DW1000 library for arduino.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Use this to test two-way ranging functionality with two
 * DW1000. This is the anchor component's code which computes range after
 * exchanging some messages. Addressing and frame filtering is currently done 
 * in a custom way, as no MAC features are implemented yet.
 *
 * Complements the "DW1000-arduino-ranging-tag" sketch. 
 */

#include <SPI.h>
#include <DW1000.h>

// messages used in the ranging protocol
#define POLL 0
#define POLL_ACK 1
#define RANGE 2
#define RANGE_REPORT 3
volatile byte expectedMsgId = POLL;
volatile boolean sentAck = false;
volatile boolean receivedAck = false;
// timestamps to remember
unsigned long timePollSent;
unsigned long timePollReceived;
unsigned long timePollAckSent;
unsigned long timePollAckReceived;
unsigned long timeRangeSent;
unsigned long timeRangeReceived;
// data buffer
#define LEN_DATA 13
byte data[LEN_DATA];
// reset line to the chip
int RST = 9;

void setup() {
  // DEBUG monitoring
  Serial.begin(9600);
  Serial.println("### DW1000-arduino-ranging-anchor ###");
  // initialize the driver
  DW1000.begin(SS, RST, 0);
  Serial.println("DW1000 initialized ...");
  // general configuration
  DW1000.newConfiguration();
  DW1000.setDefaults();
  DW1000.setDeviceAddress(1);
  DW1000.setNetworkId(10);
  DW1000.setFrameFilter(false);
  DW1000.commitConfiguration();
  Serial.println("Committed configuration ...");
  // DEBUG chip info and registers pretty printed
  Serial.print("Device ID: "); Serial.println(DW1000.getPrintableDeviceIdentifier());
  Serial.print("Unique ID: "); Serial.println(DW1000.getPrintableExtendedUniqueIdentifier());
  Serial.print("Network ID & Device Address: "); Serial.println(DW1000.getPrintableNetworkIdAndShortAddress());
  // attach callback for (successfully) sent and received messages
  DW1000.attachSentHandler(handleSent);
  DW1000.attachReceivedHandler(handleReceived);
  // anchor starts in receiving mode, awaiting a ranging poll message
  receiver();
}

void handleSent() {
  // status change on sent success
  sentAck = true;
}

void handleReceived() {
  // status change on received success
  receivedAck = true;
}

void transmitPollAck() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  data[0] = POLL_ACK;
  DW1000.setData(data, LEN_DATA);
  DW1000.startTransmit();
  receiver();
}

void transmitRangeReport() {
  DW1000.newTransmit();
  DW1000.setDefaults();
  data[0] = RANGE_REPORT;
  DW1000.setData(data, LEN_DATA);
  DW1000.startTransmit();
  receiver();
}

void receiver() {
  DW1000.newReceive();
  DW1000.setDefaults();
  // so we don't need to restart the receiver manually
  DW1000.permanentReceive(false);
  DW1000.startReceive();
}

unsigned long getRange() {
  // correct timestamps (in case system time counter wrap-arounds occured)
  // TODO
  // two roundtrip times - each minus message preparation times / 4
  unsigned long timeOfFlight = ((timePollAckReceived-timePollSent)-(timePollAckSent-timePollReceived) +
      (timeRangeReceived-timePollAckSent)-(timeRangeSent-timePollAckReceived)) / 4;
  // TODO mult by speed of light
  return timeOfFlight;
}

void loop() {
  if(!sentAck && !receivedAck) {
    return;
  }
  // continue on any success confirmation
  if(sentAck) {
    sentAck = false;
    // get timestamp
    unsigned long txTime = DW1000.getTransmitTimestamp();
    byte msgId = data[0];
    if(msgId == POLL_ACK) {
      timePollAckSent = txTime;
      Serial.print("Sent POLL ACK @ "); Serial.println(timePollAckSent);
    }
  } else if(receivedAck) {
    receivedAck = false;;
    // get timestamp
    unsigned long rxTime = DW1000.getReceiveTimestamp();
    // get message and parse
    DW1000.getData(data, LEN_DATA);
    byte msgId = data[0];
    if(msgId != expectedMsgId) {
      // unexpected message, start over again
      Serial.print("Received wrong message # "); Serial.println(msgId);
      delay(2000);
      expectedMsgId = POLL;
      return;
    }
    if(msgId == POLL) {
      timePollReceived = rxTime;
      expectedMsgId = RANGE;
      Serial.print("Received POLL @ "); Serial.println(timePollReceived);
      transmitPollAck();
    } else if(msgId == RANGE) {
      timeRangeReceived = rxTime;
      timePollSent = readTimestamp(data+1);
      timePollAckReceived = readTimestamp(data+5);
      timeRangeSent = readTimestamp(data+9);
      expectedMsgId = POLL;
      Serial.print("Received RANGE @ "); Serial.println(timeRangeReceived);
      Serial.print("POLL sent @ "); Serial.println(timePollSent);
      Serial.print("POLL ACK received @ "); Serial.println(timePollAckReceived);
      Serial.print("RANGE sent @ "); Serial.println(timeRangeSent);
      Serial.print("Range time is "); Serial.println(getRange());
      transmitRangeReport();
    }
  }
}

/* Helper function to convert data bytes back to long timestamps. */
unsigned long readTimestamp(byte data[]) {
  unsigned long tsValue = (unsigned long)(data[0]);
  tsValue |= (unsigned long)(data[1] << 8); 
  tsValue |= (unsigned long)(data[2] << 16);
  tsValue |= (unsigned long)(data[3] << 24);
  return tsValue;
}
