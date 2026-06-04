#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "WasapiLoopbackCapture.h"

/**
 * @brief 音频捕获与处理服务类
 * 
 * 这是 SeePosition 的核心音频处理模块，负责：
 * - 音频输入捕获（WASAPI 环回 或 JUCE 默认输入）
 * - 实时计算声像位置（Balance）和立体声宽度（Width）
 * - 瞬态检测与分类（枪声、脚步声、换弹）
 * - 多频段能量分析与评分
 */
class AudioCaptureService final : private juce::AudioIODeviceCallback
{
public:
    //==============================================================================
    // 调优参数结构体
    
    /**
     * @brief 音频分析调优参数集合
     * 
     * 这些参数控制音频检测和分析的行为，可通过设置面板实时调整。
     * 所有参数都有合理的默认值，适用于大多数场景。
     */
    struct Tuning
    {
        //--- 基础检测门限 ---
        float loudEnoughDb = -62.0f;          ///< 响度门限（dB）：低于此值视为静音
        float onsetContrastStart = 0.55f;     ///< 起音对比起始阈值：检测瞬态开始的对比度
        float onsetRatioStart = 1.10f;        ///< 起音倍率起始阈值：瞬态开始时信号倍率
        float onsetContrastSettle = 0.45f;    ///< 起音对比稳定阈值：瞬态结束后的对比度
        float onsetRatioSettle = 1.10f;       ///< 起音倍率稳定阈值
        
        //--- 冷却控制 ---
        float cooldownBaseSeconds = 0.050f;   ///< 基础冷却时间（秒）：瞬态触发后的最小间隔
        float cooldownByLevelSeconds = 0.070f; ///< 电平相关冷却：响度对冷却时间的影响范围

        //--- 瞬态评分权重 ---
        float scoreEnergyFluxWeight = 0.95f;  ///< 能量流量评分权重：检测能量突变
        float scoreHighFluxWeight = 1.10f;    ///< 高频流量评分权重：检测高频瞬态
        float scoreContrastWeight = 1.25f;    ///< 对比度评分权重：检测起音对比
        float scoreAdaptTauSeconds = 0.45f;   ///< 评分自适应时间常数：自适应阈值的平滑时间
        float scoreHighThresholdK = 2.15f;    ///< 评分高阈值系数：触发瞬态的阈值倍数
        float scoreLowThresholdK = 0.95f;     ///< 评分低阈值系数：重置触发状态的阈值倍数

        //--- 强制重装 ---
        float forcedRearmSeconds = 0.120f;    ///< 强制重装时间：超过此时间强制重新武装

        //--- 连发检测参数 ---
        float burstIoiBlend = 0.30f;          ///< 连发 IOI 混合率：学习连发间隔的速度
        float burstDecayPerSecond = 0.70f;    ///< 连发置信度衰减率：每秒衰减量
        float burstThresholdDropScale = 0.40f; ///< 连发阈值降低比例：连发时降低检测阈值
        float burstCooldownReduction = 0.35f; ///< 连发冷却缩减：连发时减少冷却时间
        float burstMinCooldownScale = 0.55f;  ///< 连发最小冷却缩放：连发时冷却时间的最小比例

        //--- 持续音处理 ---
        float transientHighpassHz = 180.0f;   ///< 瞬态高通滤波频率（Hz）：滤除低频持续音
        float sustainedRelaxStartSeconds = 0.15f;  ///< 持续音放松起始时间
        float sustainedRelaxFullSeconds = 1.20f;   ///< 持续音完全放松时间
        float sustainedThresholdDropK = 0.65f;      ///< 持续音阈值降低系数
        float sustainedCooldownReduction = 0.35f;   ///< 持续音冷却缩减
        float sustainedGateDbOffset = 6.0f;        ///< 持续音门限 dB 偏移

        //--- 包络检测时间常数 ---
        float fastTauSeconds = 0.010f;       ///< 快包络时间常数（秒）：检测快速变化
        float slowTauSeconds = 0.170f;       ///< 慢包络时间常数（秒）：平滑背景电平
        float lowBandCutoffHz = 260.0f;      ///< 低频带截止频率（Hz）
        float midBandCutoffHz = 1800.0f;     ///< 中频带截止频率（Hz）

