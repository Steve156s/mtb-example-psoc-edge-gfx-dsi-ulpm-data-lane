################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M33 core project
#
################################################################################
# \copyright
# (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
# Technologies AG.  SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
################################################################################

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm33_ns

# If set to "true" or "1", display full command-lines when building.
VERBOSE=


################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS+=FREERTOS RTOS_AWARE 

# HUD goggle connectivity on the CYW55513 (KIT_PSE84_AI / KIT_PSE84_EVAL):
# AIROC BT stack (LE only) + Wi-Fi Connection Manager with lwIP / mbedTLS.
# Same component set as the Infineon example
# mtb-example-psoc-edge-btstack-wifi-onboarding.
#   GOGGLE_WIFI=1 : BLE + Wi-Fi (CYW55513 combo chip, default)
#   GOGGLE_WIFI=0 : BLE only (product CYW55310 has no WLAN; also delete
#                   deps/wifi-core-freertos-lwip-mbedtls.mtb)
GOGGLE_WIFI?=1
COMPONENTS+=WICED_BLE
ifeq ($(GOGGLE_WIFI),1)
COMPONENTS+=LWIP MBEDTLS WCM SECURE_SOCKETS
endif

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

CORE=CM33
CORE_NAME=CM33_0

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
SOURCES+=$(wildcard ../shared/goggle_link/*.c)

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
INCLUDES+=../shared/goggle_link ./connectivity

# Add additional defines to the build process (without a leading -D).
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF

# Connectivity (values from the Infineon Wi-Fi onboarding example)
DEFINES+=CY_RTOS_AWARE GOGGLE_WIFI_ENABLE=$(GOGGLE_WIFI)
ifeq ($(GOGGLE_WIFI),1)
MBEDTLSFLAGS=MBEDTLS_USER_CONFIG_FILE='"configs/mbedtls_user_config.h"'
DEFINES+=$(MBEDTLSFLAGS) CYBSP_WIFI_CAPABLE
DEFINES+=IFX_TF_PSA IFX_TF_PSA_CRYPTO
DEFINES+=TF_PSA_CRYPTO_CONFIG_FILE='"configs/tf_psa_crypto_config.h"'
DEFINES+=TF_PSA_CRYPTO_USER_CONFIG_FILE='"configs/tf_psa_crypto_user_config.h"'
endif
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_DOWNLOAD=3000000
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000

# 1 = every HUD Link write needs an encrypted (paired) link (default).
# 0 = open writes, only for bring-up with a generic BLE app.
GOGGLE_LINK_SECURE?=1
DEFINES+=GOGGLE_LINK_SECURE=$(GOGGLE_LINK_SECURE)

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT+=

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CFLAGS+=

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS+=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS+=

# Additional / custom linker flags.
LDFLAGS+=

# Additional / custom libraries to link in to the application.
LDLIBS+=

# Path to the linker script to use (if empty, use the default linker script).
LINKER_SCRIPT=

# Custom pre-build commands to run.
PREBUILD=

# Custom post-build commands to run.
POSTBUILD=

################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> 
# field begins with $$ASSET_REPO$$, then the repo is deposited in the path 
# specified by the CY_GETLIBS_SHARED_PATH variable. The default location is one 
# directory level above the current app directory.
# This is used with CY_GETLIBS_SHARED_NAME variable, which specifies the 
# directory name.
CY_GETLIBS_SHARED_PATH=../..

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

include $(CY_TOOLS_DIR)/make/start.mk