#include <windows.h>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <vector>

int height;
int width;
bool isGetPrintIdCalled = false;

int64_t __fastcall chcusb_setPrinterInfo(int64_t a1, int64_t a2, int64_t a3, int64_t a4)
{
    OutputDebugStringA("SetPrinterInfo called");
    return 1;
}

int64_t __fastcall chcusb_imageformat(
    int16_t a1,
    int16_t a2,
    int16_t a3,
    uint16_t a4,
    uint16_t a5,
    int64_t a6,
    WORD* a7)
{
    OutputDebugStringA("ImageFormatHook called");
    OutputDebugStringA(std::format("Height: {}, Width: {}", a4, a5).c_str());
    height = a5;
    width = a4;
    return 1;
}

int64_t __fastcall chcusb_setIcctable(
    int a1,
    int a2,
    uint16_t a3,
    void* a4,
    void* a5,
    void* a6,
    void* a7,
    void* a8,
    void* a9,
    WORD* a10)
{
    OutputDebugStringA("SetIcctable called");
    return 1;
}

int64_t __fastcall chcusb_startpage(int64_t printId, WORD* returnCode)
{
    OutputDebugStringA("StartPage called");
    return 1;
}

int64_t __fastcall chcusb_getMtf(int64_t a1, int64_t a2, WORD* a3)
{
    OutputDebugStringA("GetMtf called");
    return 1;
}

int64_t __fastcall chcusb_endpage(WORD* a1)
{
    if (a1 != nullptr)
    {
        *a1 = 0;
    }
    return 1;
}

std::string createUniqueFileName(const std::string& baseName, const std::string& extension)
{
    // Get the current time point
    auto now = std::chrono::system_clock::now();

    // Convert it to a time_t object
    auto now_c = std::chrono::system_clock::to_time_t(now);

    // Convert it to tm struct
    std::tm now_tm;
    localtime_s(&now_tm, &now_c);

    // Get the number of milliseconds since the last second
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    // Use stringstream to format the file name
    std::stringstream ss;
    ss << baseName << "_"
        << std::put_time(&now_tm, "%Y%m%d_%H%M%S") // format: YYYYMMDD_HHMMSS
        << '_' << std::setfill('0') << std::setw(3) << milliseconds.count()
        << '.' << extension;

    return ss.str();
}

int64_t __fastcall chcusb_write(void* data, unsigned int* size, WORD* returnCode)
{
    OutputDebugStringA(std::format("Printing image, height: {}, width: {}", height, width).c_str());
    // Now adding code to dump the data to a file
    std::string filename = createUniqueFileName("output_image", "bmp");
    std::ofstream file("printSave/" + filename, std::ios::out | std::ios::binary);
    if (!file)
    {
        // Handle file open error
        OutputDebugStringA("Failed to open file");
        return -1;
    }

    int bytesPerPixel = 3; // For RGB

    // Write BMP header
    int rowPadding = (4 - (width * bytesPerPixel) % 4) % 4;
    int filesize = 54 + (width * bytesPerPixel + rowPadding) * height;
    std::vector<unsigned char> bmpfileheader(14, 0);
    std::vector<unsigned char> bmpinfoheader(40, 0);

    bmpfileheader[0] = 'B';
    bmpfileheader[1] = 'M';
    bmpfileheader[2] = filesize & 0xFF;
    bmpfileheader[3] = (filesize >> 8) & 0xFF;
    bmpfileheader[4] = (filesize >> 16) & 0xFF;
    bmpfileheader[5] = (filesize >> 24) & 0xFF;
    bmpfileheader[10] = 54;

    bmpinfoheader[0] = 40;
    bmpinfoheader[4] = width & 0xFF;
    bmpinfoheader[5] = (width >> 8) & 0xFF;
    bmpinfoheader[6] = (width >> 16) & 0xFF;
    bmpinfoheader[7] = (width >> 24) & 0xFF;
    bmpinfoheader[8] = (height) & 0xFF;
    bmpinfoheader[9] = (height >> 8) & 0xFF;
    bmpinfoheader[10] = (height >> 16) & 0xFF;
    bmpinfoheader[11] = (height >> 24) & 0xFF;
    bmpinfoheader[12] = 1;
    bmpinfoheader[14] = 24;

    file.write(reinterpret_cast<const char*>(bmpfileheader.data()), bmpfileheader.size());
    file.write(reinterpret_cast<const char*>(bmpinfoheader.data()), bmpinfoheader.size());

    unsigned char* pixelData = static_cast<unsigned char*>(data);
    // Write the image data
    std::vector<unsigned char> padding(rowPadding, 0);
    for (int y = height - 1; y >= 0; y--)
    {
        for (int x = 0; x < width; x++)
        {
            // Swap Red and Blue components for each pixel (Convert from RGB to BGR)
            std::swap(pixelData[(y * width + x) * 3], pixelData[(y * width + x) * 3 + 2]);
        }

        // Write the row with BGR data
        file.write(reinterpret_cast<const char*>(pixelData + (y * width * bytesPerPixel)), width * bytesPerPixel);

        // Write padding if needed
        if (rowPadding > 0)
        {
            file.write(reinterpret_cast<const char*>(padding.data()), rowPadding);
        }
    }


    file.close();
    return 1;
}

int64_t __fastcall chcusb_getPrintIDStatus(int16_t printId, BYTE* a2, WORD* returnCode)
{
    *returnCode = 0;
    if (!isGetPrintIdCalled)
    {
        OutputDebugStringA("PrintIDStatusHook called 0");
        *(WORD*)&a2[6] = 2211;
        isGetPrintIdCalled = true;
    }
    else
    {
        OutputDebugStringA("PrintIDStatusHook called 1");
        *(WORD*)&a2[6] = 2212;
        isGetPrintIdCalled = false;
    }
    return 1;
}

int64_t chcusb_universal_command()
{
    return 0i64;
}

int64_t chcusb_status()
{
    return 1i64;
}

