/** @file

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiLib.h>
#include <Library/HobLib.h>
#include <Protocol/FirmwareVolume2.h>

#include <Protocol/PiPcd.h>
#include <Protocol/Pcd.h>
#include <Protocol/PiPcdInfo.h>
#include <Protocol/PcdInfo.h>

#include <Guid/VariableFormat.h>

#include <Guid/PcdDataBaseHobGuid.h>
#include <Guid/PcdDataBaseSignatureGuid.h>
#define PCD_TYPE_ALL_SET_WITHOUT_SKU  (PCD_TYPE_DATA | PCD_TYPE_HII | PCD_TYPE_VPD | PCD_TYPE_STRING)

#include <PCD/PcdPei.h>
#include <PCD/PcdDxe.h>

extern UINTN   Argc;
extern CHAR16  **Argv;

UINTN  mPcdPeiLocalTokenCount;

EFI_PCD_PROTOCOL           *mPiPcd;
PCD_PROTOCOL               *mPcd;
EFI_GET_PCD_INFO_PROTOCOL  *mPiPcdInfo;
GET_PCD_INFO_PROTOCOL      *mPcdInfo;

EFI_GUID  mPcdDxeDriverGuid = PCD_DXE_DRIVER_GUID;
EFI_GUID  mPcdPeiDriverGuid = PCD_PEI_DRIVER_GUID;

/**

  This function parse application ARG.

  @return Status
**/
EFI_STATUS
GetArg (
  VOID
  );

/**
  Read a file.

  @param[in]  FileName        The file to be read.
  @param[out] BufferSize      The file buffer size
  @param[out] Buffer          The file buffer

  @retval EFI_SUCCESS    Read file successfully
  @retval EFI_NOT_FOUND  File not found
**/
EFI_STATUS
ReadFileToBuffer (
  IN  CHAR16  *FileName,
  OUT UINTN   *BufferSize,
  OUT VOID    **Buffer
  );

/**

  This function dump raw data.

  @param  Data  raw data
  @param  Size  raw data size

**/
VOID
InternalDumpData (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN  Index;

  for (Index = 0; Index < Size; Index++) {
    Print (L"%02x", (UINTN)Data[Index]);
  }
}

/**

  This function dump raw data with colume format.

  @param  Data  raw data
  @param  Size  raw data size

**/
VOID
InternalDumpHex (
  IN UINT8  *Data,
  IN UINTN  Size
  )
{
  UINTN  Index;
  UINTN  Count;
  UINTN  Left;

  #define COLUME_SIZE  (16 * 2)

  Count = Size / COLUME_SIZE;
  Left  = Size % COLUME_SIZE;
  for (Index = 0; Index < Count; Index++) {
    Print (L"%04x: ", Index * COLUME_SIZE);
    InternalDumpData (Data + Index * COLUME_SIZE, COLUME_SIZE);
    Print (L"\n");
  }

  if (Left != 0) {
    Print (L"%04x: ", Index * COLUME_SIZE);
    InternalDumpData (Data + Index * COLUME_SIZE, Left);
    Print (L"\n");
  }
}

/**
#        A local token number is a 32-bit value in following meaning:
#         32 ------------- 28 ---------- 24 -------- 0
#          | PCD type mask  | Datum Type  |  Offset  |
#          +-----------------------------------------+
#        where:
#          PCd type mask: indicate Pcd type from following macro:
#                         PCD_TYPE_DATA
#                         PCD_TYPE_HII
#                         PCD_TYPE_VPD
#                         PCD_TYPE_STRING
#          Datum Type   : indicate PCD vaue type from following macro:
#                         PCD_DATUM_TYPE_POINTER
#                         PCD_DATUM_TYPE_UINT8
#                         PCD_DATUM_TYPE_UINT16
#                         PCD_DATUM_TYPE_UINT32
#                         PCD_DATUM_TYPE_UINT64
#          Offset      : indicate the related offset of PCD value in PCD database array.
#       Based on local token number, PCD driver could fast determine PCD type, value
#       type and get PCD entry from PCD database.
**/
BOOLEAN
IsSkuPcd (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN VOID               *DataPtr,
  IN UINTN              DataSize
  )
{
  PCD_DATABASE_SKU_DELTA  *SkuDelta;
  PCD_DATA_DELTA          *DeltaData;
  UINTN                   Offset;

  if (PcdDatabaseInit->LengthForAllSkus <= PcdDatabaseInit->Length) {
    return FALSE;
  }

  Offset   = PcdDatabaseInit->Length;
  Offset   = ALIGN_VALUE (Offset, 8);
  SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
  while ((UINTN)SkuDelta < (UINTN)PcdDatabaseInit + PcdDatabaseInit->LengthForAllSkus) {
    for (DeltaData = (PCD_DATA_DELTA *)(SkuDelta + 1);
         (UINTN)DeltaData < (UINTN)SkuDelta + SkuDelta->Length;
         DeltaData++)
    {
      if (((UINTN)PcdDatabaseInit + DeltaData->Offset >= (UINTN)DataPtr) &&
          ((UINTN)PcdDatabaseInit + DeltaData->Offset <  (UINTN)DataPtr + DataSize))
      {
        return TRUE;
      }
    }

    Offset  += SkuDelta->Length;
    Offset   = ALIGN_VALUE (Offset, 8);
    SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
  }

  return FALSE;
}

CHAR8 *
PcdTypeMaskToString (
  IN UINT32  Token
  )
{
  switch (Token & PCD_TYPE_ALL_SET_WITHOUT_SKU) {
    case PCD_TYPE_DATA:
      return "DATA";
    case PCD_TYPE_HII:
      return "HII";
    case PCD_TYPE_VPD:
      return "VPD";
    case PCD_TYPE_STRING:
      return "STRING";
    case PCD_TYPE_HII|PCD_TYPE_STRING:
      return "HII STRING";
  }

  return "<UnknownType>";
}

CHAR8 *
PcdDataTypeToString (
  IN UINT32  Token
  )
{
  switch (Token & PCD_DATUM_TYPE_ALL_SET) {
    case PCD_DATUM_TYPE_POINTER:
      return "POINTER";
    case PCD_DATUM_TYPE_UINT8:
      return "UINT8";
    case PCD_DATUM_TYPE_UINT16:
      return "UINT16";
    case PCD_DATUM_TYPE_UINT32:
      return "UINT32";
    case PCD_DATUM_TYPE_UINT64:
      return "UINT64";
  }

  return "<UnknownDataType>";
}

/**
  Get index of PCD entry in size table.

  @param LocalTokenNumberTableIdx Index of this PCD in local token number table.
  @param Database                 Pointer to PCD database in PEI phase.

  @return index of PCD entry in size table.

**/
UINTN
GetSizeTableIndex (
  IN    UINTN             LocalTokenNumberTableIdx,
  IN    PEI_PCD_DATABASE  *Database
  )
{
  UINTN  Index;
  UINTN  SizeTableIdx;
  UINTN  LocalTokenNumber;

  SizeTableIdx = 0;

  for (Index = 0; Index < LocalTokenNumberTableIdx; Index++) {
    LocalTokenNumber = *((UINT32 *)((UINT8 *)Database + Database->LocalTokenNumberTableOffset) + Index);

    if ((LocalTokenNumber & PCD_DATUM_TYPE_ALL_SET) == PCD_DATUM_TYPE_POINTER) {
      //
      // SizeTable only contain record for PCD_DATUM_TYPE_POINTER type
      // PCD entry.
      //
      if ((LocalTokenNumber & PCD_TYPE_VPD) != 0) {
        //
        // We have only two entry for VPD enabled PCD entry:
        // 1) MAX Size.
        // 2) Current Size
        // Current size is equal to MAX size.
        //
        SizeTableIdx += 2;
      } else {
        //
        // We have only two entry for Non-Sku enabled PCD entry:
        // 1) MAX SIZE
        // 2) Current Size
        //
        SizeTableIdx += 2;
      }
    }
  }

  return SizeTableIdx;
}

/**
  Get PCD value's size for POINTER type PCD.

  The POINTER type PCD's value will be stored into a buffer in specified size.
  The max size of this PCD's value is described in PCD's definition in DEC file.

  @param LocalTokenNumberTableIdx Index of PCD token number in PCD token table
  @param MaxSize                  Maximum size of PCD's value
  @param Database                 Pcd database in PEI phase.

  @return PCD value's size for POINTER type PCD.

**/
UINTN
GetPtrTypeSize (
  IN    UINTN             LocalTokenNumberTableIdx,
  OUT   UINTN             *MaxSize,
  IN    PEI_PCD_DATABASE  *Database
  )
{
  INTN       SizeTableIdx;
  UINTN      LocalTokenNumber;
  SIZE_INFO  *SizeTable;

  SizeTableIdx = GetSizeTableIndex (LocalTokenNumberTableIdx, Database);

  LocalTokenNumber = *((UINT32 *)((UINT8 *)Database + Database->LocalTokenNumberTableOffset) + LocalTokenNumberTableIdx);

  ASSERT ((LocalTokenNumber & PCD_DATUM_TYPE_ALL_SET) == PCD_DATUM_TYPE_POINTER);

  SizeTable = (SIZE_INFO *)((UINT8 *)Database + Database->SizeTableOffset);

  *MaxSize = SizeTable[SizeTableIdx];
  //
  // SizeTable only contain record for PCD_DATUM_TYPE_POINTER type
  // PCD entry.
  //
  if ((LocalTokenNumber & PCD_TYPE_VPD) != 0) {
    //
    // We have only two entry for VPD enabled PCD entry:
    // 1) MAX Size.
    // 2) Current Size
    // We consider current size is equal to MAX size.
    //
    return *MaxSize;
  } else {
    //
    // We have only two entry for Non-Sku enabled PCD entry:
    // 1) MAX SIZE
    // 2) Current Size
    //
    return SizeTable[SizeTableIdx + 1];
  }
}

