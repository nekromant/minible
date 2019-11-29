# Timeout for reading data in ms
USB_READ_TIMEOUT		= 10000

# Device VID & PID
USB_VID                 = 0x1209
USB_PID                 = 0x4321

# TLV Field indexes
LEN_INDEX               = 0x00
CMD_INDEX               = 0x01
DATA_INDEX              = 0x02

# Field Indexes
PREV_ADDRESS_INDEX      = 0x02
NEXT_ADDRESS_INDEX      = 0x04
NEXT_CHILD_INDEX        = 0x06
SERVICE_INDEX           = 0x08
DESC_INDEX              = 6
LOGIN_INDEX             = 37

# Sizes
NODE_SIZE					= 132
DEVICE_PASSWORD_SIZE		= 62
MINI_DEVICE_PASSWORD_SIZE	= 16
UID_REQUEST_KEY_SIZE		= 16
UID_KEY_SIZE				= 6
AES_KEY_SIZE				= 32
AES_BLOCK_SIZE				= 16

# Ack / Nack defines
CMD_HID_ACK					= 0x01
CMD_HID_NACK				= 0x00

# New Command IDs
CMD_PING                	= 0x0001
CMD_ID_RETRY				= 0x0002

# New Debug Command IDs
CMD_DBG_MESSAGE					= 0x8000
CMD_DBG_OPEN_DISP_BUFFER		= 0x8001
CMD_DBG_SEND_TO_DISP_BUFFER		= 0x8002
CMD_DBG_CLOSE_DISP_BUFFER		= 0x8003
CMD_DBG_ERASE_DATA_FLASH		= 0x8004
CMD_DBG_IS_DATA_FLASH_READY		= 0x8005
CMD_DBG_DATAFLASH_WRITE_256B	= 0x8006
CMD_DBG_REBOOT_TO_BOOTLOADER	= 0x8007
CMD_DBG_GET_ACC_32_SAMPLES		= 0x8008
CMD_DBG_FLASH_AUX_MCU			= 0x8009
CMD_DBG_GET_PLAT_INFO			= 0x800A
CMD_DBG_REINDEX_BUNDLE			= 0x800B
CMD_DBG_SET_OLED_PARAMS 		= 0x800C
CMD_DBG_GET_BATTERY_STATUS		= 0x800D