/** @file

  Copyright (c) 2004  - 2019, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

Module Name:


  BootMode.c

Abstract:

  EFI PEIM to provide the platform support functionality on the Thurley.


--*/
#include "CommonHeader.h"
#include "Platform.h"
#include "PlatformBaseAddresses.h"
#include "PchAccess.h"
#include "PlatformBootMode.h"
#include <Guid/SetupVariable.h>

//
// Priority of our boot modes, highest priority first
//
EFI_BOOT_MODE mBootModePriority[] = {
  BOOT_IN_RECOVERY_MODE,
  BOOT_WITH_DEFAULT_SETTINGS,
  BOOT_ON_FLASH_UPDATE,
  BOOT_ON_S2_RESUME,
  BOOT_ON_S3_RESUME,
  BOOT_ON_S4_RESUME,
  BOOT_WITH_MINIMAL_CONFIGURATION,
  BOOT_ASSUMING_NO_CONFIGURATION_CHANGES,
  BOOT_WITH_FULL_CONFIGURATION_PLUS_DIAGNOSTICS,
  BOOT_WITH_FULL_CONFIGURATION,
  BOOT_ON_S5_RESUME
};

EFI_PEI_NOTIFY_DESCRIPTOR mCapsuleNotifyList[1] = {
  {
    (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
    &gPeiCapsulePpiGuid,
    CapsulePpiNotifyCallback
  }
};

BOOLEAN
GetSleepTypeAfterWakeup (
  IN  CONST EFI_PEI_SERVICES          **PeiServices,
  OUT UINT16                    *SleepType
  );

EFI_STATUS
EFIAPI
CapsulePpiNotifyCallback (
  IN EFI_PEI_SERVICES           **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
  )
{
  EFI_STATUS      Status;
  EFI_BOOT_MODE   BootMode;
  PEI_CAPSULE_PPI *Capsule;

  Status = (*PeiServices)->GetBootMode((const EFI_PEI_SERVICES **)PeiServices, &BootMode);
  ASSERT_EFI_ERROR (Status);

  if (BootMode == BOOT_ON_S3_RESUME) {
    //
    // Determine if we're in capsule update mode
    //
    Status = (*PeiServices)->LocatePpi ((const EFI_PEI_SERVICES **)PeiServices,
                                        &gPeiCapsulePpiGuid,
                                        0,
                                        NULL,
                                        (VOID **)&Capsule
                                        );

    if (Status == EFI_SUCCESS) {
      if (Capsule->CheckCapsuleUpdate ((EFI_PEI_SERVICES**)PeiServices) == EFI_SUCCESS) {
        BootMode = BOOT_ON_FLASH_UPDATE;
        DEBUG ((EFI_D_ERROR, "Setting BootMode to BOOT_ON_FLASH_UPDATE\n"));
        Status = (*PeiServices)->SetBootMode((const EFI_PEI_SERVICES **)PeiServices, BootMode);
        ASSERT_EFI_ERROR (Status);
      }
    }
  }

  return Status;
}

/**
  Get sleep type after wakeup

  @param PeiServices       Pointer to the PEI Service Table.
  @param SleepType         Sleep type to be returned.

  @retval TRUE              A wake event occured without power failure.
  @retval FALSE             Power failure occured or not a wakeup.

**/
BOOLEAN
GetSleepTypeAfterWakeup (
  IN  CONST EFI_PEI_SERVICES          **PeiServices,
  OUT UINT16                    *SleepType
  )
{
  UINT16  Pm1Sts;
  UINT16  Pm1Cnt;
  UINT16  GenPmCon1;
  //
  // VLV BIOS Specification 0.6.2 - Section 18.4, "Power Failure Consideration"
  //
  // When the SUS_PWR_FLR bit is set, it indicates the SUS well power is lost.
  // This bit is in the SUS Well and defaults to 1�b1 based on RSMRST# assertion (not cleared by any type of reset).
  // System BIOS should follow cold boot path if SUS_PWR_FLR (PBASE + 0x20[14]),
  // GEN_RST_STS (PBASE + 0x20[9]) or PWRBTNOR_STS (ABASE + 0x00[11]) is set to 1�b1
  // regardless of the value in the SLP_TYP (ABASE + 0x04[12:10]) field.
  //
  GenPmCon1 = MmioRead16 (PMC_BASE_ADDRESS + R_PCH_PMC_GEN_PMCON_1);

  //
  // Read the ACPI registers
  //
  Pm1Sts  = IoRead16 (ACPI_BASE_ADDRESS + R_PCH_ACPI_PM1_STS);
  Pm1Cnt  = IoRead16 (ACPI_BASE_ADDRESS + R_PCH_ACPI_PM1_CNT);

  if ((GenPmCon1 & (B_PCH_PMC_GEN_PMCON_SUS_PWR_FLR | B_PCH_PMC_GEN_PMCON_GEN_RST_STS)) ||
     (Pm1Sts & B_PCH_ACPI_PM1_STS_PRBTNOR)) {
	  //
    // If power failure indicator, then don't attempt s3 resume.
    // Clear PM1_CNT of S3 and set it to S5 as we just had a power failure, and memory has
    // lost already.  This is to make sure no one will use PM1_CNT to check for S3 after
    // power failure.
	  //
    if ((Pm1Cnt & B_PCH_ACPI_PM1_CNT_SLP_TYP) == V_PCH_ACPI_PM1_CNT_S3) {
      Pm1Cnt = ((Pm1Cnt & ~B_PCH_ACPI_PM1_CNT_SLP_TYP) | V_PCH_ACPI_PM1_CNT_S5);
      IoWrite16 (ACPI_BASE_ADDRESS + R_PCH_ACPI_PM1_CNT, Pm1Cnt);
    }
	  //
    // Clear Wake Status (WAK_STS)
    //
  }
  //
  // Get sleep type if a wake event occurred and there is no power failure
  //
  if ((Pm1Cnt & B_PCH_ACPI_PM1_CNT_SLP_TYP) == V_PCH_ACPI_PM1_CNT_S3) {
    *SleepType = Pm1Cnt & B_PCH_ACPI_PM1_CNT_SLP_TYP;
    return TRUE;
  } else if ((Pm1Cnt & B_PCH_ACPI_PM1_CNT_SLP_TYP) == V_PCH_ACPI_PM1_CNT_S4) {
    *SleepType = Pm1Cnt & B_PCH_ACPI_PM1_CNT_SLP_TYP;
    return TRUE;
  }
  return FALSE;
}

BOOLEAN
EFIAPI
IsFastBootEnabled (
  IN CONST EFI_PEI_SERVICES           **PeiServices
  )
{
  EFI_STATUS                      Status;
  EFI_PEI_READ_ONLY_VARIABLE2_PPI *PeiReadOnlyVarPpi;
  UINTN                           VarSize;
  SYSTEM_CONFIGURATION            SystemConfiguration;
  BOOLEAN                         FastBootEnabledStatus;

  FastBootEnabledStatus = FALSE;
  Status = (**PeiServices).LocatePpi (
                             PeiServices,
                             &gEfiPeiReadOnlyVariable2PpiGuid,
                             0,
                             NULL,
                             (void **)&PeiReadOnlyVarPpi
                             );
  if (Status == EFI_SUCCESS) {
    VarSize = sizeof (SYSTEM_CONFIGURATION);
    Status = PeiReadOnlyVarPpi->GetVariable (
                                  PeiReadOnlyVarPpi,
                                  PLATFORM_SETUP_VARIABLE_NAME,
                                  &gEfiSetupVariableGuid,
                                  NULL,
                                  &VarSize,
                                  &SystemConfiguration
                                  );
    if (Status == EFI_SUCCESS) {
      if (SystemConfiguration.FastBoot != 0) {
        FastBootEnabledStatus = TRUE;
      }
    }
  }

  return FastBootEnabledStatus;
}

/**
  Given the current boot mode, and a proposed new boot mode, determine
  which has priority. If the new boot mode has higher priority, then
  make it the current boot mode.

  @param CurrentBootMode    pointer to current planned boot mode
  @param NewBootMode        proposed boot mode

  @retval EFI_NOT_FOUND      if either boot mode is not recognized
  @retval EFI_SUCCESS        if both boot mode values were recognized and
                             were processed.
**/
EFI_STATUS
PrioritizeBootMode (
  IN OUT EFI_BOOT_MODE    *CurrentBootMode,
  IN EFI_BOOT_MODE        NewBootMode
  )
{
  UINT32 CurrentIndex;
  UINT32 NewIndex;

  //
  // Find the position of the current boot mode in our priority array
  //
  for ( CurrentIndex = 0;
        CurrentIndex < ARRAY_SIZE (mBootModePriority);
        CurrentIndex++) {
    if (mBootModePriority[CurrentIndex] == *CurrentBootMode) {
      break;
    }
  }
  if (CurrentIndex >= ARRAY_SIZE (mBootModePriority)) {
    return EFI_NOT_FOUND;
  }

  //
  // Find the position of the new boot mode in our priority array
  //
  for ( NewIndex = 0;
        NewIndex < ARRAY_SIZE (mBootModePriority);
        NewIndex++) {
    if (mBootModePriority[NewIndex] == NewBootMode) {
      //
      // If this new boot mode occurs before the current boot mode in the
      // priority array, then take it.
      //
      if (NewIndex < CurrentIndex) {
        *CurrentBootMode = NewBootMode;
      }
      return EFI_SUCCESS;
    }
  }
  return EFI_NOT_FOUND;
}