VOID
DumpPcdRawData (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN UINTN              LocalTokenNumberTableIdx,
  IN UINT32             LocalTokenNumber,
  IN UINT8              *Data,
  OUT VOID              **DataPtr,
  OUT UINTN             *DataSize
  )
{
  UINTN    Size;
  UINTN    MaxSize;
  UINTN    Index;
  BOOLEAN  IsUninitialized;

  if ((UINTN)Data - (UINTN)PcdDatabaseInit >= PcdDatabaseInit->Length) {
    IsUninitialized = TRUE;
    *DataPtr        = NULL;
    *DataSize       = 0;
  } else {
    IsUninitialized = FALSE;
    *DataPtr        = Data;
    *DataSize       = 0;
  }

  switch (LocalTokenNumber & PCD_DATUM_TYPE_ALL_SET) {
    case PCD_DATUM_TYPE_POINTER:
      Size = GetPtrTypeSize (LocalTokenNumberTableIdx, &MaxSize, PcdDatabaseInit);
      Print (L"    DataSize    : %d\n", Size);
      Print (L"    DataMaxSize : %d\n", MaxSize);
      Print (L"    Data        : ");
      if (IsUninitialized) {
        Print (L"<Uninit>");
      } else {
        for (Index = 0; Index < Size; Index++) {
          Print (L"%02x", Data[Index]);
        }

        *DataSize = Size;
      }

      Print (L"\n");
      break;
    case PCD_DATUM_TYPE_UINT8:
      Print (L"    Data        : ");
      if (IsUninitialized) {
        Print (L"<Uninit>");
      } else {
        Print (L"0x%02x", *(UINT8 *)Data);
        *DataSize = sizeof (UINT8);
      }

      Print (L"\n");
      break;
    case PCD_DATUM_TYPE_UINT16:
      Print (L"    Data        : ");
      if (IsUninitialized) {
        Print (L"<Uninit>");
      } else {
        Print (L"0x%04x", *(UINT16 *)Data);
        *DataSize = sizeof (UINT16);
      }

      Print (L"\n");
      break;
    case PCD_DATUM_TYPE_UINT32:
      Print (L"    Data        : ");
      if (IsUninitialized) {
        Print (L"<Uninit>");
      } else {
        Print (L"0x%08x", *(UINT32 *)Data);
        *DataSize = sizeof (UINT32);
      }

      Print (L"\n");
      break;
    case PCD_DATUM_TYPE_UINT64:
      Print (L"    Data        : ");
      if (IsUninitialized) {
        Print (L"<Uninit>");
      } else {
        Print (L"0x%016lx", *(UINT64 *)Data);
        *DataSize = sizeof (UINT64);
      }

      Print (L"\n");
      break;
  }
}

BOOLEAN
CopySkuPcdData (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN SKU_ID             SkuId,
  IN VOID               *DataPtr,
  IN UINTN              DataSize,
  IN OUT VOID           *DataBuffer
  )
{
  PCD_DATABASE_SKU_DELTA  *SkuDelta;
  PCD_DATA_DELTA          *DeltaData;
  UINTN                   Offset;
  BOOLEAN                 Result;

  if (PcdDatabaseInit->LengthForAllSkus <= PcdDatabaseInit->Length) {
    return FALSE;
  }

  Result   = FALSE;
  Offset   = PcdDatabaseInit->Length;
  Offset   = ALIGN_VALUE (Offset, 8);
  SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
  while ((UINTN)SkuDelta < (UINTN)PcdDatabaseInit + PcdDatabaseInit->LengthForAllSkus) {
    if (SkuDelta->SkuId == SkuId) {
      for (DeltaData = (PCD_DATA_DELTA *)(SkuDelta + 1);
           (UINTN)DeltaData < (UINTN)SkuDelta + SkuDelta->Length;
           DeltaData++)
      {
        if (((UINTN)PcdDatabaseInit + DeltaData->Offset >= (UINTN)DataPtr) &&
            ((UINTN)PcdDatabaseInit + DeltaData->Offset <  (UINTN)DataPtr + DataSize))
        {
          *((UINT8 *)DataBuffer + ((UINTN)PcdDatabaseInit + DeltaData->Offset - (UINTN)DataPtr)) = (UINT8)DeltaData->Value;
          Result                                                                                 = TRUE;
        }
      }
    }

    Offset  += SkuDelta->Length;
    Offset   = ALIGN_VALUE (Offset, 8);
    SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
  }

  return Result;
}

VOID
DumpSkuPcdData (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN SKU_ID             SkuId,
  IN UINTN              LocalTokenNumberTableIdx,
  IN UINT32             LocalTokenNumber,
  IN VOID               *DataPtr,
  IN UINTN              DataSize
  )
{
  UINT8    *TempDataBuffer;
  UINT64   TempData;
  BOOLEAN  Result;
  UINTN    Index;

  switch (LocalTokenNumber & PCD_DATUM_TYPE_ALL_SET) {
    case PCD_DATUM_TYPE_POINTER:
      TempDataBuffer = AllocateCopyPool (DataSize, DataPtr);
      if (TempDataBuffer == NULL) {
        return;
      }

      Result = CopySkuPcdData (PcdDatabaseInit, SkuId, DataPtr, DataSize, TempDataBuffer);
      if (Result) {
        Print (L"    Data        : ");
        for (Index = 0; Index < DataSize; Index++) {
          Print (L"%02x", TempDataBuffer[Index]);
        }

        Print (L" (SKU - 0x%016lx)\n", SkuId);
      }

      FreePool (TempDataBuffer);
      break;
    case PCD_DATUM_TYPE_UINT8:
      ASSERT (DataSize == sizeof (UINT8));
      TempData = *(UINT8 *)DataPtr;
      Result   = CopySkuPcdData (PcdDatabaseInit, SkuId, DataPtr, DataSize, &TempData);
      if (Result) {
        Print (L"    Data        : ");
        Print (L"0x%02x", TempData);
        Print (L" (SKU - 0x%016lx)\n", SkuId);
      }

      break;
    case PCD_DATUM_TYPE_UINT16:
      ASSERT (DataSize == sizeof (UINT16));
      TempData = *(UINT16 *)DataPtr;
      Result   = CopySkuPcdData (PcdDatabaseInit, SkuId, DataPtr, DataSize, &TempData);
      if (Result) {
        Print (L"    Data        : ");
        Print (L"0x%04x", TempData);
        Print (L" (SKU - 0x%016lx)\n", SkuId);
      }

      break;
    case PCD_DATUM_TYPE_UINT32:
      ASSERT (DataSize == sizeof (UINT32));
      TempData = *(UINT32 *)DataPtr;
      Result   = CopySkuPcdData (PcdDatabaseInit, SkuId, DataPtr, DataSize, &TempData);
      if (Result) {
        Print (L"    Data        : ");
        Print (L"0x%08x", TempData);
        Print (L" (SKU - 0x%016lx)\n", SkuId);
      }

      break;
    case PCD_DATUM_TYPE_UINT64:
      ASSERT (DataSize == sizeof (UINT64));
      TempData = *(UINT64 *)DataPtr;
      Result   = CopySkuPcdData (PcdDatabaseInit, SkuId, DataPtr, DataSize, &TempData);
      if (Result) {
        Print (L"    Data        : ");
        Print (L"0x%016lx", TempData);
        Print (L" (SKU - 0x%016lx)\n", SkuId);
      }

      break;
  }
}

VOID
DumpAllSkuPcdData (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN SKU_ID             *SkuIdTable,
  IN UINTN              LocalTokenNumberTableIdx,
  IN UINT32             LocalTokenNumber,
  IN VOID               *DataPtr,
  IN UINTN              DataSize
  )
{
  UINTN  Index;

  for (Index = 1; Index <= SkuIdTable[0]; Index++) {
    DumpSkuPcdData (
      PcdDatabaseInit,
      SkuIdTable[Index],
      LocalTokenNumberTableIdx,
      LocalTokenNumber,
      DataPtr,
      DataSize
      );
  }
}

