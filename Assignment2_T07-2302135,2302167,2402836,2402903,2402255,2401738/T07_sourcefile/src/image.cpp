/* =====================================================================================
 *  File : image.cpp        Image helpers and dependency-free image I/O
 * ===================================================================================== */
#include "image.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <wincodec.h>
#  pragma comment(lib, "windowscodecs.lib")
#  pragma comment(lib, "ole32.lib")
#  pragma comment(lib, "oleaut32.lib")
#endif

namespace sc {

/* ==================================================================================== *
 *  SMALL HELPERS                                                                       *
 * ==================================================================================== */

Image transpose(const Image& img) {
    Image t(img.h, img.w);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            std::memcpy(t.ptr(y, x), img.ptr(x, y), 3);
    return t;
}

Grid<uint8_t> toGray(const Image& img) {
    Grid<uint8_t> g(img.w, img.h);
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.ptr(x, y);
            /* 0.299 R + 0.587 G + 0.114 B  ~=  (77 R + 150 G + 29 B) / 256 */
            g.at(x, y) = static_cast<uint8_t>((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
        }
    return g;
}

static std::string lowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string e = path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

static std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open '" + path + "'");
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
static uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static void     wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i)); }
static void     wr16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }

/* ==================================================================================== *
 *  BMP  (BITMAPFILEHEADER + BITMAPINFOHEADER, 24 or 32 bpp, BI_RGB / BI_BITFIELDS)      *
 * ==================================================================================== */

Image loadBMP(const std::string& path) {
    std::vector<uint8_t> f = readAll(path);
    if (f.size() < 54 || f[0] != 'B' || f[1] != 'M') throw std::runtime_error("not a BMP: " + path);
    uint32_t dataOff = rd32(&f[10]);
    int32_t  width   = static_cast<int32_t>(rd32(&f[18]));
    int32_t  height  = static_cast<int32_t>(rd32(&f[22]));
    uint16_t bpp     = rd16(&f[28]);
    uint32_t compr   = rd32(&f[30]);
    if (bpp != 24 && bpp != 32) throw std::runtime_error("BMP must be 24 or 32 bpp: " + path);
    if (compr != 0 && compr != 3) throw std::runtime_error("compressed BMP not supported: " + path);
    bool topDown = height < 0;
    int  h = std::abs(height), w = width;
    if (w <= 0 || h <= 0) throw std::runtime_error("bad BMP size: " + path);
    size_t bytesPP = bpp / 8, stride = (static_cast<size_t>(w) * bytesPP + 3) & ~size_t(3);
    if (dataOff + stride * h > f.size()) throw std::runtime_error("truncated BMP: " + path);

    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &f[dataOff + stride * (topDown ? y : (h - 1 - y))];
        for (int x = 0; x < w; ++x) {
            uint8_t* d = img.ptr(x, y);
            d[0] = src[x * bytesPP + 2]; d[1] = src[x * bytesPP + 1]; d[2] = src[x * bytesPP + 0];
        }
    }
    return img;
}

void saveBMP(const Image& img, const std::string& path) {
    size_t stride = (static_cast<size_t>(img.w) * 3 + 3) & ~size_t(3);
    size_t size   = 54 + stride * img.h;
    std::vector<uint8_t> f(size, 0);
    f[0] = 'B'; f[1] = 'M';
    wr32(&f[2], static_cast<uint32_t>(size)); wr32(&f[10], 54); wr32(&f[14], 40);
    wr32(&f[18], static_cast<uint32_t>(img.w)); wr32(&f[22], static_cast<uint32_t>(img.h));
    wr16(&f[26], 1); wr16(&f[28], 24); wr32(&f[34], static_cast<uint32_t>(stride * img.h));
    for (int y = 0; y < img.h; ++y) {
        uint8_t* dst = &f[54 + stride * (img.h - 1 - y)];
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* s = img.ptr(x, y);
            dst[x * 3 + 0] = s[2]; dst[x * 3 + 1] = s[1]; dst[x * 3 + 2] = s[0];
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out.write(reinterpret_cast<const char*>(f.data()), static_cast<std::streamsize>(f.size()));
}

/* ==================================================================================== *
 *  PPM  (P6 binary, P3 ASCII; maxval 255)                                              *
 * ==================================================================================== */

Image loadPPM(const std::string& path) {
    std::vector<uint8_t> f = readAll(path);
    size_t pos = 0;
    auto nextToken = [&]() -> std::string {
        while (pos < f.size()) {                                   /* skip blanks and comments */
            if (std::isspace(f[pos])) { ++pos; continue; }
            if (f[pos] == '#') { while (pos < f.size() && f[pos] != '\n') ++pos; continue; }
            break;
        }
        std::string t;
        while (pos < f.size() && !std::isspace(f[pos])) t += static_cast<char>(f[pos++]);
        return t;
    };
    std::string magic = nextToken();
    if (magic != "P6" && magic != "P3") throw std::runtime_error("not a P3/P6 PPM: " + path);
    int w = std::stoi(nextToken()), h = std::stoi(nextToken()), maxv = std::stoi(nextToken());
    if (w <= 0 || h <= 0 || maxv != 255) throw std::runtime_error("unsupported PPM header: " + path);
    Image img(w, h);
    if (magic == "P6") {
        ++pos;                                                     /* single whitespace byte */
        if (pos + img.px.size() > f.size()) throw std::runtime_error("truncated PPM: " + path);
        std::memcpy(img.px.data(), &f[pos], img.px.size());
    } else {
        for (auto& v : img.px) v = static_cast<uint8_t>(std::stoi(nextToken()));
    }
    return img;
}

void savePPM(const Image& img, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path + "'");
    out << "P6\n" << img.w << " " << img.h << "\n255\n";
    out.write(reinterpret_cast<const char*>(img.px.data()), static_cast<std::streamsize>(img.px.size()));
}

