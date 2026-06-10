################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_data.c \
../Core/Src/ethernet_task.c \
../Core/Src/freertos.c \
../Core/Src/io_task.c \
../Core/Src/main.c \
../Core/Src/modbus_task.c \
../Core/Src/sse_server.c \
../Core/Src/stm32f4xx_hal_msp.c \
../Core/Src/stm32f4xx_hal_timebase_tim.c \
../Core/Src/stm32f4xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32f4xx.c \
../Core/Src/usb_task.c \
../Core/Src/web_server.c 

OBJS += \
./Core/Src/app_data.o \
./Core/Src/ethernet_task.o \
./Core/Src/freertos.o \
./Core/Src/io_task.o \
./Core/Src/main.o \
./Core/Src/modbus_task.o \
./Core/Src/sse_server.o \
./Core/Src/stm32f4xx_hal_msp.o \
./Core/Src/stm32f4xx_hal_timebase_tim.o \
./Core/Src/stm32f4xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32f4xx.o \
./Core/Src/usb_task.o \
./Core/Src/web_server.o 

C_DEPS += \
./Core/Src/app_data.d \
./Core/Src/ethernet_task.d \
./Core/Src/freertos.d \
./Core/Src/io_task.d \
./Core/Src/main.d \
./Core/Src/modbus_task.d \
./Core/Src/sse_server.d \
./Core/Src/stm32f4xx_hal_msp.d \
./Core/Src/stm32f4xx_hal_timebase_tim.d \
./Core/Src/stm32f4xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32f4xx.d \
./Core/Src/usb_task.d \
./Core/Src/web_server.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I../LWIP/App -I../LWIP/Target -I../Middlewares/Third_Party/LwIP/src/include -I../Middlewares/Third_Party/LwIP/system -I../Drivers/BSP/Components/lan8742 -I../Middlewares/Third_Party/LwIP/src/include/netif/ppp -I../Middlewares/Third_Party/LwIP/src/include/lwip -I../Middlewares/Third_Party/LwIP/src/include/lwip/apps -I../Middlewares/Third_Party/LwIP/src/include/lwip/priv -I../Middlewares/Third_Party/LwIP/src/include/lwip/prot -I../Middlewares/Third_Party/LwIP/src/include/netif -I../Middlewares/Third_Party/LwIP/src/include/compat/posix -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/arpa -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/net -I../Middlewares/Third_Party/LwIP/src/include/compat/posix/sys -I../Middlewares/Third_Party/LwIP/src/include/compat/stdc -I../Middlewares/Third_Party/LwIP/system/arch -I../Middlewares/Third_Party/LwIP/src/apps/http -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_data.cyclo ./Core/Src/app_data.d ./Core/Src/app_data.o ./Core/Src/app_data.su ./Core/Src/ethernet_task.cyclo ./Core/Src/ethernet_task.d ./Core/Src/ethernet_task.o ./Core/Src/ethernet_task.su ./Core/Src/freertos.cyclo ./Core/Src/freertos.d ./Core/Src/freertos.o ./Core/Src/freertos.su ./Core/Src/io_task.cyclo ./Core/Src/io_task.d ./Core/Src/io_task.o ./Core/Src/io_task.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/modbus_task.cyclo ./Core/Src/modbus_task.d ./Core/Src/modbus_task.o ./Core/Src/modbus_task.su ./Core/Src/sse_server.cyclo ./Core/Src/sse_server.d ./Core/Src/sse_server.o ./Core/Src/sse_server.su ./Core/Src/stm32f4xx_hal_msp.cyclo ./Core/Src/stm32f4xx_hal_msp.d ./Core/Src/stm32f4xx_hal_msp.o ./Core/Src/stm32f4xx_hal_msp.su ./Core/Src/stm32f4xx_hal_timebase_tim.cyclo ./Core/Src/stm32f4xx_hal_timebase_tim.d ./Core/Src/stm32f4xx_hal_timebase_tim.o ./Core/Src/stm32f4xx_hal_timebase_tim.su ./Core/Src/stm32f4xx_it.cyclo ./Core/Src/stm32f4xx_it.d ./Core/Src/stm32f4xx_it.o ./Core/Src/stm32f4xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32f4xx.cyclo ./Core/Src/system_stm32f4xx.d ./Core/Src/system_stm32f4xx.o ./Core/Src/system_stm32f4xx.su ./Core/Src/usb_task.cyclo ./Core/Src/usb_task.d ./Core/Src/usb_task.o ./Core/Src/usb_task.su ./Core/Src/web_server.cyclo ./Core/Src/web_server.d ./Core/Src/web_server.o ./Core/Src/web_server.su

.PHONY: clean-Core-2f-Src