VOID
DumpPcdData (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN UINTN              LocalTokenNumberTableIdx,
  IN UINT32             LocalTokenNumber,
  OUT VOID              **DataPtr,
  OUT UINTN             *DataSize
  )
{
  UINTN          Offset;
  UINT8          *Data;
  VARIABLE_HEAD  *VariableHead;
  STRING_HEAD    StringTableIdx;
  UINT8          *StringTable;
  GUID           *GuidTable;
  GUID           *Guid;
  CHAR16         *Name;
  VPD_HEAD       *VpdHead;

  Data   = NULL;
  Offset = LocalTokenNumber & PCD_DATABASE_OFFSET_MASK;
  switch (LocalTokenNumber & PCD_TYPE_ALL_SET_WITHOUT_SKU) {
    case PCD_TYPE_DATA:
      Data = (UINT8 *)((UINTN)PcdDatabaseInit + Offset);
      break;
    case PCD_TYPE_STRING:
      StringTableIdx = *(STRING_HEAD *)((UINTN)PcdDatabaseInit + Offset);
      StringTable    = (UINT8 *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->StringTableOffset);
      Data           = (UINT8 *)(&StringTable[StringTableIdx]);
      break;
    case PCD_TYPE_HII|PCD_TYPE_STRING:
    case PCD_TYPE_HII:
      VariableHead = (VARIABLE_HEAD *)((UINTN)PcdDatabaseInit + Offset);
      GuidTable    = (GUID *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->GuidTableOffset);
      StringTable  = (UINT8 *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->StringTableOffset);
      Guid         = &GuidTable[VariableHead->GuidTableIndex];
      Name         = (UINT16 *)&StringTable[VariableHead->StringIndex];
      Print (L"    VarName     : %s\n", Name);
      Print (L"    VarGuid     : %g\n", Guid);
      Print (L"    Attributes  : 0x%08x\n", VariableHead->Attributes);
      Print (L"    Property    : 0x%04x\n", VariableHead->Property);
      Print (L"    DefaultValueOffset : 0x%04x\n", VariableHead->DefaultValueOffset);
      if ((LocalTokenNumber & PCD_TYPE_ALL_SET_WITHOUT_SKU) == (PCD_TYPE_HII|PCD_TYPE_STRING)) {
        //
        // If a HII type PCD's datum type is VOID*, the DefaultValueOffset is the index of
        // string array in string table.
        //
        StringTableIdx = *(STRING_HEAD *)((UINTN)PcdDatabaseInit + VariableHead->DefaultValueOffset);
        Data           = (UINT8 *)&StringTable[StringTableIdx];
      } else {
        Data = (UINT8 *)((UINTN)PcdDatabaseInit + VariableHead->DefaultValueOffset);
      }

      break;
    case PCD_TYPE_VPD:
      VpdHead = (VPD_HEAD *)((UINTN)PcdDatabaseInit + Offset);
      Print (L"    VpdBaseAddr : 0x%08x\n", PcdGet32 (PcdVpdBaseAddress));
      Print (L"    Offset      : 0x%08x\n", VpdHead->Offset);
      Data = (VOID *)(UINTN)(PcdGet32 (PcdVpdBaseAddress) + VpdHead->Offset);
      break;
  }

  if (Data != NULL) {
    DumpPcdRawData (PcdDatabaseInit, LocalTokenNumberTableIdx, LocalTokenNumber, Data, DataPtr, DataSize);
  } else {
    *DataPtr  = NULL;
    *DataSize = 0;
  }
}

VOID
DumpPcdDatabase (
  IN PCD_DATABASE_INIT  *PcdDatabaseInit,
  IN UINTN              PcdDatabaseSize,
  IN BOOLEAN            IsPei
  )
{
  UINTN                   Index;
  UINT32                  *LocalTokenNumberTable;
  DYNAMICEX_MAPPING       *ExMapTable;
  GUID                    *GuidTable;
  UINT8                   *StringTable;
  SKU_ID                  *SkuIdTable;
  PCD_NAME_INDEX          *PcdNameTable;
  UINT32                  LocalTokenNumber;
  PCD_DATABASE_SKU_DELTA  *SkuDelta;
  PCD_DATA_DELTA          *DeltaData;
  UINTN                   Offset;
  VOID                    *DataPtr;
  UINTN                   DataSize;

  if (!CompareGuid (&PcdDatabaseInit->Signature, &gPcdDataBaseSignatureGuid)) {
    Print (L"PCD database signature error - %g\n", &PcdDatabaseInit->Signature);
    return;
  }

  Print (L"(PcdDatabaseSize            - 0x%08x)\n", PcdDatabaseSize);
  Print (L"Signature                   - %g\n", &PcdDatabaseInit->Signature);
  Print (L"BuildVersion                - 0x%08x\n", PcdDatabaseInit->BuildVersion);
  Print (L"Length                      - 0x%08x\n", PcdDatabaseInit->Length);
  Print (L"SystemSkuId                 - 0x%016lx\n", PcdDatabaseInit->SystemSkuId);
  Print (L"LengthForAllSkus            - 0x%08x\n", PcdDatabaseInit->LengthForAllSkus);
  Print (L"UninitDataBaseSize          - 0x%08x\n", PcdDatabaseInit->UninitDataBaseSize);
  Print (L"LocalTokenNumberTableOffset - 0x%08x\n", PcdDatabaseInit->LocalTokenNumberTableOffset);
  Print (L"ExMapTableOffset            - 0x%08x\n", PcdDatabaseInit->ExMapTableOffset);
  Print (L"GuidTableOffset             - 0x%08x\n", PcdDatabaseInit->GuidTableOffset);
  Print (L"StringTableOffset           - 0x%08x\n", PcdDatabaseInit->StringTableOffset);
  Print (L"SizeTableOffset             - 0x%08x\n", PcdDatabaseInit->SizeTableOffset);
  Print (L"SkuIdTableOffset            - 0x%08x\n", PcdDatabaseInit->SkuIdTableOffset);
  Print (L"PcdNameTableOffset          - 0x%08x\n", PcdDatabaseInit->PcdNameTableOffset);
  Print (L"LocalTokenCount             - 0x%04x\n", PcdDatabaseInit->LocalTokenCount);
  Print (L"ExTokenCount                - 0x%04x\n", PcdDatabaseInit->ExTokenCount);
  Print (L"GuidTableCount              - 0x%04x\n", PcdDatabaseInit->GuidTableCount);
  Print (L"\n");

  LocalTokenNumberTable = (UINT32 *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->LocalTokenNumberTableOffset);
  ExMapTable            = (DYNAMICEX_MAPPING *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->ExMapTableOffset);
  GuidTable             = (GUID *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->GuidTableOffset);
  StringTable           = (UINT8 *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->StringTableOffset);
  SkuIdTable            = (SKU_ID *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->SkuIdTableOffset);
  PcdNameTable          = (PCD_NAME_INDEX *)((UINTN)PcdDatabaseInit + PcdDatabaseInit->PcdNameTableOffset);
  Print (L"SkuIdTable: (Count - %d)\n", SkuIdTable[0]);
  for (Index = 1; Index <= SkuIdTable[0]; Index++) {
    Print (L"  SkuId[%3d] - 0x%016lx\n", Index, SkuIdTable[Index]);
  }

  Print (L"\n");

  Print (L"LocalToken:\n");
  for (Index = 0; Index < PcdDatabaseInit->LocalTokenCount; Index++) {
    Print (L"  LocalToken[%3d]\n", Index);
    LocalTokenNumber = LocalTokenNumberTable[Index];

    if (IsPei) {
      Print (L"    TokenNumber          - 0x%08x\n", Index + 1);
    } else {
      Print (L"    TokenNumber          - 0x%08x\n", Index + 1 + mPcdPeiLocalTokenCount);
    }

    Print (L"    TokenValue           - 0x%08x", LocalTokenNumber);
    Print (L" (%a) (%a)\n", PcdTypeMaskToString (LocalTokenNumber), PcdDataTypeToString (LocalTokenNumber));

    if (PcdDatabaseInit->PcdNameTableOffset != 0) {
      Print (L"    TokenSpaceCNameIndex - 0x%08x", PcdNameTable[Index].TokenSpaceCNameIndex);
      Print (L" (%a)\n", &StringTable[PcdNameTable[Index].TokenSpaceCNameIndex]);
      Print (L"    PcdCNameIndex        - 0x%08x", PcdNameTable[Index].PcdCNameIndex);
      Print (L" (%a)\n", &StringTable[PcdNameTable[Index].PcdCNameIndex]);
    }

    DumpPcdData (PcdDatabaseInit, Index, LocalTokenNumber, &DataPtr, &DataSize);
    if (IsSkuPcd (PcdDatabaseInit, DataPtr, DataSize)) {
      DumpAllSkuPcdData (PcdDatabaseInit, SkuIdTable, Index, LocalTokenNumber, DataPtr, DataSize);
    }
  }

  Print (L"\n");

  Print (L"ExToken:\n");
  for (Index = 0; Index < PcdDatabaseInit->ExTokenCount; Index++) {
    Print (L"  ExToken[%3d]\n", Index);
    Print (L"    ExTokenNumber        - 0x%08x\n", ExMapTable[Index].ExTokenNumber);
    Print (L"    TokenNumber          - 0x%04x\n", ExMapTable[Index].TokenNumber);
    Print (L"    ExGuidIndex          - 0x%04x (%g)\n", ExMapTable[Index].ExGuidIndex, &GuidTable[ExMapTable[Index].ExGuidIndex]);
  }

  Print (L"\n");

  if (PcdDatabaseInit->LengthForAllSkus > PcdDatabaseInit->Length) {
    Print (L"PCD_DATABASE_SKU_DELTA:\n");
    Offset   = PcdDatabaseInit->Length;
    Offset   = ALIGN_VALUE (Offset, 8);
    SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
    while ((UINTN)SkuDelta < (UINTN)PcdDatabaseInit + PcdDatabaseInit->LengthForAllSkus) {
      Print (L"  SkuId           - 0x%016lx\n", SkuDelta->SkuId);
      Print (L"    SkuIdCompared - 0x%016lx\n", SkuDelta->SkuIdCompared);
      Print (L"    Length        - 0x%08x\n", SkuDelta->Length);
      for (DeltaData = (PCD_DATA_DELTA *)(SkuDelta + 1);
           (UINTN)DeltaData < (UINTN)SkuDelta + SkuDelta->Length;
           DeltaData++)
      {
        Print (L"    Delta.Offset  - 0x%06x\n", DeltaData->Offset);
        Print (L"    Delta.Value   - 0x%02x\n", DeltaData->Value);
      }

      Offset  += SkuDelta->Length;
      Offset   = ALIGN_VALUE (Offset, 8);
      SkuDelta = (PCD_DATABASE_SKU_DELTA *)((UINTN)PcdDatabaseInit + Offset);
    }
  }
}