/* ==================================================================================== *
 *  WIC (Windows only) : PNG / JPEG / GIF / TIFF                                        *
 * ==================================================================================== */
#ifdef _WIN32

namespace {

template <typename T> struct ComPtr {                     /* minimal RAII wrapper */
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

struct ComInit {
    bool ok;
    ComInit() { HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE; }
    ~ComInit() { /* leave COM initialised for the process lifetime - cheap and avoids
                    tearing it down under a GUI that also uses it */ }
};

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

void check(HRESULT hr, const char* what, const std::string& path) {
    if (FAILED(hr)) {
        char buf[32]; std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
        throw std::runtime_error(std::string("WIC ") + what + " failed (" + buf + ") for '" + path + "'");
    }
}

IWICImagingFactory* factory() {
    static ComInit com;
    static IWICImagingFactory* f = nullptr;
    if (!f) {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&f));
        if (FAILED(hr)) throw std::runtime_error("cannot create WIC factory");
    }
    return f;
}

Image wicLoad(const std::string& path) {
    IWICImagingFactory* fac = factory();
    std::wstring wpath = widen(path);
    ComPtr<IWICBitmapDecoder> dec;
    check(fac->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                         WICDecodeMetadataCacheOnDemand, &dec), "decode", path);
    ComPtr<IWICBitmapFrameDecode> frame;
    check(dec->GetFrame(0, &frame), "GetFrame", path);
    ComPtr<IWICFormatConverter> conv;
    check(fac->CreateFormatConverter(&conv), "CreateFormatConverter", path);
    check(conv->Initialize(frame.p, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone,
                           nullptr, 0.0, WICBitmapPaletteTypeCustom), "convert", path);
    UINT w = 0, h = 0;
    check(conv->GetSize(&w, &h), "GetSize", path);
    UINT stride = w * 3;
    std::vector<uint8_t> bgr(static_cast<size_t>(stride) * h);
    check(conv->CopyPixels(nullptr, stride, static_cast<UINT>(bgr.size()), bgr.data()), "CopyPixels", path);
    Image img(static_cast<int>(w), static_cast<int>(h));
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        img.px[i * 3 + 0] = bgr[i * 3 + 2]; img.px[i * 3 + 1] = bgr[i * 3 + 1]; img.px[i * 3 + 2] = bgr[i * 3 + 0];
    }
    return img;
}