        //--- 阶段 A：起音优先方向锁定 ---
        float onsetLockBaseSeconds = 0.030f;  ///< 基础锁定时间
        float onsetLockByLevelSeconds = 0.050f; ///< 电平相关锁定时间
        float onsetLockBandVoteMix = 0.85f;   ///< 频段投票混合比例

        //--- 阶段 B：频段掩蔽与投票 ---
        float bandMaskTauSeconds = 0.22f;    ///< 频段掩蔽时间常数
        float bandMaskStrength = 0.70f;       ///< 频段掩蔽强度
        float bandVoteLowWeight = 0.70f;      ///< 低频段投票权重
        float bandVoteMidWeight = 1.00f;      ///< 中频段投票权重
        float bandVoteHighWeight = 1.35f;     ///< 高频段投票权重

        //--- 阶段 C：轻量级流追踪 ---
        float streamMatchDistance = 0.42f;    ///< 流匹配距离阈值
        float streamUpdateBlend = 0.55f;      ///< 流更新混合率
        float streamHoldSeconds = 0.60f;       ///< 流保持时间

        //--- 瞬态分类参数 ---
        float gunshotVeryLoudDb = -30.0f;    ///< 枪声很响阈值（dB）
        float gunshotLoudDb = -40.0f;         ///< 枪声响度阈值（dB）
        float gunshotBrightMin = 0.38f;      ///< 枪声高频占比最小值
        float gunshotVeryBrightMin = 0.52f;  ///< 枪声很高频占比最小值
        float gunshotOnsetMin = 0.75f;       ///< 枪声起音阈值最小值

        float reloadDbMin = -52.0f;          ///< 换弹检测最小响度（dB）
        float reloadHighRatioMin = 0.26f;    ///< 换弹高频占比最小值
    };

    //==============================================================================
    // 瞬态类型枚举
    
    /** @brief 瞬态事件类型 */
    enum class TransientClass : uint8_t
    {
        gunshot,   ///< 枪声：高频占比高、响度大
        footstep,  ///< 脚步声：中低频为主、响度中等
        reload     ///< 换弹：特定频谱特征
    };

    //==============================================================================
    // 瞬态事件结构体
    
    /**
     * @brief 瞬态事件数据结构
     * 
     * 当检测到瞬态时，会生成一个 TransientEvent 并放入队列，
     * 供 UI 层读取和显示。
     */
    struct TransientEvent
    {
        float balance = 0.0f;              ///< 声像位置 (-1.0 左 ~ +1.0 右)
        float level = 0.0f;               ///< 响度等级 (0.0 ~ 1.0，quiet ~ loud)
        float lowMidRatio = 0.0f;         ///< 低频/中频能量比 (0.0 ~ 1.0，越高低频越多)
        float directionConfidence = 0.0f;  ///< 方向置信度 (0.0 ~ 1.0，越高定位越可靠)
        uint32_t streamId = 0;            ///< 音频流 ID（0 表示未追踪）
        TransientClass transientClass =     ///< 瞬态类型（枪声/脚步/换弹）
            TransientClass::footstep;
    };

    //==============================================================================
    // 构造函数与析构函数
    
    /** @brief 构造函数 - 初始化音频捕获服务 */
    AudioCaptureService();
    /** @brief 析构函数 - 停止捕获并清理资源 */
    ~AudioCaptureService() override;

    //==============================================================================
    // 数据获取接口
    
    /** @brief 获取当前声像位置 (-1.0 左 ~ +1.0 右) */
    float getBalance() const noexcept;
    /** @brief 获取当前立体声宽度 (0.0 单声道 ~ 1.0 最大宽度) */
    float getWidth() const noexcept;
    /** @brief 获取当前左声道电平 (0.0 ~ 1.0) */
    float getLeftLevel() const noexcept;
    /** @brief 获取当前右声道电平 (0.0 ~ 1.0) */
    float getRightLevel() const noexcept;
    
