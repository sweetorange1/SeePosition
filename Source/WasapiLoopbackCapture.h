#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <memory>

/**
 * @brief Windows WASAPI 环回捕获类
 * 
 * 该类使用 Windows 核心音频 API (WASAPI) 实现系统音频输出的环回捕获。
 * 环回捕获允许应用程序"听到"系统播放的音频（如游戏声音、音乐等），
 * 而无需使用虚拟音频线缆或音频接口。
 * 
 * 工作原理：
 * - 使用 WASAPI 的共享模式环回功能
 * - 捕获默认音频输出设备的播放内容
 * - 将捕获的音频数据通过回调传递给上层
 * 
 * 注意：此类仅支持 Windows 平台
 */
class WasapiLoopbackCapture
{
public:
    /**
     * @brief 音频数据回调函数类型
     * @param left 左声道音频数据（浮点，范围 -1.0 ~ +1.0）
     * @param right 右声道音频数据
     * @param numSamples 每个声道的采样点数
     * @param sampleRate 采样率（Hz）
     */
    using AudioCallback = std::function<void(const float* left,
                                             const float* right,
                                             int numSamples,
                                             double sampleRate)>;

    /** @brief 构造函数 */
    WasapiLoopbackCapture();
    /** @brief 析构函数 - 确保捕获线程已停止 */
    ~WasapiLoopbackCapture();

    //==============================================================================
    // 捕获控制
    
    /** 
     * @brief 启动环回捕获
     * @return 成功返回 true，失败返回 false
     * 
     * 此方法会：
     * 1. 初始化 WASAPI 接口
     * 2. 找到默认音频输出设备
     * 3. 创建环回捕获客户端
     * 4. 启动捕获线程
     */
    bool start();
    
    /** 
     * @brief 停止环回捕获
     * 
     * 此方法会：
     * 1. 设置停止标志
     * 2. 等待捕获线程退出
     * 3. 释放 WASAPI 资源
     */
    void stop();

    //==============================================================================
    // 状态查询
    
    /** @brief 检查捕获是否正在运行 */
    bool isRunning() const noexcept;
    /** @brief 获取最后一次错误信息 */
    const juce::String& getLastError() const noexcept;

    //==============================================================================
    // 回调设置
    
    /**
     * @brief 设置音频数据回调
     * 
     * 当捕获到新的音频数据时，会调用此回调。
     * 回调在捕获线程中执行，注意不要直接操作 UI。
     */
    AudioCallback onAudio;

private:
    //==============================================================================
    // 内部方法
    
    /** 
     * @brief 捕获线程主函数
     * 
     * 此方法在独立的线程中运行，负责：
     * 1. 从 WASAPI 环回设备读取音频数据
     * 2. 将整数采样转换为浮点数
     * 3. 调用 onAudio 回调传递数据
     */
    void runCaptureThread();

    //==============================================================================
    // 线程控制标志（原子变量，线程安全）
    
    std::atomic<bool> running { false };   ///< 捕获线程是否正在运行
    std::atomic<bool> shouldStop { false }; ///< 请求停止标志
    
    std::unique_ptr<std::thread> captureThread; ///< 捕获线程
    
    juce::String lastError; ///< 最后一次错误信息

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WasapiLoopbackCapture)
};

