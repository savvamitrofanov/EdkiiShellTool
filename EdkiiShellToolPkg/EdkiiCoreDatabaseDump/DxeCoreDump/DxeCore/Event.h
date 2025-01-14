/** @file
  UEFI Event support functions and structure.

Copyright (c) 2006 - 2010, Intel Corporation. All rights reserved.<BR>
(C) Copyright 2015 Hewlett Packard Enterprise Development LP<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#ifndef __EVENT_H__
#define __EVENT_H__

#include <Protocol/Runtime.h>

///
/// Set if Event is part of an event group
///
#define EVT_EXFLAG_EVENT_GROUP  0x01
///
/// Set if Event is registered on a protocol notify
///
#define EVT_EXFLAG_EVENT_PROTOCOL_NOTIFICATION  0x02

//
// EFI_EVENT
//

///
/// Timer event information
///
typedef struct {
  LIST_ENTRY    Link;
  UINT64        TriggerTime;
  UINT64        Period;
} TIMER_EVENT_INFO;

#define EVENT_SIGNATURE  SIGNATURE_32('e','v','n','t')
typedef struct {
  UINTN                      Signature;
  UINT32                     Type;
  UINT32                     SignalCount;
  ///
  /// Entry if the event is registered to be signalled
  ///
  LIST_ENTRY                 SignalLink;
  ///
  /// Notification information for this event
  ///
  EFI_TPL                    NotifyTpl;
  EFI_EVENT_NOTIFY           NotifyFunction;
  VOID                       *NotifyContext;
  EFI_GUID                   EventGroup;
  LIST_ENTRY                 NotifyLink;
  UINT8                      ExFlag;
  ///
  /// A list of all runtime events
  ///
  EFI_RUNTIME_EVENT_ENTRY    RuntimeData;
  TIMER_EVENT_INFO           Timer;
} IEVENT;

#endif