    /** @brief 检查音频是否已就绪（设备已启动） */
    bool isAudioReady() const noexcept;
    /** @brief 获取最后一次错误信息 */
    juce::String getLastError() const;
    /** @brief 获取当前捕获模式（"WASAPI Loopback" 或 "Default Input"） */
    juce::String getCaptureMode() const;
    /** @brief 获取当前使用的音频设备名称 */
    juce::String getDeviceName() const;

    //==============================================================================
    // 参数获取与设置
    
    /** @brief 获取当前调优参数 */
    Tuning getTuning() const noexcept;
    /** @brief 设置调优参数 */
    void setTuning(const Tuning& tuning) noexcept;

    //==============================================================================
    // 事件队列接口
    
    /**
     * @brief 弹出下一个瞬态事件
     * @param[out] event 用于存储弹出的事件
     * @return 如果队列中有事件则返回 true，否则返回 false
     */
    bool popNextTransientEvent(TransientEvent& event) noexcept;

private:
    //==============================================================================
    // JUCE AudioIODeviceCallback 接口实现
    
    /**
     * @brief 音频设备 IO 回调（核心音频处理入口）
     * 
     * 这是音频数据处理的核心入口，每次音频设备产生新数据时被调用。
     * 
     * @param inputChannelData 输入通道数据（来自麦克风或环回捕获）
     * @param numInputChannels 输入通道数
     * @param outputChannelData 输出通道数据（本应用不需要输出）
     * @param numOutputChannels 输出通道数
     * @param numSamples 本次处理的采样点数
     * @param context 音频回调上下文信息
     */
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    /** @brief 音频设备即将开始播放 */
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    /** @brief 音频设备已停止 */
    void audioDeviceStopped() override;

    //==============================================================================
    // 核心音频处理
    
    /**
     * @brief 处理一个立体声音频块
     * 
     * 这是瞬态检测的核心处理函数，对每个音频块进行：
     * - 多频段能量分析
     * - 瞬态评分计算
     * - 瞬态检测与分类
     * 
     * @param left 左声道采样数据
     * @param right 右声道采样数据
     * @param numSamples 采样点数
     * @param sampleRate 采样率
     */
    void processStereoBlock(const float* left, const float* right, int numSamples, double sampleRate);
    
    /**
     * @brief 瞬态分类
     * @param blockDb 当前块的响度（dB）
     * @param highFreqRatio 高频能量占比
     * @param onsetContrast 起音对比度
     * @return 瞬态类型（枪声/脚步/换弹）
     */
    TransientClass classifyTransient(float blockDb, float highFreqRatio, float onsetContrast) const noexcept;
    
    /** @brief 准备音频分析（分配缓冲区、重置状态） */
    void prepareAnalysis(double sampleRate);
    /** @brief 重置分析状态 */
    void resetAnalysis();

    //==============================================================================
    // 捕获启动
    
    /** @brief 启动 WASAPI 环回捕获（捕获系统音频输出） */
    bool startLoopbackCapture();
    /** @brief 启动 JUCE 默认输入捕获（备用方案） */
    bool startDefaultInputCapture();
    
    /** @brief 将瞬态事件推入队列（线程安全） */
    void pushTransientEvent(const TransientEvent& event) noexcept;

    //==============================================================================
    // 流追踪
    
    /**
     * @brief 更新流状态（用于连发追踪）
     * @param proposedBalance 提议的声像位置
     * @param confidence 方向置信度
     * @param dtSeconds 距离上次的时间（秒）
     * @return 流 ID
     */
    uint32_t updateStreamState(float proposedBalance, float confidence, float dtSeconds) noexcept;

    //==============================================================================
    // 成员变量 - 音频设备
    
    juce::AudioDeviceManager deviceManager;               ///< JUCE 音频设备管理器
    std::unique_ptr<WasapiLoopbackCapture> loopbackCapture; ///< WASAPI 环回捕获（Windows 专用）

    //==============================================================================
    // 成员变量 - 实时数据（原子变量，线程安全）
    
    std::atomic<float> balance { 0.0f };    ///< 当前声像位置
    std::atomic<float> width { 0.0f };      ///< 当前立体声宽度
    std::atomic<float> leftLevel { 0.0f };  ///< 当前左声道电平
    std::atomic<float> rightLevel { 0.0f }; ///< 当前右声道电平
    std::atomic<bool> audioReady { false };  ///< 音频设备是否已就绪
    std::atomic<double> activeSampleRate { 0.0 }; ///< 当前采样率

