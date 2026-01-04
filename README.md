# MMIP Assignment 2 — mcodec

**MMIP mcodec** 是一套教學導向的醫學影像壓縮、解壓與評估工具，
支援未壓縮灰階 DICOM（8 / 12 / 16-bit），並提供三個獨立的命令列程式：

- `encode`：影像 → 自訂位元流（`.mcodec`）
- `decode`：位元流 → 重建影像（PGM）
- `evaluate`：自動化 rate–distortion 評估

本專案著重於 **可重現性（reproducibility）** 與 **完整的評估流程設計**，
而非追求臨床等級或 state-of-the-art 的壓縮效能。

---

## 1. Project Structure

```text
src/
├─ codec/
│  ├─ encoder.cpp        # Encoding pipeline
│  └─ decoder.cpp        # Decoding pipeline
├─ entropy/
│  ├─ huffman.cpp        # Canonical Huffman coding
│  └─ rle.cpp            # Zero run-length encoding
├─ block/
│  ├─ tiling.cpp         # Block tiling
│  └─ zigzag.cpp         # Zigzag scan
├─ transform/
│  └─ dct2d.cpp          # 2D DCT / IDCT
├─ quant/
│  └─ quantizer.cpp      # Quantization / dequantization
├─ preprocess/
│  └─ level_shift.cpp
├─ io/
│  ├─ medical_loader.cpp # DICOM / PGM loader
│  └─ medical_saver.cpp  # PGM writer
├─ encode_main.cpp
├─ decode_main.cpp
└─ evaluate.cpp 
```        
---

## Codec Design

### Bitstream 格式

#### Header 欄位
- image width / height
- `bits_stored`
- flags
- block_size
- quality
- payload_bytes

#### flags
- `bit0`: `LEVEL_SHIFT_APPLIED`  
  指示 encoder 是否對輸入影像執行 level shift，decoder 依此決定是否 inverse level shift。

#### payload_bytes
```
[ Huffman table ]
  - symbol_count
  - used_symbol_count
  - canonical entries
[ Huffman encoded bitstream ]
```

---

### encode
將輸入影像（DICOM / PGM）編碼為自訂 `.mcodec` 位元流格式。

編碼流程：
```
1. Level Shift(if need)
2. Tiling
3. 2D DCT
4. Quantization
5. Zigzag Scan
6. Zero Run-Length Encoding (RLE)
7. Canonical Huffman Coding
8. Bitstream
```

---

### decode
讀取 `.mcodec` 位元流並重建影像，輸出為 PGM。

解碼流程：
```
1. Bitstream parsing
2. Canonical Huffman Decoding
3. Zero Run-Length Decoding (RLE)
4. Inverse Zigzag Scan
5. Dequantization
6. Inverse 2D DCT
7. Untiling (block reassembly)
8. Inverse Level Shift (if applied)
```

- 是否執行 inverse level shift 由 bitstream flags 決定
- 輸出 PGM 使用影像原始 `bits_stored` 動態範圍

---

## 建置方式

### Requirements
- CMake ≥ 3.20
- C++17 compiler
- DCMTK（僅用於未壓縮灰階 DICOM 讀取）

### Installing DCMTK via vcpkg (Windows)

1. 安裝 vcpkg（官方 C++ 套件管理工具）
```
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
```

完成後會得到：

```
C:\vcpkg\vcpkg.exe
```

2. 用 vcpkg 安裝 DCMTK
```
C:\vcpkg\vcpkg install dcmtk:x64-windows
```

## 3. Configure & Build

cd assignment2

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```
- `-DCMAKE_TOOLCHAIN_FILE=...`：vcpkg toolchain 路徑

```bash
cmake --build build --config Release
```

---

## 使用方式

### 1) encode
```bash
encode --in <input.dicom> --out <output.mcodec> --quality <1..100>
```
Example:
```bash
.\build\Release\encode.exe --in .\assets\I26 --out .\result\I26.mcodec --quality 50
```
### 2) decode
```bash
decode --in <input.mcodec> --out <output.pgm>
```
Example:
```bash
.\build\Release\decode.exe --in .\result\I26.mcodec --out .\result\I26_compressed.pgm
```

### 3) evaluate
```bash
evaluate --ref <dicom_path> \
         --quality q1 q2 q3 \
         --tmp_dir <dir> \
         --out <metrics.csv> \
         --fig_dir <dir>
```
Example:
```bash
.\build\Release\evaluate.exe  --ref .\assets\I26 --quality 25 50 75 --tmp_dir .\result\I26_mcodec --out .\result\I26\I26_metric.csv --fig_dir .\result\I26
```

#### Evaluation pipeline
對每一組 quality：
1. 呼叫 `encode` 產生 `<tmp_dir>/<stem>_qX.mcodec`
2. **Rate metrics**
   - `compressed_bytes`
   - `bpp`
   - `compression_ratio`  
     （raw = W × H × C × bits_allocated / 8）
3. 呼叫 `decode` 直接取得重建影像陣列
4. **Distortion metrics**
   - RMSE
   - PSNR（MAX = 2^B − 1，B = bits_stored）

> 所有 metrics 皆於原始 B-bit domain 計算；  

---

## 輸出結果

### Reconstructed images
- `<fig_dir>/<stem>_ref.pgm`  
  原始影像（原始 bit depth）
- `<fig_dir>/<stem>_qX_recon.pgm`  
  重建影像（原始 bit depth）

### Error map（視覺化）
- `<fig_dir>/<stem>_qX_err.pgm`  
  8-bit error map，使用 **99th-percentile (p99) scaling** 抑制極端誤差值，
  僅供空間誤差分佈視覺化之用。

### Metrics summary
- CSV 欄位：
```
quality, block_size, compressed_bytes, bpp, raw_bytes, compression_ratio, rmse, psnr
```

---

## 限制與注意事項

- 僅支援 **未壓縮灰階 DICOM**
- JPEG-compressed DICOM 需先轉為 uncompressed
- `bits_stored` 上限為 16（超出將拒絕）
- 16-bit PGM 採用 **big-endian**，`maxval = 2^bits_stored − 1`

---
