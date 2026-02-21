# nRF5340 Bare-Metal Makefile
# Usage:
#   make CORE=net        - build for network core (default)
#   make CORE=app        - build for application core
#   make flash CORE=net  - flash network core
#   make flash CORE=app  - flash application core

######################################
# Core selection (default: network)
######################################
CORE ?= net

######################################
# Building variables
######################################
# Debug build?
DEBUG = 1
# Optimization
OPT = -Og

#######################################
# Binaries
#######################################
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# Core-specific configuration
#######################################
ifeq ($(CORE),app)

TARGET = nrf5340_app
BUILD_DIR = build/app

C_SOURCES = \
src/main_app.c \
startup/system_nrf5340_application.c

ASM_SOURCES = \
startup/gcc_startup_nrf5340_application.s

CPU = -mcpu=cortex-m33
FPU = -mfpu=fpv5-sp-d16
FLOAT-ABI = -mfloat-abi=hard

C_DEFS = \
-DNRF5340_XXAA \
-DNRF_APPLICATION

LDSCRIPT = linker/nrf5340_xxaa_application.ld
COPROCESSOR = CP_APPLICATION

else ifeq ($(CORE),net)

TARGET = nrf5340_net
BUILD_DIR = build/net

C_SOURCES = \
src/net.c \
startup/system_nrf5340_network.c\

ASM_SOURCES = \
startup/gcc_startup_nrf5340_network.s

CPU = -mcpu=cortex-m33+nodsp
FPU =
FLOAT-ABI = -mfloat-abi=soft

C_DEFS = \
-DNRF5340_XXAA \
-DNRF_NETWORK

LDSCRIPT = linker/nrf5340_xxaa_network.ld
COPROCESSOR = CP_NETWORK

else
$(error Invalid CORE selection "$(CORE)". Use CORE=app or CORE=net)
endif

#######################################
# CFLAGS
#######################################
# MCU
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# C includes
C_INCLUDES = \
-Iinclude \
-ICMSIS/Include

# Compile gcc flags
ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections

CFLAGS = $(MCU) $(C_INCLUDES) $(C_DEFS) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

#######################################
# LDFLAGS
#######################################
# Libraries
LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -Llinker $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# Default action: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# Build
#######################################
# List of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

# List of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	-mkdir $(subst /,\,$@)

#######################################
# Clean up
#######################################
clean:
	-rmdir /s /q build

#######################################
# Flash
#######################################
flash: all
	nrfjprog --program $(BUILD_DIR)/$(TARGET).hex --chiperase --verify -f nrf53 --coprocessor $(COPROCESSOR)
	nrfjprog --reset -f nrf53

#######################################
# Dependencies
#######################################
-include $(wildcard $(BUILD_DIR)/*.d)

# *** EOF ***