VOID
DumpPeiPcdHob (
  VOID
  )
{
  EFI_HOB_GUID_TYPE  *GuidHob;
  VOID               *PeiPcdHob;
  UINT16             Size;

  Print (L"###############\n");
  Print (L"# PEI PCD HOB #\n");
  Print (L"###############\n");

  GuidHob = GetFirstGuidHob (&gPcdDataBaseHobGuid);
  if (GuidHob == NULL) {
    Print (L"PcdDataBaseHob - %r\n", EFI_NOT_FOUND);
    return;
  }

  PeiPcdHob = GET_GUID_HOB_DATA (GuidHob);
  Size      = GET_GUID_HOB_DATA_SIZE (GuidHob);
  //  InternalDumpHex (PeiPcdHob, Size);
  DumpPcdDatabase (PeiPcdHob, Size, TRUE);
}

UINTN
GetPcdPeiLocalTokenCount (
  VOID
  )
{
  EFI_HOB_GUID_TYPE  *GuidHob;
  VOID               *PeiPcdHob;
  PCD_DATABASE_INIT  *PcdDatabaseInit;

  GuidHob = GetFirstGuidHob (&gPcdDataBaseHobGuid);
  if (GuidHob == NULL) {
    return 0;
  }

  PeiPcdHob       = GET_GUID_HOB_DATA (GuidHob);
  PcdDatabaseInit = PeiPcdHob;
  return PcdDatabaseInit->LocalTokenCount;
}

VOID
DumpPcdBinary (
  IN CHAR16   *FileName,
  IN BOOLEAN  IsPei
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  UINT8       *Buffer;

  Print (L"#######################\n");
  Print (L"# PCD Database Binary #\n");
  Print (L"#######################\n");

  Status = ReadFileToBuffer (FileName, &Size, (VOID **)&Buffer);
  if (EFI_ERROR (Status)) {
    Print (L"File (%s) - %r\n", FileName, Status);
    return;
  }

  //  InternalDumpHex (Buffer, Size);
  DumpPcdDatabase ((PCD_DATABASE_INIT *)Buffer, Size, IsPei);
  FreePool (Buffer);
}

BOOLEAN
IsDataPcdDatabase (
  IN VOID   *Data,
  IN UINTN  DataSize
  )
{
  if (DataSize < sizeof (GUID)) {
    return FALSE;
  }

  if (!CompareGuid (Data, &gPcdDataBaseSignatureGuid)) {
    return FALSE;
  }

  return TRUE;
}

EFI_STATUS
LocateFvInstanceWithPcdImage (
  IN  EFI_GUID                       *ImageGuid,
  OUT EFI_FIRMWARE_VOLUME2_PROTOCOL  **Instance
  )
{
  EFI_STATUS                     Status;
  EFI_HANDLE                     *HandleBuffer;
  UINTN                          NumberOfHandles;
  EFI_FV_FILETYPE                FileType;
  UINT32                         FvStatus;
  EFI_FV_FILE_ATTRIBUTES         Attributes;
  UINTN                          Size;
  UINTN                          Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *FvInstance;

  FvStatus = 0;

  //
  // Locate protocol.
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  NULL,
                  &NumberOfHandles,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    //
    // Defined errors at this time are not found and out of resources.
    //
    return Status;
  }

  //
  // Looking for FV with PCD image file
  //
  for (Index = 0; Index < NumberOfHandles; Index++) {
    //
    // Get the protocol on this handle
    // This should not fail because of LocateHandleBuffer
    //
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiFirmwareVolume2ProtocolGuid,
                    (VOID **)&FvInstance
                    );
    ASSERT_EFI_ERROR (Status);

    //
    // See if it has the PCD image file
    //
    Status = FvInstance->ReadFile (
                           FvInstance,
                           ImageGuid,
                           NULL,
                           &Size,
                           &FileType,
                           &Attributes,
                           &FvStatus
                           );

    //
    // If we found it, then we are done
    //
    if (Status == EFI_SUCCESS) {
      *Instance = FvInstance;
      break;
    }
  }

  //
  // Our exit status is determined by the success of the previous operations
  // If the protocol was found, Instance already points to it.
  //

  //
  // Free any allocated buffers
  //
  FreePool (HandleBuffer);

  return Status;
}

EFI_STATUS
GetImagePcdDb (
  IN  EFI_GUID  *ImageGuid,
  OUT VOID      **Data,
  OUT UINTN     *DataSize
  )
{
  EFI_STATUS                     Status;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *FwVol;
  INTN                           Instance;
  VOID                           *Buffer;
  UINT32                         FvStatus;
  UINTN                          Size;

  //
  // Locate the firmware volume protocol
  //
  Status = LocateFvInstanceWithPcdImage (ImageGuid, &FwVol);
  if (EFI_ERROR (Status)) {
    return EFI_ABORTED;
  }

  //
  // Read data from the storage file.
  //
  Instance = 0;
  Buffer   = NULL;
  while (Status == EFI_SUCCESS) {
    Status = FwVol->ReadSection (
                      FwVol,
                      ImageGuid,
                      EFI_SECTION_RAW,
                      Instance,
                      (VOID **)&Buffer,
                      &Size,
                      &FvStatus
                      );
    if (!EFI_ERROR (Status)) {
      //
      // Data found
      //
      if (IsDataPcdDatabase (Buffer, Size)) {
        *Data     = Buffer;
        *DataSize = Size;
        return EFI_SUCCESS;
      }

      //
      // Free memory allocated by ReadSection
      //
      FreePool (Buffer);

      //
      // Increment the instance
      //
      Instance++;
      Buffer = NULL;
    }
  }

  return EFI_NOT_FOUND;
}