    //==============================================================================
    // 成员变量 - 包络状态
    
    double alpha = 0.0;   ///< 低通滤波系数
    double sLL = 0.0;     ///< 左声道慢包络平方（用于电平计算）
    double sRR = 0.0;     ///< 右声道慢包络平方
    double sLR = 0.0;     ///< 左右互相关（用于宽度计算）
    double sMM = 0.0;     ///< 中置声道（M = L+R）慢包络平方
    double sSS = 0.0;     ///< 侧边声道（S = L-R）慢包络平方

    //==============================================================================
    // 成员变量 - 瞬态检测状态
    
    double fastTransientEnv = 0.0;      ///< 瞬态快包络
    double slowTransientEnv = 0.0;      ///< 瞬态慢包络
    double transientCooldownSeconds = 0.0; ///< 瞬态冷却计时器
    bool transientArmed = true;          ///< 瞬态检测是否武装（可以触发）
    double unarmedSeconds = 0.0;         ///< 未武装时间累计

    //==============================================================================
    // 成员变量 - 自适应评分
    
    double scoreMean = 0.0;             ///< 评分均值（用于自适应阈值）
    double scoreVar = 1.0;              ///< 评分方差
    double previousLogMonoEnergy = 0.0;  ///< 上一次的对数单声道能量
    double previousLogHighResidual = 0.0; ///< 上一次的对数高频残差

    //==============================================================================
    // 成员变量 - 连发检测
    
    double timeSinceLastTriggerSeconds = 10.0; ///< 距离上次触发的时间
    double burstIoiMeanSeconds = 0.095;       ///< 连发平均间隔（秒）
    double burstConfidence = 0.0;             ///< 连发置信度
    double sustainedHighSeconds = 0.0;        ///< 持续高电平时间

    //==============================================================================
    // 成员变量 - 滤波状态
    
    double transientHpState = 0.0;         ///< 瞬态高通滤波器状态
    double transientHpPreviousInput = 0.0;  ///< 瞬态高通滤波器上一输入

    //==============================================================================
    // 成员变量 - 方向锁定
    
    double onsetLockRemainingSeconds = 0.0; ///< 起音锁定剩余时间
    float onsetLockBalance = 0.0f;         ///< 起音锁定的声像位置
    float onsetLockConfidence = 0.0f;      ///< 起音锁定的置信度

    //==============================================================================
    // 成员变量 - 频段分析状态
    
    double lowBandState = 0.0;   ///< 低频带滤波器状态
    double lowBandStateL = 0.0;  ///< 左声道低频状态
    double lowBandStateR = 0.0;  ///< 右声道低频状态
    double midBandStateL = 0.0;  ///< 左声道中频状态
    double midBandStateR = 0.0;  ///< 右声道中频状态
    double maskLow = 0.0;        ///< 低频掩蔽量
    double maskMid = 0.0;        ///< 中频掩蔽量
    double maskHigh = 0.0;       ///< 高频掩蔽量

    //==============================================================================
    // 成员变量 - 流追踪状态
    
    struct StreamState                   ///< 音频流状态结构体
    {
        bool active = false;             ///< 流是否活跃
        uint32_t id = 0;                ///< 流 ID
        float balance = 0.0f;           ///< 流的声像位置
        float confidence = 0.0f;        ///< 流的置信度
        float holdSeconds = 0.0f;       ///< 流保持时间
    };
    
    std::array<StreamState, 4> streamStates {}; ///< 流状态数组（最多追踪 4 个流）
    uint32_t nextStreamId = 1;                  ///< 下一个流 ID

    //==============================================================================
    // 成员变量 - 调优参数（原子变量，线程安全）
    
