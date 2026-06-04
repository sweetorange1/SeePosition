#include "WasapiLoopbackCapture.h"

// 仅在 Windows 平台下编译 WASAPI 相关代码
#if JUCE_WINDOWS

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

// 链接必要的 Windows 库
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace
{
//==============================================================================
// COM 公寓初始化辅助结构体
// 
// Windows COM (Component Object Model) 需要在线程中初始化后才能使用 COM 接口。
// 这个结构体利用 RAII 原则，在构造时初始化 COM，析构时释放 COM。
struct ComApartment
{
    bool ok = false;  ///< COM 初始化是否成功

    /**
     * @brief 构造函数 - 初始化 COM 库
     * 
     * 使用 COINIT_MULTITHREADED 模式，允许多线程访问 COM 对象。
     * 如果 COM 已经被初始化为单线程模式，会返回 RPC_E_CHANGED_MODE，
     * 这种情况下也算成功（ok = true）。
     */
    ComApartment()
    {
        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    /**
     * @brief 析构函数 - 释放 COM 库
     * 
     * 调用 CoUninitialize() 释放本线程的 COM 资源。
     */
    ~ComApartment()
    {
        if (ok)
            CoUninitialize();
    }
};

//==============================================================================
// COM 接口安全释放模板函数
// 
// 释放 COM 接口并置空指针，避免悬空指针。
// @tparam T COM 接口类型（如 IMMDevice*, IAudioClient* 等）
// @param p 要释放的接口指针的引用
template <class T>
void safeRelease(T*& p) noexcept
{
    if (p != nullptr)
    {
        p->Release();
        p = nullptr;
    }
}

//==============================================================================
// 音频数据解码函数
// 
// 将 WASAPI 捕获的原始音频数据解码为立体声浮点格式。
// 支持多种采样格式：
// - 32-bit IEEE 浮点 (WAVE_FORMAT_IEEE_FLOAT)
// - 16-bit 整数
// - 24-bit 整数
// - 32-bit 整数
// 
// @param src 原始音频数据指针
// @param framesAvailable 可用帧数
// @param wf WAVEFORMATEX 格式描述结构体
// @param left 输出左声道浮点数据
// @param right 输出右声道浮点数据
// @return 解码的帧数
int decodeToStereoFloat(const BYTE* src,
                        int framesAvailable,
                        const WAVEFORMATEX* wf,
                        std::vector<float>& left,
                        std::vector<float>& right)
{
    // 根据帧数调整输出缓冲区大小
    left.resize((size_t)framesAvailable);
    right.resize((size_t)framesAvailable);

    const int channels = wf->nChannels;           // 声道数
    const int bytesPerFrame = wf->nBlockAlign;     // 每帧字节数
    const int bitsPerSample = wf->wBitsPerSample; // 每样本位数

    // 判断是否为浮点格式
    bool isFloat = (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        // 对于 WAVEFORMATEXTENSIBLE 格式，需要检查 SubFormat GUID
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    /**
     * @brief 从字节流中读取一个采样点并转换为浮点
     * @param p 指向采样数据的指针
     * @return 归一化的浮点采样值 (-1.0 ~ +1.0)
     */
    auto readSample = [&](const BYTE* p) -> float
    {
        if (isFloat)
        {
            // 32-bit 浮点格式：直接拷贝
            float v;
            std::memcpy(&v, p, sizeof(float));
            return v;
        }

        if (bitsPerSample == 16)
        {
            // 16-bit 整数：范围 -32768 ~ +32767，除以 32768.0 归一化
            int16_t v;
            std::memcpy(&v, p, 2);
            return (float)v / 32768.0f;
        }

        if (bitsPerSample == 24)
        {
            // 24-bit 整数：需要符号扩展为 32-bit
            // 24-bit 范围 -8388608 ~ +8388607，除以 8388608.0 归一化
            int32_t v = ((int32_t)(int8_t)p[2] << 16)
                      | ((int32_t)p[1] << 8)
                      | (int32_t)p[0];
            return (float)v / 8388608.0f;
        }

        if (bitsPerSample == 32)
        {
            // 32-bit 整数：范围 -2147483648 ~ +2147483647
            int32_t v;
            std::memcpy(&v, p, 4);
            return (float)v / 2147483648.0f;
        }

        return 0.0f;
    };

    // 逐帧解码
    for (int f = 0; f < framesAvailable; ++f)
    {
        const BYTE* frame = src + (size_t)f * (size_t)bytesPerFrame;

        if (channels >= 2)
        {
            // 立体声：左声道在前，右声道在后
            left[(size_t)f] = readSample(frame);
            right[(size_t)f] = readSample(frame + (bitsPerSample / 8));
        }
        else
        {
            // 单声道：复制到左右两个声道
            const float s = readSample(frame);
            left[(size_t)f] = s;
            right[(size_t)f] = s;
        }
    }

    return framesAvailable;
}
} // namespace

//==============================================================================
// WasapiLoopbackCapture 类实现
//==============================================================================

/**
 * @brief 默认构造函数
 * 
 * 使用编译器生成的默认实现（= default）。
 * 实际初始化在 start() 方法中进行。
 */
WasapiLoopbackCapture::WasapiLoopbackCapture() = default;

/**
 * @brief 析构函数
 * 
 * 确保捕获线程在对象销毁前停止。
 */
WasapiLoopbackCapture::~WasapiLoopbackCapture()
{
    stop();
}

//==============================================================================
// 启动捕获
// 
// 创建并启动捕获线程。线程会：
// 1. 初始化 COM 库
// 2. 获取默认渲染设备（扬声器）
// 3. 初始化 WASAPI 环回捕获
// 4. 开始捕获循环
// 
// @return 启动成功返回 true，失败返回 false（错误信息存储在 lastError 中）
bool WasapiLoopbackCapture::start()
{
    // 如果已经在运行，直接返回成功
    if (isRunning())
        return true;

    // 重置停止标志
    shouldStop.store(false, std::memory_order_release);
    lastError.clear();

    // 创建并启动捕获线程
    captureThread = std::make_unique<std::thread>([this] { runCaptureThread(); });

    // 等待捕获线程启动（最多等待 500ms）
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline)
    {
        // 检查是否已启动
        if (isRunning())
            return true;

        // 检查线程是否还活着
        if (!captureThread->joinable())
            return false;

        // 检查是否有错误发生
        if (lastError.isNotEmpty())
        {
            stop();
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // 超时：捕获线程在 500ms 内未能启动
    if (!isRunning())
    {
        stop();
        if (lastError.isEmpty())
            lastError = "Loopback capture did not start within 500ms.";
        return false;
    }

    return true;
}

//==============================================================================
// 停止捕获
// 
// 设置停止标志，等待捕获线程退出。
// 这是一个阻塞调用，会等待线程完全退出后才返回。
void WasapiLoopbackCapture::stop()
{
    // 设置停止标志（使用 memory_order_release 确保其他线程能看到）
    shouldStop.store(true, std::memory_order_release);

    // 等待捕获线程退出
    if (captureThread != nullptr)
    {
        if (captureThread->joinable())
            captureThread->join();
        captureThread.reset();
    }

    // 标记为非运行状态
    running.store(false, std::memory_order_release);
}

//==============================================================================
// 检查是否正在运行
// 
// @return 捕获线程正在运行返回 true，否则返回 false
bool WasapiLoopbackCapture::isRunning() const noexcept
{
    return running.load(std::memory_order_acquire);
}

//==============================================================================
// 获取最后一次错误信息
// 
// @return 错误信息字符串的常量引用
const juce::String& WasapiLoopbackCapture::getLastError() const noexcept
{
    return lastError;
}

//==============================================================================
// 捕获线程主函数
// 
// 这是捕获线程的入口点，负责：
// 1. 初始化 COM 和 WASAPI
// 2. 创建音频客户端并启动捕获
// 3. 循环读取音频数据并调用回调
// 
// 线程会使用 Windows MMCSS (Multimedia Class Scheduler Service)
// 来提升音频线程的优先级，确保实时性。
void WasapiLoopbackCapture::runCaptureThread()
{
    // 注册 MMCSS 多媒体调度特性，提升线程优先级
    DWORD mmcssTaskIdx = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIdx);

    // 初始化 COM 库（RAII，自动释放）
    ComApartment com;
    if (!com.ok)
    {
        lastError = "CoInitializeEx failed";
        if (mmcss != nullptr)
            AvRevertMmThreadCharacteristics(mmcss);
        return;
    }

    //==========================================================================
    // 声明 WASAPI 接口指针
    IMMDeviceEnumerator* enumerator = nullptr;  ///< 设备枚举器
    IMMDevice* renderDevice = nullptr;         ///< 渲染设备（扬声器）
    IAudioClient* audioClient = nullptr;        ///< 音频客户端
    IAudioCaptureClient* captureClient = nullptr; ///< 捕获客户端
    WAVEFORMATEX* mixFormat = nullptr;         ///< 音频格式描述

    // 清理 Lambda：释放所有 COM 接口
    auto cleanup = [&]()
    {
        safeRelease(captureClient);

        if (audioClient != nullptr)
        {
            audioClient->Stop();
            safeRelease(audioClient);
        }

        safeRelease(renderDevice);
        safeRelease(enumerator);

        if (mixFormat != nullptr)
        {
            CoTaskMemFree(mixFormat);
            mixFormat = nullptr;
        }
    };

    // 失败退出 Lambda：设置错误信息并清理
    auto failAndExit = [&](const juce::String& msg)
    {
        lastError = msg;
        cleanup();
        if (mmcss != nullptr)
            AvRevertMmThreadCharacteristics(mmcss);
    };

    //==========================================================================
    // 步骤 1：创建设备枚举器
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr))
    {
        failAndExit("CoCreateInstance(MMDeviceEnumerator) failed");
        return;
    }

    //==========================================================================
    // 步骤 2：获取默认渲染设备（扬声器/耳机等）
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &renderDevice);
    if (FAILED(hr) || renderDevice == nullptr)
    {
        failAndExit("GetDefaultAudioEndpoint(eRender) failed");
        return;
    }

    //==========================================================================
    // 步骤 3：激活音频客户端接口
    hr = renderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr))
    {
        failAndExit("IMMDevice::Activate(IAudioClient) failed");
        return;
    }

    //==========================================================================
    // 步骤 4：获取音频混合格式
    hr = audioClient->GetMixFormat(&mixFormat);
    if (FAILED(hr) || mixFormat == nullptr)
    {
        failAndExit("IAudioClient::GetMixFormat failed");
        return;
    }

    //==========================================================================
    // 步骤 5：初始化音频客户端（环回模式）
    // 
    // AUDCLNT_STREAMFLAGS_LOOPBACK 标志启用环回捕获，
    // 可以捕获系统播放的音频（如游戏声音、音乐等）。
    // 
    // bufferDuration = 10秒（10 * 1000 * 100 百纳秒）
    constexpr REFERENCE_TIME bufferDuration = 10 * 1000 * 100;
    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK,
                                 bufferDuration,
                                 0,
                                 mixFormat,
                                 nullptr);
    if (FAILED(hr))
    {
        failAndExit("IAudioClient::Initialize(LOOPBACK) failed (hr=0x"
                    + juce::String::toHexString((int)hr) + ")");
        return;
    }

    //==========================================================================
    // 步骤 6：获取捕获客户端接口
    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr))
    {
        failAndExit("IAudioClient::GetService(IAudioCaptureClient) failed");
        return;
    }

    //==========================================================================
    // 步骤 7：启动音频客户端
    hr = audioClient->Start();
    if (FAILED(hr))
    {
        failAndExit("IAudioClient::Start failed");
        return;
    }

    // 标记捕获已启动
    running.store(true, std::memory_order_release);

    //==========================================================================
    // 步骤 8：捕获循环
    // 
    // 不断从 WASAPI 获取音频数据包，解码后通过回调传递给上层。
    const double sampleRate = (double)mixFormat->nSamplesPerSec;
    std::vector<float> bufL, bufR;
    bufL.reserve(4096);
    bufR.reserve(4096);

    while (!shouldStop.load(std::memory_order_acquire))
    {
        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize(&packetLength);

        if (FAILED(hr))
            break;

        // 如果没有数据包可用，休眠 5ms 后重试
        if (packetLength == 0)
        {
            Sleep(5);
            continue;
        }

        // 处理所有可用的数据包
        while (packetLength != 0)
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            // 获取一个数据包
            hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr))
            {
                packetLength = 0;
                break;
            }

            if (frames > 0)
            {
                // 检查是否为静音包
                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                
                // 初始化输出缓冲区
                bufL.assign((size_t)frames, 0.0f);
                bufR.assign((size_t)frames, 0.0f);

                // 如果不是静音包，解码音频数据
                if (!silent && data != nullptr)
                    decodeToStereoFloat(data, (int)frames, mixFormat, bufL, bufR);

                // 调用回调函数，将解码后的音频数据传递给上层
                if (onAudio)
                    onAudio(bufL.data(), bufR.data(), (int)frames, sampleRate);
            }

            // 释放数据包（让 WASAPI 知道我们已经处理完这块数据）
            captureClient->ReleaseBuffer(frames);

            // 检查是否还有更多数据包
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr))
            {
                packetLength = 0;
                break;
            }
        }
    }

    //==========================================================================
    // 清理阶段
    running.store(false, std::memory_order_release);
    cleanup();

    // 取消 MMCSS 注册
    if (mmcss != nullptr)
        AvRevertMmThreadCharacteristics(mmcss);
}

//==============================================================================
// 非 Windows 平台的空实现
//==============================================================================
#else

/**
 * @brief 非 Windows 平台的默认构造函数
 */
WasapiLoopbackCapture::WasapiLoopbackCapture() = default;

/**
 * @brief 非 Windows 平台的默认析构函数
 */
WasapiLoopbackCapture::~WasapiLoopbackCapture() = default;

/**
 * @brief 非 Windows 平台：start() 始终返回失败
 */
bool WasapiLoopbackCapture::start()
{
    lastError = "WASAPI loopback is only supported on Windows.";
    return false;
}

/**
 * @brief 非 Windows 平台：stop() 为空实现
 */
void WasapiLoopbackCapture::stop() {}

/**
 * @brief 非 Windows 平台：isRunning() 始终返回 false
 */
bool WasapiLoopbackCapture::isRunning() const noexcept
{
    return false;
}

/**
 * @brief 非 Windows 平台：返回错误信息
 */
const juce::String& WasapiLoopbackCapture::getLastError() const noexcept
{
    return lastError;
}

/**
 * @brief 非 Windows 平台：runCaptureThread() 为空实现
 */
void WasapiLoopbackCapture::runCaptureThread() {}

#endif