/**
  Check if a block of buffer is erased.

  @param[in] ErasePolarity  Erase polarity attribute of the firmware volume
  @param[in] InBuffer       The buffer to be checked
  @param[in] BufferSize     Size of the buffer in bytes

  @retval    TRUE           The block of buffer is erased
  @retval    FALSE          The block of buffer is not erased
**/
BOOLEAN
IsBufferErased (
  IN UINT8  ErasePolarity,
  IN VOID   *InBuffer,
  IN UINTN  BufferSize
  )
{
  UINTN  Count;
  UINT8  EraseByte;
  UINT8  *Buffer;

  if (ErasePolarity == 1) {
    EraseByte = 0xFF;
  } else {
    EraseByte = 0;
  }

  Buffer = InBuffer;
  for (Count = 0; Count < BufferSize; Count++) {
    if (Buffer[Count] != EraseByte) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Get Section buffer pointer by SectionType and SectionInstance.

  @param[in]   SectionBuffer     The buffer of section
  @param[in]   SectionBufferSize The size of SectionBuffer in bytes
  @param[in]   SectionType       The SectionType of Section to be found
  @param[in]   SectionInstance   The Instance of Section to be found
  @param[out]  OutSectionBuffer  The section found, including SECTION_HEADER
  @param[out]  OutSectionSize    The size of section found, including SECTION_HEADER

  @retval TRUE  The FFS buffer is found.
  @retval FALSE The FFS buffer is not found.
**/
BOOLEAN
GetSectionByType (
  IN VOID              *SectionBuffer,
  IN UINT32            SectionBufferSize,
  IN EFI_SECTION_TYPE  SectionType,
  IN UINTN             SectionInstance,
  OUT VOID             **OutSectionBuffer,
  OUT UINTN            *OutSectionSize
  )
{
  EFI_COMMON_SECTION_HEADER  *SectionHeader;
  UINTN                      SectionSize;
  UINTN                      Instance;

  DEBUG ((DEBUG_INFO, "GetSectionByType - Buffer: 0x%08x - 0x%08x\n", SectionBuffer, SectionBufferSize));

  //
  // Find Section
  //
  SectionHeader = SectionBuffer;

  Instance = 0;
  while ((UINTN)SectionHeader < (UINTN)SectionBuffer + SectionBufferSize) {
    DEBUG ((DEBUG_INFO, "GetSectionByType - Section: 0x%08x\n", SectionHeader));
    if (IS_SECTION2 (SectionHeader)) {
      SectionSize = SECTION2_SIZE (SectionHeader);
    } else {
      SectionSize = SECTION_SIZE (SectionHeader);
    }

    if (SectionHeader->Type == SectionType) {
      if (Instance == SectionInstance) {
        *OutSectionBuffer = (UINT8 *)SectionHeader;
        *OutSectionSize   = SectionSize;
        DEBUG ((DEBUG_INFO, "GetSectionByType - 0x%x - 0x%x\n", *OutSectionBuffer, *OutSectionSize));
        return TRUE;
      } else {
        DEBUG ((DEBUG_INFO, "GetSectionByType - find section instance %x\n", Instance));
        Instance++;
      }
    } else {
      //
      // Skip other section type
      //
      DEBUG ((DEBUG_INFO, "GetSectionByType - other section type 0x%x\n", SectionHeader->Type));
    }

    //
    // Next Section
    //
    SectionHeader = (EFI_COMMON_SECTION_HEADER *)((UINTN)SectionHeader + ALIGN_VALUE (SectionSize, 4));
  }

  return FALSE;
}

/**
  Get FFS buffer pointer by FileName GUID and FileType.

  @param[in]   FdStart          The System Firmware FD image
  @param[in]   FdSize           The size of System Firmware FD image
  @param[in]   FileName         The FileName GUID of FFS to be found
  @param[in]   Type             The FileType of FFS to be found
  @param[out]  OutFfsBuffer     The FFS buffer found, including FFS_FILE_HEADER
  @param[out]  OutFfsBufferSize The size of FFS buffer found, including FFS_FILE_HEADER

  @retval TRUE  The FFS buffer is found.
  @retval FALSE The FFS buffer is not found.
**/
BOOLEAN
GetFfsByName (
  IN VOID             *FdStart,
  IN UINTN            FdSize,
  IN EFI_GUID         *FileName,
  IN EFI_FV_FILETYPE  Type,
  OUT VOID            **OutFfsBuffer,
  OUT UINTN           *OutFfsBufferSize
  )
{
  UINTN                           FvSize;
  EFI_FIRMWARE_VOLUME_HEADER      *FvHeader;
  EFI_FIRMWARE_VOLUME_EXT_HEADER  *FvExtHeader;
  EFI_FFS_FILE_HEADER             *FfsHeader;
  UINT32                          FfsSize;
  UINTN                           TestLength;
  BOOLEAN                         FvFound;

  DEBUG ((DEBUG_INFO, "GetFfsByName - FV: 0x%08x - 0x%08x\n", (UINTN)FdStart, (UINTN)FdSize));

  FvFound  = FALSE;
  FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)FdStart;
  while ((UINTN)FvHeader < (UINTN)FdStart + FdSize - 1) {
    FvSize = (UINTN)FdStart + FdSize - (UINTN)FvHeader;

    if (FvHeader->Signature != EFI_FVH_SIGNATURE) {
      FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)((UINTN)FvHeader + SIZE_4KB);
      continue;
    }

    DEBUG ((DEBUG_ERROR, "checking FV....0x%08x - 0x%x\n", FvHeader, FvHeader->FvLength));
    FvFound = TRUE;
    if (FvHeader->FvLength > FvSize) {
      DEBUG ((DEBUG_ERROR, "GetFfsByName - FvSize: 0x%08x, MaxSize - 0x%08x\n", (UINTN)FvHeader->FvLength, (UINTN)FvSize));
      return FALSE;
    }

    FvSize = (UINTN)FvHeader->FvLength;

    //
    // Find FFS
    //
    if (FvHeader->ExtHeaderOffset != 0) {
      FvExtHeader = (EFI_FIRMWARE_VOLUME_EXT_HEADER *)((UINT8 *)FvHeader + FvHeader->ExtHeaderOffset);
      FfsHeader   = (EFI_FFS_FILE_HEADER *)((UINT8 *)FvExtHeader + FvExtHeader->ExtHeaderSize);
    } else {
      FfsHeader = (EFI_FFS_FILE_HEADER *)((UINT8 *)FvHeader + FvHeader->HeaderLength);
    }

    FfsHeader = (EFI_FFS_FILE_HEADER *)((UINTN)FvHeader + ALIGN_VALUE ((UINTN)FfsHeader - (UINTN)FvHeader, 8));

    while ((UINTN)FfsHeader < (UINTN)FvHeader + FvSize - 1) {
      DEBUG ((DEBUG_INFO, "GetFfsByName - FFS: 0x%08x\n", FfsHeader));
      TestLength = (UINTN)((UINTN)FvHeader + FvSize - (UINTN)FfsHeader);
      if (TestLength > sizeof (EFI_FFS_FILE_HEADER)) {
        TestLength = sizeof (EFI_FFS_FILE_HEADER);
      }

      if (IsBufferErased (1, FfsHeader, TestLength)) {
        break;
      }

      if (IS_FFS_FILE2 (FfsHeader)) {
        FfsSize = FFS_FILE2_SIZE (FfsHeader);
      } else {
        FfsSize = FFS_FILE_SIZE (FfsHeader);
      }

      if (CompareGuid (FileName, &FfsHeader->Name) &&
          ((Type == EFI_FV_FILETYPE_ALL) || (FfsHeader->Type == Type)))
      {
        //
        // Check section
        //
        *OutFfsBuffer     = FfsHeader;
        *OutFfsBufferSize = FfsSize;
        return TRUE;
      } else {
        //
        // Any other type is not allowed
        //
        DEBUG ((DEBUG_INFO, "GetFfsByName - other FFS type 0x%x, name %g\n", FfsHeader->Type, &FfsHeader->Name));
      }

      //
      // Next File
      //
      FfsHeader = (EFI_FFS_FILE_HEADER *)((UINTN)FfsHeader + ALIGN_VALUE (FfsSize, 8));
    }

    //
    // Next FV
    //
    FvHeader = (VOID *)(UINTN)((UINTN)FvHeader + FvHeader->FvLength);
    DEBUG ((DEBUG_ERROR, "Next FV....0x%08x - 0x%x\n", FvHeader, FvHeader->FvLength));
  }

  if (!FvFound) {
    DEBUG ((DEBUG_ERROR, "GetFfsByName - NO FV Found\n"));
  }

  return FALSE;
}

EFI_STATUS
GetImagePcdDbFromAddress (
  IN  EFI_GUID  *ImageGuid,
  IN  UINTN     FvAddress,
  OUT VOID      **Data,
  OUT UINTN     *DataSize
  )
{
  BOOLEAN                     Result;
  EFI_FIRMWARE_VOLUME_HEADER  *FvHeader;
  VOID                        *FfsData;
  UINTN                       FfsDataSize;
  UINT32                      FileHeaderSize;
  VOID                        *SectionData;
  UINTN                       SectionDataSize;
  UINT32                      SectionHeaderSize;
  UINTN                       Instance;

  FvHeader = (EFI_FIRMWARE_VOLUME_HEADER *)FvAddress;
  if (FvHeader->Signature != EFI_FVH_SIGNATURE) {
    return EFI_NOT_FOUND;
  }

  Result = GetFfsByName (FvHeader, (UINTN)FvHeader->FvLength, ImageGuid, EFI_FV_FILETYPE_ALL, &FfsData, &FfsDataSize);
  if (!Result) {
    return EFI_NOT_FOUND;
  }

  if (IS_FFS_FILE2 (FfsData)) {
    FileHeaderSize = sizeof (EFI_FFS_FILE_HEADER2);
  } else {
    FileHeaderSize = sizeof (EFI_FFS_FILE_HEADER);
  }

  FfsData     = (UINT8 *)FfsData + FileHeaderSize;
  FfsDataSize = FfsDataSize - FileHeaderSize;

  Instance = 0;
  while (TRUE) {
    Result = GetSectionByType (FfsData, (UINT32)FfsDataSize, EFI_SECTION_RAW, Instance, &SectionData, &SectionDataSize);
    if (!Result) {
      return EFI_NOT_FOUND;
    }

    if (IS_SECTION2 (SectionData)) {
      SectionHeaderSize = sizeof (EFI_RAW_SECTION2);
    } else {
      SectionHeaderSize = sizeof (EFI_RAW_SECTION);
    }

    SectionData     = (VOID *)((UINT8 *)SectionData + SectionHeaderSize);
    SectionDataSize = SectionDataSize - SectionHeaderSize;

    //
    // Data found
    //
    if (IsDataPcdDatabase (SectionData, SectionDataSize)) {
      *Data     = SectionData;
      *DataSize = SectionDataSize;
      return EFI_SUCCESS;
    }

    Instance++;
  }

  return EFI_NOT_FOUND;
}

VOID
DumpImagePcdDb (
  IN BOOLEAN  IsPei,
  IN UINTN    FvAddress
  )
{
  EFI_STATUS  Status;
  VOID        *Data;
  UINTN       DataSize;

  if (IsPei) {
    Print (L"########################\n");
    Print (L"# Image PCD PEI Binary #\n");
    Print (L"########################\n");
    if (FvAddress != 0) {
      Status = GetImagePcdDbFromAddress (&mPcdPeiDriverGuid, FvAddress, &Data, &DataSize);
    } else {
      Status = GetImagePcdDb (&mPcdPeiDriverGuid, &Data, &DataSize);
    }

    if (EFI_ERROR (Status)) {
      Print (L"PcdPei section not found in FV.\n");
    } else {
      // InternalDumpHex (Data, DataSize);
      DumpPcdDatabase (Data, DataSize, TRUE);
    }
  } else {
    Print (L"########################\n");
    Print (L"# Image PCD DXE Binary #\n");
    Print (L"########################\n");
    if (FvAddress != 0) {
      Status = GetImagePcdDbFromAddress (&mPcdDxeDriverGuid, FvAddress, &Data, &DataSize);
    } else {
      Status = GetImagePcdDb (&mPcdDxeDriverGuid, &Data, &DataSize);
    }

    if (EFI_ERROR (Status)) {
      Print (L"PcdDxe section not found in FV.\n");
    } else {
      // InternalDumpHex (Data, DataSize);
      DumpPcdDatabase (Data, DataSize, FALSE);
    }
  }
}

