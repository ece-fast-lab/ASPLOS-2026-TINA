sudo ./pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 25781
sudo ./pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x14 w 128000  #Burst Starts @128K
sudo ./pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x18 w 2000000 #Burst Port Switching Starts @2M
sudo ./pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x1C w 7000000 #Burst Port stop switching @7M
sudo ./pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x20 w 100  # Original port is 100