    // 使用原子变量存储调优参数，确保音频线程和 UI 线程的安全访问
    std::atomic<float> tuningLoudEnoughDb { -62.0f };
    std::atomic<float> tuningOnsetContrastStart { 0.55f };
    std::atomic<float> tuningOnsetRatioStart { 1.10f };
    std::atomic<float> tuningOnsetContrastSettle { 0.45f };
    std::atomic<float> tuningOnsetRatioSettle { 1.10f };
    std::atomic<float> tuningCooldownBaseSeconds { 0.050f };
    std::atomic<float> tuningCooldownByLevelSeconds { 0.070f };
    std::atomic<float> tuningScoreEnergyFluxWeight { 0.95f };
    std::atomic<float> tuningScoreHighFluxWeight { 1.10f };
    std::atomic<float> tuningScoreContrastWeight { 1.25f };
    std::atomic<float> tuningScoreAdaptTauSeconds { 0.45f };
    std::atomic<float> tuningScoreHighThresholdK { 2.15f };
    std::atomic<float> tuningScoreLowThresholdK { 0.95f };
    std::atomic<float> tuningForcedRearmSeconds { 0.120f };
    std::atomic<float> tuningBurstIoiBlend { 0.30f };
    std::atomic<float> tuningBurstDecayPerSecond { 0.70f };
    std::atomic<float> tuningBurstThresholdDropScale { 0.40f };
    std::atomic<float> tuningBurstCooldownReduction { 0.35f };
    std::atomic<float> tuningBurstMinCooldownScale { 0.55f };
    std::atomic<float> tuningTransientHighpassHz { 180.0f };
    std::atomic<float> tuningSustainedRelaxStartSeconds { 0.15f };
    std::atomic<float> tuningSustainedRelaxFullSeconds { 1.20f };
    std::atomic<float> tuningSustainedThresholdDropK { 0.65f };
    std::atomic<float> tuningSustainedCooldownReduction { 0.35f };
    std::atomic<float> tuningSustainedGateDbOffset { 6.0f };
    std::atomic<float> tuningFastTauSeconds { 0.010f };
    std::atomic<float> tuningSlowTauSeconds { 0.170f };
    std::atomic<float> tuningLowBandCutoffHz { 260.0f };
    std::atomic<float> tuningMidBandCutoffHz { 1800.0f };
    std::atomic<float> tuningOnsetLockBaseSeconds { 0.030f };
    std::atomic<float> tuningOnsetLockByLevelSeconds { 0.050f };
    std::atomic<float> tuningOnsetLockBandVoteMix { 0.85f };
    std::atomic<float> tuningBandMaskTauSeconds { 0.22f };
    std::atomic<float> tuningBandMaskStrength { 0.70f };
    std::atomic<float> tuningBandVoteLowWeight { 0.70f };
    std::atomic<float> tuningBandVoteMidWeight { 1.00f };
    std::atomic<float> tuningBandVoteHighWeight { 1.35f };
    std::atomic<float> tuningStreamMatchDistance { 0.42f };
    std::atomic<float> tuningStreamUpdateBlend { 0.55f };
    std::atomic<float> tuningStreamHoldSeconds { 0.60f };
    std::atomic<float> tuningGunshotVeryLoudDb { -30.0f };
    std::atomic<float> tuningGunshotLoudDb { -40.0f };
    std::atomic<float> tuningGunshotBrightMin { 0.38f };
    std::atomic<float> tuningGunshotVeryBrightMin { 0.52f };
    std::atomic<float> tuningGunshotOnsetMin { 0.75f };
    std::atomic<float> tuningReloadDbMin { -52.0f };
    std::atomic<float> tuningReloadHighRatioMin { 0.26f };

    //==============================================================================
    // 成员变量 - 瞬态事件队列
    
    static constexpr uint32_t transientQueueSize = 128; ///< 事件队列大小
    std::array<TransientEvent, transientQueueSize> transientQueue {}; ///< 事件环形队列
    std::atomic<uint32_t> transientWriteIndex { 0 }; ///< 队列写指针
    std::atomic<uint32_t> transientReadIndex { 0 };  ///< 队列读指针

    //==============================================================================
    // 成员变量 - 状态信息
    
    juce::String lastError;                  ///< 最后一次错误信息
    juce::String captureMode { "Unavailable" }; ///< 当前捕获模式
    juce::String currentDeviceName { "(none)" }; ///< 当前设备名称
};