/**
  Get PCD type string based on input PCD type.

  @param[in]    PcdType         The input PCD type.

  @return       Pointer to PCD type string.
**/
CHAR16 *
GetPcdTypeString (
  IN EFI_PCD_TYPE  PcdType
  )
{
  switch (PcdType) {
    case EFI_PCD_TYPE_8:
      return L"UINT8";
    case EFI_PCD_TYPE_16:
      return L"UINT16";
    case EFI_PCD_TYPE_32:
      return L"UINT32";
    case EFI_PCD_TYPE_64:
      return L"UINT64";
    case EFI_PCD_TYPE_BOOL:
      return L"BOOLEAN";
    case EFI_PCD_TYPE_PTR:
      return L"POINTER";
    default:
      return L"<UNKNOWN>";
  }
}

/**
  Get PCD Token Space string based on input PCD Token Space.

  @param[in]    TokenSpace      PCD Token Space.

  @return       Pointer to PCD Token Space string.
**/
CHAR16 *
GetPcdTokenSpaceString (
  IN CONST EFI_GUID  *TokenSpace
  )
{
  if (TokenSpace == NULL) {
    return L"DYNAMIC";
  } else {
    return L"DYNAMICEX";
  }
}

/**
  Dump PCD info.

  @param[in]    TokenSpace      PCD Token Space.
  @param[in]    TokenNumber     PCD Token Number.
  @param[in]    PcdInfo         Pointer to PCD info.
**/
VOID
DumpPcdInfo (
  IN UINTN           PcdInfoIndex,
  IN CONST EFI_GUID  *TokenSpace,
  IN UINTN           TokenNumber,
  IN EFI_PCD_INFO    *PcdInfo
  )
{
  CHAR16   *TypeString;
  UINT8    Uint8;
  UINT16   Uint16;
  UINT32   Uint32;
  UINT64   Uint64;
  BOOLEAN  Boolean;
  VOID     *PcdData;

  if (TokenSpace == NULL) {
    Print (L"  LocalToken[%3d]\n", PcdInfoIndex);
  } else {
    Print (L"  ExToken[%3d]\n", PcdInfoIndex);
  }

  Print (L"    TokenNumber          - 0x%08x\n", TokenNumber);
  TypeString = GetPcdTypeString (PcdInfo->PcdType);
  Print (L"    TokenType            - %s\n", TypeString);
  if (TokenSpace != NULL) {
    Print (L"    TokenSpace           - %g\n", TokenSpace);
  }

  if (PcdInfo->PcdName != NULL) {
    Print (L"    PcdName              - %a\n", PcdInfo->PcdName);
  }

  switch (PcdInfo->PcdType) {
    case EFI_PCD_TYPE_8:
      if (TokenSpace == NULL) {
        Uint8 = mPcd->Get8 (TokenNumber);
      } else {
        Uint8 = mPiPcd->Get8 (TokenSpace, TokenNumber);
      }

      Print (L"    Data        : 0x%02x\n", Uint8);
      break;
    case EFI_PCD_TYPE_16:
      if (TokenSpace == NULL) {
        Uint16 = mPcd->Get16 (TokenNumber);
      } else {
        Uint16 = mPiPcd->Get16 (TokenSpace, TokenNumber);
      }

      Print (L"    Data        : 0x%04x\n", Uint16);
      break;
    case EFI_PCD_TYPE_32:
      if (TokenSpace == NULL) {
        Uint32 = mPcd->Get32 (TokenNumber);
      } else {
        Uint32 = mPiPcd->Get32 (TokenSpace, TokenNumber);
      }

      Print (L"    Data        : 0x%08x\n", Uint32);
      break;
    case EFI_PCD_TYPE_64:
      if (TokenSpace == NULL) {
        Uint64 = mPcd->Get64 (TokenNumber);
      } else {
        Uint64 = mPiPcd->Get64 (TokenSpace, TokenNumber);
      }

      Print (L"    Data        : 0x%016lx\n", Uint64);
      break;
    case EFI_PCD_TYPE_BOOL:
      if (TokenSpace == NULL) {
        Boolean = mPcd->GetBool (TokenNumber);
      } else {
        Boolean = mPiPcd->GetBool (TokenSpace, TokenNumber);
      }

      Print (L"    Data        : %a\n", Boolean ? "TRUE" : "FALSE");
      break;
    case EFI_PCD_TYPE_PTR:
      if (TokenSpace == NULL) {
        PcdData = mPcd->GetPtr (TokenNumber);
      } else {
        PcdData = mPiPcd->GetPtr (TokenSpace, TokenNumber);
      }

      Print (L"    DataSize    : %d\n", PcdInfo->PcdSize);
      Print (L"    Data        : ");
      InternalDumpData (PcdData, PcdInfo->PcdSize);
      Print (L"\n");
      break;
    default:
      return;
  }
}

VOID
DumpDxePcdProtocolInfo (
  VOID
  )
{
  EFI_STATUS    Status;
  EFI_GUID      *TokenSpace;
  UINTN         TokenNumber;
  EFI_PCD_INFO  PcdInfo;
  UINTN         PcdInfoIndex;

  Status = gBS->LocateProtocol (&gEfiPcdProtocolGuid, NULL, (VOID **)&mPiPcd);
  if (EFI_ERROR (Status)) {
    Print (L"PI PCD protocol - %r\n", Status);
    return;
  }

  Status = gBS->LocateProtocol (&gEfiGetPcdInfoProtocolGuid, NULL, (VOID **)&mPiPcdInfo);
  if (EFI_ERROR (Status)) {
    Print (L"PI PCD info protocol - %r\n", Status);
    return;
  }

  Status = gBS->LocateProtocol (&gPcdProtocolGuid, NULL, (VOID **)&mPcd);
  if (EFI_ERROR (Status)) {
    Print (L"PCD protocol - %r\n", Status);
    return;
  }

  Status = gBS->LocateProtocol (&gGetPcdInfoProtocolGuid, NULL, (VOID **)&mPcdInfo);
  if (EFI_ERROR (Status)) {
    Print (L"PCD info protocol - %r\n", Status);
    return;
  }

  Print (L"####################\n");
  Print (L"# DXE PCD Protocol #\n");
  Print (L"####################\n");

  PcdInfo.PcdName = NULL;
  PcdInfo.PcdSize = 0;
  PcdInfo.PcdType = 0xFF;

  TokenSpace   = NULL;
  PcdInfoIndex = 0;
  do {
    TokenNumber = 0;
    if (TokenSpace == NULL) {
      Print (L"LocalToken:\n");
    }

    do {
      Status = mPiPcd->GetNextToken (TokenSpace, &TokenNumber);
      if (!EFI_ERROR (Status) && (TokenNumber != 0)) {
        if (TokenSpace == NULL) {
          //
          // PCD in default Token Space.
          //
          mPcdInfo->GetInfo (TokenNumber, &PcdInfo);
        } else {
          mPiPcdInfo->GetInfo (TokenSpace, TokenNumber, &PcdInfo);
        }

        DumpPcdInfo (PcdInfoIndex++, TokenSpace, TokenNumber, &PcdInfo);
      }
    } while (!EFI_ERROR (Status) && TokenNumber != 0);

    if (TokenSpace == NULL) {
      Print (L"ExToken:\n");
      PcdInfoIndex = 0;
    }

    Status = mPiPcd->GetNextTokenSpace ((CONST EFI_GUID **)&TokenSpace);
  } while (!EFI_ERROR (Status) && TokenSpace != NULL);
}

VARIABLE_STORE_HEADER *
GetVariableStoreHeaderFromNvStore (
  IN PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer
  )
{
  PCD_DEFAULT_DATA       *PcdDefaultData;
  UINTN                  Offset;
  VARIABLE_STORE_HEADER  *VariableStoreHeader;

  if (NvStoreDefaultBuffer->Signature != PCD_NV_STORE_DEFAULT_BUFFER_SIGNATURE) {
    return NULL;
  }

  Offset = sizeof (PCD_NV_STORE_DEFAULT_BUFFER_HEADER);

  PcdDefaultData = (VOID *)(NvStoreDefaultBuffer + 1);

  Offset              = Offset + PcdDefaultData->HeaderSize;
  Offset              = ALIGN_VALUE (Offset, 8);
  VariableStoreHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);

  return VariableStoreHeader;
}

VOID *
GetSkuDataFromNvStore (
  IN PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer
  )
{
  PCD_DEFAULT_DATA               *PcdDefaultData;
  UINTN                          Offset;
  VARIABLE_STORE_HEADER          *VariableStoreHeader;
  VARIABLE_HEADER                *VariableHeader;
  AUTHENTICATED_VARIABLE_HEADER  *AuthVariableHeader;

  VariableStoreHeader = GetVariableStoreHeaderFromNvStore (NvStoreDefaultBuffer);
  if (VariableStoreHeader == NULL) {
    return NULL;
  }

  PcdDefaultData = (VOID *)(NvStoreDefaultBuffer + 1);

  Offset = (UINTN)VariableStoreHeader - (UINTN)NvStoreDefaultBuffer;

  Offset = Offset + sizeof (VARIABLE_STORE_HEADER);
  Offset = ALIGN_VALUE (Offset, 4);
  if (CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid)) {
    while (Offset < PcdDefaultData->DataSize) {
      VariableHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Offset         = Offset + sizeof (VARIABLE_HEADER);
      Offset         = Offset + VariableHeader->NameSize;
      Offset         = Offset + VariableHeader->DataSize;
      Offset         = ALIGN_VALUE (Offset, 4);
    }
  } else if (CompareGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid)) {
    while (Offset < PcdDefaultData->DataSize) {
      AuthVariableHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Offset             = Offset + sizeof (AUTHENTICATED_VARIABLE_HEADER);
      Offset             = Offset + AuthVariableHeader->NameSize;
      Offset             = Offset + AuthVariableHeader->DataSize;
      Offset             = ALIGN_VALUE (Offset, 4);
    }
  }

  Offset = ALIGN_VALUE (Offset, 8);
  if (Offset < NvStoreDefaultBuffer->Length) {
    return (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
  }

  return NULL;
}