void wicSave(const Image& img, const std::string& path, const GUID& container) {
    IWICImagingFactory* fac = factory();
    std::wstring wpath = widen(path);
    ComPtr<IWICStream> stream;
    check(fac->CreateStream(&stream), "CreateStream", path);
    check(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE), "open for write", path);
    ComPtr<IWICBitmapEncoder> enc;
    check(fac->CreateEncoder(container, nullptr, &enc), "CreateEncoder", path);
    check(enc->Initialize(stream.p, WICBitmapEncoderNoCache), "encoder init", path);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    check(enc->CreateNewFrame(&frame, &props), "CreateNewFrame", path);
    if (container == GUID_ContainerFormatJpeg && props.p) {          /* high JPEG quality */
        PROPBAG2 opt = {}; opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = 0.95f;
        props->Write(1, &opt, &v);
    }
    check(frame->Initialize(props.p), "frame init", path);
    check(frame->SetSize(static_cast<UINT>(img.w), static_cast<UINT>(img.h)), "SetSize", path);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    check(frame->SetPixelFormat(&fmt), "SetPixelFormat", path);
    UINT stride = static_cast<UINT>(img.w) * 3;
    std::vector<uint8_t> bgr(img.px.size());
    for (size_t i = 0; i < static_cast<size_t>(img.w) * img.h; ++i) {
        bgr[i * 3 + 0] = img.px[i * 3 + 2]; bgr[i * 3 + 1] = img.px[i * 3 + 1]; bgr[i * 3 + 2] = img.px[i * 3 + 0];
    }
    check(frame->WritePixels(static_cast<UINT>(img.h), stride, static_cast<UINT>(bgr.size()), bgr.data()),
          "WritePixels", path);
    check(frame->Commit(), "frame commit", path);
    check(enc->Commit(), "encoder commit", path);
}

}  // namespace

bool hasNativeCodecs() { return true; }

#else
bool hasNativeCodecs() { return false; }
#endif

/* ==================================================================================== *
 *  DISPATCH BY EXTENSION                                                               *
 * ==================================================================================== */

Image loadImage(const std::string& path) {
    std::string e = lowerExt(path);
    if (e == "ppm" || e == "pnm") return loadPPM(path);
#ifdef _WIN32
    return wicLoad(path);                                          /* BMP, PNG, JPG, GIF, TIFF */
#else
    if (e == "bmp") return loadBMP(path);
    throw std::runtime_error("'" + path + "': only .bmp/.ppm are supported on this platform");
#endif
}

void saveImage(const Image& img, const std::string& path) {
    std::string e = lowerExt(path);
    if (e == "ppm" || e == "pnm") return savePPM(img, path);
    if (e == "bmp") return saveBMP(img, path);
#ifdef _WIN32
    if (e == "png") return wicSave(img, path, GUID_ContainerFormatPng);
    if (e == "jpg" || e == "jpeg") return wicSave(img, path, GUID_ContainerFormatJpeg);
#endif
    throw std::runtime_error("'" + path + "': unsupported output format on this platform");
}

/* ==================================================================================== *
 *  BASELINES: uniform scaling and centre cropping                                      *
 * ==================================================================================== */

Image resizeBilinear(const Image& img, int newW, int newH) {
    Image out(newW, newH);
    double sx = static_cast<double>(img.w) / newW, sy = static_cast<double>(img.h) / newH;
    for (int y = 0; y < newH; ++y) {
        double fy = std::max(0.0, (y + 0.5) * sy - 0.5);
        int y0 = std::min(static_cast<int>(fy), img.h - 1), y1 = std::min(y0 + 1, img.h - 1);
        double ty = fy - y0;
        for (int x = 0; x < newW; ++x) {
            double fx = std::max(0.0, (x + 0.5) * sx - 0.5);
            int x0 = std::min(static_cast<int>(fx), img.w - 1), x1 = std::min(x0 + 1, img.w - 1);
            double tx = fx - x0;
            for (int c = 0; c < 3; ++c) {
                double top = img.ptr(x0, y0)[c] * (1 - tx) + img.ptr(x1, y0)[c] * tx;
                double bot = img.ptr(x0, y1)[c] * (1 - tx) + img.ptr(x1, y1)[c] * tx;
                out.ptr(x, y)[c] = static_cast<uint8_t>(std::lround(top * (1 - ty) + bot * ty));
            }
        }
    }
    return out;
}

Image cropCenter(const Image& img, int newW, int newH) {
    newW = std::min(newW, img.w); newH = std::min(newH, img.h);
    int ox = (img.w - newW) / 2, oy = (img.h - newH) / 2;
    Image out(newW, newH);
    for (int y = 0; y < newH; ++y)
        std::memcpy(out.ptr(0, y), img.ptr(ox, oy + y), static_cast<size_t>(newW) * 3);
    return out;
}

Image hconcat(const std::vector<Image>& imgs, int gap) {
    int W = 0, H = 0;
    for (const Image& im : imgs) { W += im.w + gap; H = std::max(H, im.h); }
    W = std::max(0, W - gap);
    Image out(W, H);
    std::fill(out.px.begin(), out.px.end(), uint8_t(255));
    int x0 = 0;
    for (const Image& im : imgs) {
        for (int y = 0; y < im.h; ++y)
            std::memcpy(out.ptr(x0, y), im.ptr(0, y), static_cast<size_t>(im.w) * 3);
        x0 += im.w + gap;
    }
    return out;
}

}  // namespace sc
