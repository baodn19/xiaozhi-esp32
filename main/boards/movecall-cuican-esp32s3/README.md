# ESP32-S3 Build Configuration Guide

## Basic Commands

### Set Target Chip

```bash
idf.py set-target esp32s3
```

### Open Configuration UI:

```bash
idf.py menuconfig
```
### Flash Configuration:

```
Serial flasher config -> Flash size -> 8 MB
```

### Partition Table Configuration:

```
Partition Table -> Custom partition CSV file -> partitions/v2/8m.csv
```

### Board Selection:

```
Xiaozhi Assistant -> Board Type -> Movecall CuiCan AI Pendant
```

### Enable Build Optimization:

```
Component config → Compiler options → Optimization Level → Optimize for size (-Os)
```

### Build:

```bash
idf.py build
```