BOOLEAN
HasSkuData (
  IN PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer,
  IN VOID                                *DataPtr,
  IN UINTN                               DataSize
  )
{
  PCD_DEFAULT_DATA       *PcdDefaultData;
  UINTN                  Offset;
  PCD_DATA_DELTA         *DeltaData;
  BOOLEAN                Result;
  VARIABLE_STORE_HEADER  *VariableStoreHeader;

  PcdDefaultData = GetSkuDataFromNvStore (NvStoreDefaultBuffer);
  if (PcdDefaultData == NULL) {
    return FALSE;
  }

  VariableStoreHeader = GetVariableStoreHeaderFromNvStore (NvStoreDefaultBuffer);

  Result = FALSE;
  Offset = (UINTN)PcdDefaultData - (UINTN)NvStoreDefaultBuffer;

  while (Offset < NvStoreDefaultBuffer->Length) {
    PcdDefaultData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);

    Offset = Offset + sizeof (PcdDefaultData->DataSize) + PcdDefaultData->HeaderSize;
    Offset = ALIGN_VALUE (Offset, 4);
    for (DeltaData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
         (UINTN)DeltaData < (UINTN)PcdDefaultData + PcdDefaultData->DataSize;
         DeltaData++)
    {
      if ((((UINTN)VariableStoreHeader + DeltaData->Offset) >= (UINTN)DataPtr) &&
          (((UINTN)VariableStoreHeader + DeltaData->Offset) < (UINTN)DataPtr + DataSize))
      {
        Result = TRUE;
      }
    }

    Offset = (UINTN)DeltaData - (UINTN)NvStoreDefaultBuffer;
    Offset = ALIGN_VALUE (Offset, 8);
  }

  return Result;
}

VOID
DumpAllSkuData (
  IN PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer,
  IN VOID                                *DataPtr,
  IN UINTN                               DataSize
  )
{
  PCD_DEFAULT_DATA       *PcdDefaultData;
  PCD_DEFAULT_INFO       *DefaultInfo;
  UINTN                  Index;
  UINTN                  Offset;
  PCD_DATA_DELTA         *DeltaData;
  VARIABLE_STORE_HEADER  *VariableStoreHeader;
  UINT8                  *TempData;
  BOOLEAN                Result;

  PcdDefaultData = GetSkuDataFromNvStore (NvStoreDefaultBuffer);
  if (PcdDefaultData == NULL) {
    return;
  }

  VariableStoreHeader = GetVariableStoreHeaderFromNvStore (NvStoreDefaultBuffer);

  Offset = (UINTN)PcdDefaultData - (UINTN)NvStoreDefaultBuffer;

  while (Offset < NvStoreDefaultBuffer->Length) {
    PcdDefaultData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
    DefaultInfo    = PcdDefaultData->DefaultInfo;

    Result   = FALSE;
    TempData = AllocateCopyPool (DataSize, DataPtr);
    if (TempData == NULL) {
      return;
    }

    Offset = Offset + sizeof (PcdDefaultData->DataSize) + PcdDefaultData->HeaderSize;
    Offset = ALIGN_VALUE (Offset, 4);
    for (DeltaData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
         (UINTN)DeltaData < (UINTN)PcdDefaultData + PcdDefaultData->DataSize;
         DeltaData++)
    {
      if ((((UINTN)VariableStoreHeader + DeltaData->Offset) >= (UINTN)DataPtr) &&
          (((UINTN)VariableStoreHeader + DeltaData->Offset) < (UINTN)DataPtr + DataSize))
      {
        *((UINT8 *)TempData + ((UINTN)VariableStoreHeader + DeltaData->Offset - (UINTN)DataPtr)) = (UINT8)DeltaData->Value;
        Result                                                                                   = TRUE;
      }
    }

    if (Result) {
      Print (L"    Data       - ");
      for (Index = 0; Index < DataSize; Index++) {
        Print (L"%02x", TempData[Index]);
      }

      Print (L" (SKU:0x%016lx) (StoreId:0x%04x)", DefaultInfo->SkuId, DefaultInfo->DefaultId);
      Print (L"\n");
    }

    FreePool (TempData);

    Offset = (UINTN)DeltaData - (UINTN)NvStoreDefaultBuffer;
    Offset = ALIGN_VALUE (Offset, 8);
  }

  return;
}

VOID
DumpNvStore (
  IN PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer,
  IN UINTN                               NvStoreDefaultBufferSize
  )
{
  PCD_DEFAULT_DATA               *PcdDefaultData;
  UINTN                          Index;
  UINTN                          Count;
  PCD_DEFAULT_INFO               *DefaultInfo;
  UINTN                          Offset;
  VARIABLE_STORE_HEADER          *VariableStoreHeader;
  VARIABLE_HEADER                *VariableHeader;
  AUTHENTICATED_VARIABLE_HEADER  *AuthVariableHeader;
  UINT8                          *Data;
  PCD_DATA_DELTA                 *DeltaData;
  BOOLEAN                        HasDelta;

  Print (L"###########################\n");
  Print (L"# NV_STORE_DEFAULT_BUFFER #\n");
  Print (L"###########################\n");

  if (NvStoreDefaultBuffer->Signature != PCD_NV_STORE_DEFAULT_BUFFER_SIGNATURE) {
    Print (L"NV_STORE_DEFAULT_BUFFER database signature error - 0x%08x\n", NvStoreDefaultBuffer->Signature);
    return;
  }

  Print (L"(NvStoreDefaultBufferSize   - 0x%08x)\n", NvStoreDefaultBufferSize);
  Print (L"Signature                   - 0x%08x\n", NvStoreDefaultBuffer->Signature);
  Print (L"Length                      - 0x%08x\n", NvStoreDefaultBuffer->Length);
  Print (L"MaxLength                   - 0x%08x\n", NvStoreDefaultBuffer->MaxLength);

  Offset = sizeof (PCD_NV_STORE_DEFAULT_BUFFER_HEADER);

  PcdDefaultData = (VOID *)(NvStoreDefaultBuffer + 1);
  Print (L"PcdDefaultData:\n");
  Print (L"  DataSize                  - 0x%08x\n", PcdDefaultData->DataSize);
  Print (L"  HeaderSize                - 0x%08x\n", PcdDefaultData->HeaderSize);
  Count       = (PcdDefaultData->HeaderSize - sizeof (PcdDefaultData->HeaderSize)) / sizeof (PCD_DEFAULT_INFO);
  DefaultInfo = PcdDefaultData->DefaultInfo;
  for (Index = 0; Index < Count; Index++) {
    Print (L"  DefaultInfo[%d]:\n", Index);
    Print (L"    SkuId                   - 0x%016lx\n", DefaultInfo->SkuId);
    Print (L"    DefaultId               - 0x%04x\n", DefaultInfo->DefaultId);
  }

  Offset              = Offset + PcdDefaultData->HeaderSize;
  Offset              = ALIGN_VALUE (Offset, 8);
  VariableStoreHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
  Print (L"VariableStoreHeader:\n");
  Print (L"  Signature    - %g\n", &VariableStoreHeader->Signature);
  Print (L"  Size         - 0x%08x\n", VariableStoreHeader->Size);
  Print (L"  Format       - 0x%02x\n", VariableStoreHeader->Format);
  Print (L"  State        - 0x%02x\n", VariableStoreHeader->State);
  Offset = Offset + sizeof (VARIABLE_STORE_HEADER);
  Offset = ALIGN_VALUE (Offset, 4);
  if (CompareGuid (&VariableStoreHeader->Signature, &gEfiVariableGuid)) {
    Print (L"(Normal Variable)\n");
    while (Offset < PcdDefaultData->DataSize) {
      VariableHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Print (L"  VariableHeader:\n");
      Print (L"    StartId    - 0x%04x\n", VariableHeader->StartId);
      Print (L"    State      - 0x%02x\n", VariableHeader->State);
      Print (L"    Attributes - 0x%08x\n", VariableHeader->Attributes);
      Print (L"    NameSize   - 0x%08x\n", VariableHeader->NameSize);
      Print (L"    DataSize   - 0x%08x\n", VariableHeader->DataSize);
      Print (L"    VendorGuid - %g\n", &VariableHeader->VendorGuid);
      Offset = Offset + sizeof (VARIABLE_HEADER);
      Print (L"    VendorName - %s\n", (VOID *)((UINTN)NvStoreDefaultBuffer + Offset));
      Offset = Offset + VariableHeader->NameSize;
      Data   = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Print (L"    Data       - ");
      for (Index = 0; Index < VariableHeader->DataSize; Index++) {
        Print (L"%02x", Data[Index]);
      }

      HasDelta = HasSkuData (NvStoreDefaultBuffer, Data, VariableHeader->DataSize);
      if (HasDelta) {
        Print (L" (SKU:Default) (StoreId:Default)");
      }

      Print (L"\n");
      if (HasDelta) {
        DumpAllSkuData (NvStoreDefaultBuffer, Data, VariableHeader->DataSize);
      }

      Offset = Offset + VariableHeader->DataSize;
      Offset = ALIGN_VALUE (Offset, 4);
    }
  } else if (CompareGuid (&VariableStoreHeader->Signature, &gEfiAuthenticatedVariableGuid)) {
    Print (L"(Auth Variable)\n");
    while (Offset < PcdDefaultData->DataSize) {
      AuthVariableHeader = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Print (L"  VariableHeader: %x\n", Offset);
      Print (L"    StartId    - 0x%04x\n", AuthVariableHeader->StartId);
      Print (L"    State      - 0x%02x\n", AuthVariableHeader->State);
      Print (L"    Attributes - 0x%08x\n", AuthVariableHeader->Attributes);
      Print (L"  * MonotonicCount - 0x%016lx\n", AuthVariableHeader->MonotonicCount);
      Print (L"  * TimeStamp      - %t\n", &AuthVariableHeader->TimeStamp);
      Print (L"  * PubKeyIndex    - 0x%08x\n", AuthVariableHeader->PubKeyIndex);
      Print (L"    NameSize   - 0x%08x\n", AuthVariableHeader->NameSize);
      Print (L"    DataSize   - 0x%08x\n", AuthVariableHeader->DataSize);
      Print (L"    VendorGuid - %g\n", &AuthVariableHeader->VendorGuid);
      Offset = Offset + sizeof (AUTHENTICATED_VARIABLE_HEADER);
      Print (L"    VendorName - %s\n", (VOID *)((UINTN)NvStoreDefaultBuffer + Offset));
      Offset = Offset + AuthVariableHeader->NameSize;
      Data   = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
      Print (L"    Data       - ");
      for (Index = 0; Index < AuthVariableHeader->DataSize; Index++) {
        Print (L"%02x", Data[Index]);
      }

      HasDelta = HasSkuData (NvStoreDefaultBuffer, Data, AuthVariableHeader->DataSize);
      if (HasDelta) {
        Print (L" (SKU:Default) (StoreId:Default)");
      }

      Print (L"\n");
      if (HasDelta) {
        DumpAllSkuData (NvStoreDefaultBuffer, Data, AuthVariableHeader->DataSize);
      }

      Offset = Offset + AuthVariableHeader->DataSize;
      Offset = ALIGN_VALUE (Offset, 4);
    }
  }

  Offset = ALIGN_VALUE (Offset, 8);
  while (Offset < NvStoreDefaultBuffer->Length) {
    PcdDefaultData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
    Print (L"PcdDefaultData:\n");
    Print (L"  DataSize                  - 0x%08x\n", PcdDefaultData->DataSize);
    Print (L"  HeaderSize                - 0x%08x\n", PcdDefaultData->HeaderSize);
    Count       = (PcdDefaultData->HeaderSize - sizeof (PcdDefaultData->HeaderSize)) / sizeof (PCD_DEFAULT_INFO);
    DefaultInfo = PcdDefaultData->DefaultInfo;
    for (Index = 0; Index < Count; Index++) {
      Print (L"  DefaultInfo[%d]:\n", Index);
      Print (L"    SkuId                   - 0x%016lx\n", DefaultInfo->SkuId);
      Print (L"    DefaultId               - 0x%04x\n", DefaultInfo->DefaultId);
    }

    Offset = Offset + sizeof (PcdDefaultData->DataSize) + PcdDefaultData->HeaderSize;
    Offset = ALIGN_VALUE (Offset, 4);
    Print (L"PCD_DATA_DELTA:\n");
    for (DeltaData = (VOID *)((UINTN)NvStoreDefaultBuffer + Offset);
         (UINTN)DeltaData < (UINTN)PcdDefaultData + PcdDefaultData->DataSize;
         DeltaData++)
    {
      Print (L"    Delta.Offset  - 0x%06x\n", DeltaData->Offset);
      Print (L"    Delta.Value   - 0x%02x\n", DeltaData->Value);
    }

    Offset = (UINTN)DeltaData - (UINTN)NvStoreDefaultBuffer;
    Offset = ALIGN_VALUE (Offset, 8);
  }
}

