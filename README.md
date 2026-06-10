# ĐỒ ÁN TỐT NGHIỆP: HỆ THỐNG NHÚNG ĐA GIAO TIẾP (STM32 & ESP32)

## 📌 Tổng Quan Dự Án
Dự án này xây dựng một hệ thống nhúng theo mô hình **Master - Slave** sử dụng giao thức truyền thông công nghiệp (Modbus RTU) để thu thập dữ liệu cảm biến và giám sát từ xa.
* **Master (Khối điều khiển trung tâm):** Sử dụng vi điều khiển **STM32**, chịu trách nhiệm cấu hình mạng và webserver (ETH), gửi lệnh yêu cầu dữ liệu đến các Slave, debug với USB CDC, xử lý thuật toán và hiển thị/điều khiển hệ thống.
* **Slave (Khối thu thập dữ liệu):** Sử dụng vi điều khiển **ESP32**, kết nối trực tiếp với các cảm biến (DHT22), phản hồi dữ liệu khi có yêu cầu từ Master.
---

## 🛠️ Thành Phần Phần Cứng & Phần Mềm

### 1. Phần cứng (Hardware)
* **Master:** Kit phát triển STM32 (Cấu hình bằng STM32CubeMX & biên dịch qua CMake).
* **Slave:** Kit phát triển ESP32 Doit DevKit V1.
* **Cảm biến:** DHT22 (Đo nhiệt độ, độ ẩm).
* **Giao tiếp:** Module chuyển đổi RS485 (Max485) phục vụ mạng Modbus RTU. ETH với LwIP dùng làm Webserver. USB CDC đọc trạng thái hệ thống để debug.

### 2. Môi trường phần mềm (Software Môi Trường)
Dự án được quản lý và phát triển hoàn toàn trên **Visual Studio Code** dưới dạng **Multi-Root Workspace** để tránh xung đột cấu hình biên dịch:
* **Master Toolchain:** CMake, GCC ARM Compiler (`arm-none-eabi-gcc`), VS Code CMake Tools Extension.
* **Slave Toolchain:** PlatformIO IDE Extension, Arduino Framework.

---
