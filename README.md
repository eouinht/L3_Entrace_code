### Layer 3 Entrance Code
1. Viết chương trình C đơn giản mô phỏng đồng bộ SFN giữa UE và gNodeB
2. Viết chương trình mô phỏng thủ tục Paging giữa UE – gNodeB – AMF (mức tính năng)

# Buil & Run
Bài 1: Đồng bộ SFN chỉ cần chạy terminal 1 và terminal 2:
```bash
make all
```
Terminal1 (chạy gNodeB): gNodeB gửi MIB_msg và RRC_msg (nếu nhận được NGAP) đến UE
```bash
./gnodeb
```
Terminal2 (chạy UE): UE nhận MIB_msg và RRC_msg (nếu có) từ gNodeB, đồng bộ SFN
```bash
./ue
```
Bài 2: Thủ tục Paging cần chạy thêm terminal 3:

Terminal3 (chạy AMF): Để gửi gói NGAPNGAP_msg đến gNodeB
```bash
./amf
```
