# Set the name of your application:
APPLICATION = ho_rubbish

# If no BOARD is found in the environment, use this default:
BOARD ?= bluepill

DEFAULT_PERIOD_SENDING ?= 3600
DEFAULT_RESOLUTION ?= 0

# Lorawan config
-include lorawan.credentials
LORA_REGION ?= EU868

# defines
CFLAGS += -DDEFAULT_PERIOD_SENDING=$(DEFAULT_PERIOD_SENDING)
CFLAGS += -DDEFAULT_RESOLUTION=$(DEFAULT_RESOLUTION)
CFLAGS += -DDS18_PARAM_PIN=$(DS18_PARAM_PIN)
CFLAGS += -DDEVEUI=\"$(DEVEUI)\" -DAPPEUI=\"$(APPEUI)\" -DAPPKEY=\"$(APPKEY)\"
CFLAGS += -DDISABLE_LORAMAC_DUTYCYCLE

# Uncomment this to enable code in RIOT that does safety checking
# which is not needed in a production environment but helps in the
# development process:
DEVELHELP = 1

# Change this to 0 to show compiler invocation lines by default:
QUIET ?= 1

# Modules to include:
USEPKG += semtech-loramac
USEMODULE += fmt
USEMODULE += semtech_loramac_rx
#USEMODULE += stdio_null
USEMODULE += sx1276

FEATURES_REQUIRED += periph_adc
FEATURES_REQUIRED += periph_gpio
FEATURES_REQUIRED += periph_rtc

# Specify custom dependencies for your application here
APPDEPS = scaling.h

# This has to be the absolute path to the RIOT base directory:
RIOTBASE ?= $(CURDIR)/../RIOT

include $(RIOTBASE)/Makefile.include
