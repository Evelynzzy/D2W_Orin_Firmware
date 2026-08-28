#ifndef BOOT_SHARED_H
#define BOOT_SHARED_H

#include <stdint.h>


#define BOOT_FLASH_BASE_ADDRESS   (0x08000000UL)
#define BOOT_FLASH_END_EXCLUSIVE   (0x08040000UL)
#define BOOT_FLASH_PAGE_SIZE   (0x00000800UL)


#define BOOT_BOOTLOADER_ADDRESS   (0x08000000UL)
#define BOOT_BOOTLOADER_SIZE   (0x00008000UL)


#define BOOT_APPLICATION_ADDRESS   (0x08008000UL)
#define BOOT_APPLICATION_MAX_SIZE   (0x0001B000UL)
#define BOOT_APPLICATION_END_EXCLUSIVE   (0x08023000UL)


#define BOOT_STAGING_ADDRESS   (0x08023000UL)
#define BOOT_STAGING_SIZE   (0x0001B000UL)
#define BOOT_STAGING_END_EXCLUSIVE   (0x0803E000UL)


#define BOOT_METADATA_ADDRESS   (0x0803E000UL)
#define BOOT_METADATA_SIZE   (0x00001000UL)
#define BOOT_METADATA_END_EXCLUSIVE   (0x0803F000UL)


#define BOOT_POWER_FAIL_ADDRESS   (0x0803F000UL)
#define BOOT_POWER_FAIL_SIZE   (0x00001000UL)
#define BOOT_POWER_FAIL_END_EXCLUSIVE   (0x08040000UL)


#define BOOT_BOARD_ID   (0x44325731UL)


#define BOOT_REQUEST_MAGIC_HIGH   (0x4432U)
#define BOOT_REQUEST_MAGIC_LOW   (0x5742U)

#if (BOOT_BOOTLOADER_ADDRESS != BOOT_FLASH_BASE_ADDRESS)
#error "The Bootloader must start at the beginning of internal Flash"
#endif

#if ((BOOT_BOOTLOADER_ADDRESS + BOOT_BOOTLOADER_SIZE) != BOOT_APPLICATION_ADDRESS)
#error "The Bootloader and Application regions must be adjacent"
#endif

#if (BOOT_APPLICATION_END_EXCLUSIVE != BOOT_STAGING_ADDRESS)
#error "The Application and staging regions must be adjacent"
#endif

#if (BOOT_STAGING_END_EXCLUSIVE != BOOT_METADATA_ADDRESS)
#error "The staging and metadata regions must be adjacent"
#endif

#if (BOOT_METADATA_END_EXCLUSIVE != BOOT_POWER_FAIL_ADDRESS)
#error "The metadata and power fail regions must be adjacent"
#endif

#if (BOOT_POWER_FAIL_END_EXCLUSIVE != BOOT_FLASH_END_EXCLUSIVE)
#error "The complete Flash partition does not reach the device Flash end"
#endif

#if ((BOOT_APPLICATION_ADDRESS % 0x200UL) != 0UL)
#error "The Application vector table must satisfy Cortex M4 alignment"
#endif

#if ((BOOT_APPLICATION_MAX_SIZE % BOOT_FLASH_PAGE_SIZE) != 0UL)
#error "The Application region must contain complete Flash pages"
#endif

#if ((BOOT_STAGING_SIZE % BOOT_FLASH_PAGE_SIZE) != 0UL)
#error "The staging region must contain complete Flash pages"
#endif

#endif 
