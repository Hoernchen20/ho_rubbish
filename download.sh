!/bin/bash
arm-none-eabi-objcopy -O ihex bin/bluepill/ho_rubbish.elf bin/bluepill/ho_rubbish.hex
~/Downloads/stm32flash/./stm32flash -w bin/bluepill/ho_rubbish.hex -v /dev/ttyUSB0 -b 576000
