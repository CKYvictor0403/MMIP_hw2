#include "io/medical_loader.hpp"

#include <dcmtk/config/osconfig.h>   // MUST be first with DCMTK on some platforms
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcxfer.h>

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace mcodec {
namespace {

static void skip_ws_and_comments(std::istream& is) {
    while (true) {
        int c = is.peek();
        if (c == '#') {
            std::string dummy;
            std::getline(is, dummy);
            continue;
        }
        if (c == EOF) return;
        if (std::isspace(static_cast<unsigned char>(c))) {
            is.get();
            continue;
        }
        return;
    }
}

static std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

static Image load_pgm(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.good()) throw std::runtime_error("Cannot open file: " + path);

    std::string magic;
    ifs >> magic;
    if (magic != "P5") throw std::runtime_error("Only PGM P5 is supported: " + path);

    skip_ws_and_comments(ifs);
    int w = 0, h = 0;
    ifs >> w >> h;
    if (w <= 0 || h <= 0) throw std::runtime_error("Invalid PGM size: " + path);

    skip_ws_and_comments(ifs);
    int maxv = 0;
    ifs >> maxv;
    if (maxv <= 0 || maxv > 65535) throw std::runtime_error("Invalid PGM maxval: " + path);

    // consume one whitespace after header
    ifs.get();

    Image im;
    im.width = w;
    im.height = h;
    im.channels = 1;
    im.bits_allocated = (maxv <= 255) ? 8 : 16;
    im.bits_stored = im.bits_allocated;
    im.is_signed = false;
    im.type = (im.bits_allocated == 8) ? PixelType::U8 : PixelType::U16;
    im.pixels.resize(static_cast<size_t>(w) * h);

    if (im.bits_allocated == 8) {
        std::vector<uint8_t> buf(static_cast<size_t>(w) * h);
        ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        if (ifs.gcount() != static_cast<std::streamsize>(buf.size())) {
            throw std::runtime_error("PGM payload too short: " + path);
        }
        for (size_t i = 0; i < buf.size(); ++i) im.pixels[i] = static_cast<int32_t>(buf[i]);
    } else {
        // PGM 16-bit is big-endian
        const size_t n = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < n; ++i) {
            uint8_t hi = 0, lo = 0;
            ifs.read(reinterpret_cast<char*>(&hi), 1);
            ifs.read(reinterpret_cast<char*>(&lo), 1);
            if (!ifs.good()) throw std::runtime_error("PGM payload too short: " + path);
            uint16_t v = static_cast<uint16_t>((hi << 8) | lo);
            im.pixels[i] = static_cast<int32_t>(v);
        }
    }
    return im;
}

static std::runtime_error dcmtk_error(const std::string& where, const OFCondition& cond) {
    return std::runtime_error(where + ": " + cond.text());
}

static void require(bool ok, const std::string& msg) {
    if (!ok) throw std::runtime_error(msg);
}

static int get_instance_number(const std::string& path) {
    DcmFileFormat file;
    OFCondition st = file.loadFile(
        path.c_str(),
        EXS_Unknown,
        EGL_noChange,
        0
    );
    if (st.bad()) return 0;
    DcmDataset* ds = file.getDataset();
    if (!ds) return 0;

    Sint32 inst = 0;
    st = ds->findAndGetSint32(DCM_InstanceNumber, inst);
    if (st.good()) return static_cast<int>(inst);
    return 0;
}