VOID
DumpNvStoreFromVpd (
  IN CHAR16  *FileName
  )
{
  EFI_STATUS                          Status;
  UINTN                               Size;
  UINT8                               *Buffer;
  UINTN                               Index;
  PCD_NV_STORE_DEFAULT_BUFFER_HEADER  *NvStoreDefaultBuffer;

  Status = ReadFileToBuffer (FileName, &Size, (VOID **)&Buffer);
  if (EFI_ERROR (Status)) {
    Print (L"File (%s) - %r\n", FileName, Status);
    return;
  }

  if (Size <= 4) {
    Print (L"Invalid File\n", FileName);
    return;
  }

  for (Index = 0; Index < Size - 4; Index++) {
    NvStoreDefaultBuffer = (VOID *)(Buffer + Index);
    if (NvStoreDefaultBuffer->Signature == PCD_NV_STORE_DEFAULT_BUFFER_SIGNATURE) {
      DumpNvStore (NvStoreDefaultBuffer, NvStoreDefaultBuffer->Length);
    }
  }

  FreePool (Buffer);
}

/**
  Print APP usage.
**/
VOID
PrintUsage (
  VOID
  )
{
  Print (L"PcdDump:  usage\n");
  Print (L"  PcdDump -DB(PEI) [FvAddress]\n");
  Print (L"  PcdDump -DB(DxE)\n");
  Print (L"  PcdDump -HOB\n");
  Print (L"  PcdDump -PROTOCOL\n");
  Print (L"  PcdDump -FD <FdFile>\n");
  Print (L"  PcdDump -BIN(PEI) <PeiPcdDatabaseFile>\n");
  Print (L"  PcdDump -BIN(DXE) <DxePcdDatabaseFile>\n");
  Print (L"  PcdDump -BIN(NvStore) <VpdDatabaseFile>\n");
  Print (L"Parameter:\n");
  Print (L"  -DB:       Dump PcdDatabase file in current image.\n");
  Print (L"  -HOB:      Dump PEI Hob based PcdDatabase\n");
  Print (L"  -PROTOCOL: Dump PCD information based upon protocol.\n");
  Print (L"  -FD:       Dump PDB in the input FD file.\n");
  Print (L"  -BIN:      Dump PDB in the input database binary file, such as:\n");
  Print (L"             PEI: IA32/MdeModulePkg/Universal/PCD/Pei/Pcd/OUTPUT/PEIPcdDataBase.raw\n");
  Print (L"             DXE: X64/MdeModulePkg/Universal/PCD/Dxe/Pcd/OUTPUT/DXEPcdDataBase.raw\n");
  Print (L"             VPD: FV/8C3D856A-9BE6-468E-850A-24F7A8D38E08.bin\n");
  // Some other way to check PCD database:
  // 1. Build report: build -y report.log
  // 2. AutoGen.c
  //    Build\<Package>\<Target>_<Toolchain>\<Arch>\MdeModulePkg\Universal\PCD\Pei\Pcd\DEBUG\AutoGen.c
  //    Build\<Package>\<Target>_<Toolchain>\<Arch>\MdeModulePkg\Universal\PCD\Dxe\Pcd\DEBUG\AutoGen.c
}

EFI_STATUS
EFIAPI
PcdDumpEntrypoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       FvAddress;

  Status = GetArg ();
  if (EFI_ERROR (Status)) {
    Print (L"Please use UEFI SHELL to run this application!\n");
    return Status;
  }

  if (Argc < 2) {
    PrintUsage ();
    return EFI_INVALID_PARAMETER;
  }

  mPcdPeiLocalTokenCount = GetPcdPeiLocalTokenCount ();

  if (StrCmp (Argv[1], L"-HOB") == 0) {
    DumpPeiPcdHob ();
    return EFI_SUCCESS;
  }

  if (StrCmp (Argv[1], L"-PROTOCOL") == 0) {
    DumpDxePcdProtocolInfo ();
    return EFI_SUCCESS;
  }

  if ((StrCmp (Argv[1], L"-BIN(PEI)") == 0) && (Argc == 3)) {
    DumpPcdBinary (Argv[2], TRUE);
    return EFI_SUCCESS;
  }

  if ((StrCmp (Argv[1], L"-BIN(DXE)") == 0) && (Argc == 3)) {
    DumpPcdBinary (Argv[2], FALSE);
    return EFI_SUCCESS;
  }

  if (StrCmp (Argv[1], L"-DB(PEI)") == 0) {
    FvAddress = 0;
    if (Argc == 3) {
      FvAddress = StrHexToUintn (Argv[2]);
    }

    DumpImagePcdDb (TRUE, FvAddress);
    return EFI_SUCCESS;
  }

  if (StrCmp (Argv[1], L"-DB(DXE)") == 0) {
    DumpImagePcdDb (FALSE, 0);
    return EFI_SUCCESS;
  }

  if ((StrCmp (Argv[1], L"-BIN(NvStore)") == 0) && (Argc == 3)) {
    DumpNvStoreFromVpd (Argv[2]);
    return EFI_SUCCESS;
  }

  Print (L"Unknown Option - %S\n", Argv[1]);
  PrintUsage ();
  return EFI_INVALID_PARAMETER;
}