static Image load_dicom_file_uncompressed(const std::string& path) {
    DcmFileFormat file;
    OFCondition st = file.loadFile(
        path.c_str(),
        EXS_Unknown,   // don't force transfer syntax
        EGL_noChange,  // keep group length encoding
        DCM_MaxReadLength   // read full value fields (incl. PixelData)
    );
    if (st.bad()) throw dcmtk_error("loadFile failed (" + path + ")", st);

    DcmDataset* ds = file.getDataset();
    require(ds != nullptr, "Dataset is null: " + path);

    // Ensure this is not encapsulated/compressed (you said you already converted to uncompressed)
    const DcmXfer xfer(ds->getOriginalXfer());
    require(!xfer.isEncapsulated(),
            "Compressed/encapsulated DICOM detected (TransferSyntax=" + std::string(xfer.getXferName()) +
            "). Please convert to uncompressed first: " + path);

    Uint16 rows = 0, cols = 0;
    Uint16 bitsStored = 0, bitsAllocated = 0;
    Uint16 pixelRep = 0; // 0=unsigned, 1=signed
    Uint16 spp = 1;

    st = ds->findAndGetUint16(DCM_Rows, rows);
    if (st.bad()) throw dcmtk_error("Missing/invalid Rows", st);
    st = ds->findAndGetUint16(DCM_Columns, cols);
    if (st.bad()) throw dcmtk_error("Missing/invalid Columns", st);
    st = ds->findAndGetUint16(DCM_BitsStored, bitsStored);
    if (st.bad()) throw dcmtk_error("Missing/invalid BitsStored", st);
    st = ds->findAndGetUint16(DCM_BitsAllocated, bitsAllocated);
    if (st.bad()) throw dcmtk_error("Missing/invalid BitsAllocated", st);
    st = ds->findAndGetUint16(DCM_PixelRepresentation, pixelRep);
    if (st.bad()) throw dcmtk_error("Missing/invalid PixelRepresentation", st);

    st = ds->findAndGetUint16(DCM_SamplesPerPixel, spp);
    if (st.good()) {
        require(spp == 1, "Only SamplesPerPixel=1 (grayscale) is supported: " + path);
    }

    OFString photo;
    st = ds->findAndGetOFString(DCM_PhotometricInterpretation, photo);
    if (st.good()) {
        require(photo == "MONOCHROME2",
                "Unsupported PhotometricInterpretation: " + std::string(photo.c_str()) + " (" + path + ")");
    }

    // Multi-frame (optional) - keep it simple for now
    Sint32 nFrames = 1;
    st = ds->findAndGetSint32(DCM_NumberOfFrames, nFrames);
    if (st.good()) require(nFrames >= 1, "Invalid NumberOfFrames (" + path + ")");
    else nFrames = 1;
    require(nFrames == 1, "Only single-frame DICOM is supported (NumberOfFrames=1): " + path);

    require(bitsAllocated == 16 || bitsAllocated == 8,
            "Only BitsAllocated=8 or 16 is supported: " + path);
    require(bitsStored >= 1 && bitsStored <= bitsAllocated,
            "Invalid BitsStored: " + path);

    const size_t W = static_cast<size_t>(cols);
    const size_t H = static_cast<size_t>(rows);
    const size_t N = W * H;

    Image im;
    im.width = static_cast<int>(cols);
    im.height = static_cast<int>(rows);
    im.channels = 1;
    im.bits_stored = static_cast<int>(bitsStored);
    im.bits_allocated = static_cast<int>(bitsAllocated);
    im.is_signed = (pixelRep == 1);
    im.type = im.is_signed ? PixelType::S16 : (bitsAllocated <= 8 ? PixelType::U8 : PixelType::U16);
    im.pixels.resize(N);

    if (bitsAllocated == 8) {
        const Uint8* u8 = nullptr;
        st = ds->findAndGetUint8Array(DCM_PixelData, u8);
        if (st.bad() || !u8) throw dcmtk_error("Failed to read Uint8 PixelData", st);
        for (size_t i = 0; i < N; ++i) im.pixels[i] = static_cast<int32_t>(u8[i]);
        return im;
    }

    // BitsAllocated == 16
    // Some DICOMs (PixelData VR=OW) may fail with findAndGetSint16Array even when signed.
    // Try Uint16 first, then fallback.
    const Uint16* u16 = nullptr;
    OFCondition st_u16 = ds->findAndGetUint16Array(DCM_PixelData, u16);
    if (st_u16.good() && u16) {
        for (size_t i = 0; i < N; ++i) {
            if (im.is_signed) {
                const int16_t s = static_cast<int16_t>(u16[i]); // preserve bit-pattern
                im.pixels[i] = static_cast<int32_t>(s);
            } else {
                im.pixels[i] = static_cast<int32_t>(u16[i]);
            }
        }
        return im;
    }

    const Sint16* s16 = nullptr;
    OFCondition st_s16 = ds->findAndGetSint16Array(DCM_PixelData, s16);
    if (st_s16.bad() || !s16) {
        throw dcmtk_error("Failed to read Uint16 PixelData", st_u16);
    }
    for (size_t i = 0; i < N; ++i) im.pixels[i] = static_cast<int32_t>(s16[i]);
    return im;
}

} // namespace

Image load_medical(const std::string& path) {
    namespace fs = std::filesystem;

    // directory: treat as a DICOM series folder; load the first slice (sorted by InstanceNumber)
    if (fs::exists(path) && fs::is_directory(path)) {
        struct Item { std::string p; int inst; };
        std::vector<Item> items;
        for (auto& ent : fs::directory_iterator(path)) {
            if (!ent.is_regular_file()) continue;
            const std::string p = ent.path().string();
            items.push_back({p, get_instance_number(p)});
        }
        require(!items.empty(), "No files in folder: " + path);
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.inst < b.inst; });

        // 找第一個可讀的 DICOM
        for (const auto& it : items) {
            try {
                return load_dicom_file_uncompressed(it.p);
            } catch (...) {
                // skip non-dicom or broken files
            }
        }
        throw std::runtime_error("No readable DICOM found in folder: " + path);
    }

    auto p = lower(path);
    if (p.size() >= 4 && p.substr(p.size() - 4) == ".pgm") {
        return load_pgm(path);
    }

    // Try DICOM (no extension is common for your dataset)
    try {
        return load_dicom_file_uncompressed(path);
    } catch (const std::exception& e) {
        // TODO: PNG/TIFF
        throw std::runtime_error(std::string("Load failed (not supported PGM/DICOM): ") + e.what());
    }
}

} // namespace mcodec


