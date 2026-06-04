/**
 * @file AudioCaptureService.cpp
 * @brief SeePosition 音频捕获服务实现文件
 * 
 * 本文件包含 AudioCaptureService 类的实现，负责：
 * - 音频设备初始化和管理（WASAPI 环回捕获 / 默认输入设备）
 * - 实时音频分析（声像、宽度、音量）
 * - 瞬态检测算法（基于快慢包络对比）
 * - 瞬态分类（枪声、脚步声、换弹）
 * - 多频段能量分析（低频、中频、高频）
 * - 连发检测和节奏学习
 * 
 * @author iisaacbeats
 * @date 2024
 */

#include "AudioCaptureService.h"

#include <cmath>

/**
 * @brief 构造函数
 * 
 * 初始化音频捕获服务：
 * 1. 尝试启动 WASAPI 环回捕获（捕获系统播放的音频）
 * 2. 如果失败，回退到默认输入设备捕获
 * 3. 如果都失败，设置 captureMode 为 "Unavailable"
 */
AudioCaptureService::AudioCaptureService()
{
    if (startLoopbackCapture())
    {
        audioReady.store(true);
        return;
    }

    const auto loopbackError = lastError;
    if (startDefaultInputCapture())
    {
        audioReady.store(true);
        captureMode = "Default Input (Loopback fallback)";
        if (loopbackError.isNotEmpty())
            lastError = loopbackError;
        return;
    }

    captureMode = "Unavailable";
}

/**
 * @brief 析构函数
 * 
 * 清理工作：
 * 1. 移除音频回调
 * 2. 停止 WASAPI 环回捕获
 */
AudioCaptureService::~AudioCaptureService()
{
    deviceManager.removeAudioCallback(this);

    if (loopbackCapture != nullptr)
        loopbackCapture->stop();
}

/**
 * @brief 获取当前声像位置
 * @return 声像位置（-1.0 = 最左，0.0 = 中央，1.0 = 最右）
 * 
 * 使用 std::atomic 保证线程安全。
 */
float AudioCaptureService::getBalance() const noexcept
{
    return balance.load();
}

/**
 * @brief 获取当前立体声宽度
 * @return 立体声宽度（0.0 = 单声道，1.0 = 最大宽度）
 * 
 * 使用 std::atomic 保证线程安全。
 */
float AudioCaptureService::getWidth() const noexcept
{
    return width.load();
}

/**
 * @brief 获取左声道音量
 * @return 左声道音量（0.0 ~ 1.0）
 * 
 * 使用 std::atomic 保证线程安全。
 */
float AudioCaptureService::getLeftLevel() const noexcept
{
    return leftLevel.load();
}

/**
 * @brief 获取右声道音量
 * @return 右声道音量（0.0 ~ 1.0）
 * 
 * 使用 std::atomic 保证线程安全。
 */
float AudioCaptureService::getRightLevel() const noexcept
{
    return rightLevel.load();
}

/**
 * @brief 查询音频设备是否就绪
 * @return 是否就绪
 */
bool AudioCaptureService::isAudioReady() const noexcept
{
    return audioReady.load();
}

/**
 * @brief 获取最后一次错误信息
 * @return 错误描述字符串
 */
juce::String AudioCaptureService::getLastError() const
{
    return lastError;
}

/**
 * @brief 获取当前捕获模式
 * @return 捕获模式字符串（"WASAPI Loopback" / "Default Input" / "Unavailable"）
 */
juce::String AudioCaptureService::getCaptureMode() const
{
    return captureMode;
}

/**
 * @brief 获取当前音频设备名称
 * @return 设备名称
 */
juce::String AudioCaptureService::getDeviceName() const
{
    return currentDeviceName;
}

/**
 * @brief 获取当前调优参数
 * @return Tuning 结构体（包含所有音频分析算法的参数）
 * 
 * 从原子变量中读取所有调优参数并写入 Tuning 结构体。
 * 使用 std::atomic 保证线程安全。
 */
AudioCaptureService::Tuning AudioCaptureService::getTuning() const noexcept
{
    Tuning t;
    t.loudEnoughDb = tuningLoudEnoughDb.load();
    t.onsetContrastStart = tuningOnsetContrastStart.load();
    t.onsetRatioStart = tuningOnsetRatioStart.load();
    t.onsetContrastSettle = tuningOnsetContrastSettle.load();
    t.onsetRatioSettle = tuningOnsetRatioSettle.load();
    t.cooldownBaseSeconds = tuningCooldownBaseSeconds.load();
    t.cooldownByLevelSeconds = tuningCooldownByLevelSeconds.load();
    t.scoreEnergyFluxWeight = tuningScoreEnergyFluxWeight.load();
    t.scoreHighFluxWeight = tuningScoreHighFluxWeight.load();
    t.scoreContrastWeight = tuningScoreContrastWeight.load();
    t.scoreAdaptTauSeconds = tuningScoreAdaptTauSeconds.load();
    t.scoreHighThresholdK = tuningScoreHighThresholdK.load();
    t.scoreLowThresholdK = tuningScoreLowThresholdK.load();
    t.forcedRearmSeconds = tuningForcedRearmSeconds.load();
    t.burstIoiBlend = tuningBurstIoiBlend.load();
    t.burstDecayPerSecond = tuningBurstDecayPerSecond.load();
    t.burstThresholdDropScale = tuningBurstThresholdDropScale.load();
    t.burstCooldownReduction = tuningBurstCooldownReduction.load();
    t.burstMinCooldownScale = tuningBurstMinCooldownScale.load();
    t.transientHighpassHz = tuningTransientHighpassHz.load();
    t.sustainedRelaxStartSeconds = tuningSustainedRelaxStartSeconds.load();
    t.sustainedRelaxFullSeconds = tuningSustainedRelaxFullSeconds.load();
    t.sustainedThresholdDropK = tuningSustainedThresholdDropK.load();
    t.sustainedCooldownReduction = tuningSustainedCooldownReduction.load();
    t.sustainedGateDbOffset = tuningSustainedGateDbOffset.load();
    t.fastTauSeconds = tuningFastTauSeconds.load();
    t.slowTauSeconds = tuningSlowTauSeconds.load();
    t.lowBandCutoffHz = tuningLowBandCutoffHz.load();
    t.midBandCutoffHz = tuningMidBandCutoffHz.load();
    t.onsetLockBaseSeconds = tuningOnsetLockBaseSeconds.load();
    t.onsetLockByLevelSeconds = tuningOnsetLockByLevelSeconds.load();
    t.onsetLockBandVoteMix = tuningOnsetLockBandVoteMix.load();
    t.bandMaskTauSeconds = tuningBandMaskTauSeconds.load();
    t.bandMaskStrength = tuningBandMaskStrength.load();
    t.bandVoteLowWeight = tuningBandVoteLowWeight.load();
    t.bandVoteMidWeight = tuningBandVoteMidWeight.load();
    t.bandVoteHighWeight = tuningBandVoteHighWeight.load();
    t.streamMatchDistance = tuningStreamMatchDistance.load();
    t.streamUpdateBlend = tuningStreamUpdateBlend.load();
    t.streamHoldSeconds = tuningStreamHoldSeconds.load();
    t.gunshotVeryLoudDb = tuningGunshotVeryLoudDb.load();
    t.gunshotLoudDb = tuningGunshotLoudDb.load();
    t.gunshotBrightMin = tuningGunshotBrightMin.load();
    t.gunshotVeryBrightMin = tuningGunshotVeryBrightMin.load();
    t.gunshotOnsetMin = tuningGunshotOnsetMin.load();
    t.reloadDbMin = tuningReloadDbMin.load();
    t.reloadHighRatioMin = tuningReloadHighRatioMin.load();
    return t;
}

/**
 * @brief 设置调优参数
 * @param tuning 新的调优参数
 * 
 * 将所有调优参数写入原子变量，并确保参数在合理范围内。
 * 使用 std::atomic 保证线程安全。
 * 
 * 参数分类：
 * - 瞬态检测：loudEnoughDb, onsetContrastStart, cooldownBaseSeconds 等
 * - 连发检测：burstIoiBlend, burstDecayPerSecond 等
 * - 频段分析：lowBandCutoffHz, midBandCutoffHz 等
 * - 瞬态分类：gunshotVeryLoudDb, reloadDbMin 等
 */
void AudioCaptureService::setTuning(const Tuning& tuning) noexcept
{
    tuningLoudEnoughDb.store(tuning.loudEnoughDb);
    tuningOnsetContrastStart.store(juce::jmax(0.01f, tuning.onsetContrastStart));
    tuningOnsetRatioStart.store(juce::jmax(1.0f, tuning.onsetRatioStart));
    tuningOnsetContrastSettle.store(juce::jmax(0.0f, tuning.onsetContrastSettle));
    tuningOnsetRatioSettle.store(juce::jmax(1.0f, tuning.onsetRatioSettle));
    tuningCooldownBaseSeconds.store(juce::jmax(0.0f, tuning.cooldownBaseSeconds));
    tuningCooldownByLevelSeconds.store(juce::jmax(0.0f, tuning.cooldownByLevelSeconds));
    tuningScoreEnergyFluxWeight.store(juce::jlimit(0.0f, 4.0f, tuning.scoreEnergyFluxWeight));
    tuningScoreHighFluxWeight.store(juce::jlimit(0.0f, 4.0f, tuning.scoreHighFluxWeight));
    tuningScoreContrastWeight.store(juce::jlimit(0.0f, 4.0f, tuning.scoreContrastWeight));
    tuningScoreAdaptTauSeconds.store(juce::jlimit(0.05f, 2.0f, tuning.scoreAdaptTauSeconds));
    tuningScoreHighThresholdK.store(juce::jlimit(0.2f, 5.0f, tuning.scoreHighThresholdK));
    tuningScoreLowThresholdK.store(juce::jlimit(-1.0f, 4.0f, tuning.scoreLowThresholdK));
    tuningForcedRearmSeconds.store(juce::jlimit(0.02f, 0.40f, tuning.forcedRearmSeconds));
    tuningBurstIoiBlend.store(juce::jlimit(0.01f, 1.0f, tuning.burstIoiBlend));
    tuningBurstDecayPerSecond.store(juce::jlimit(0.05f, 4.0f, tuning.burstDecayPerSecond));
    tuningBurstThresholdDropScale.store(juce::jlimit(0.0f, 1.5f, tuning.burstThresholdDropScale));
    tuningBurstCooldownReduction.store(juce::jlimit(0.0f, 0.95f, tuning.burstCooldownReduction));
    tuningBurstMinCooldownScale.store(juce::jlimit(0.20f, 1.0f, tuning.burstMinCooldownScale));
    tuningTransientHighpassHz.store(juce::jlimit(40.0f, 1200.0f, tuning.transientHighpassHz));
    tuningSustainedRelaxStartSeconds.store(juce::jlimit(0.0f, 2.0f, tuning.sustainedRelaxStartSeconds));
    tuningSustainedRelaxFullSeconds.store(juce::jlimit(0.05f, 4.0f, tuning.sustainedRelaxFullSeconds));
    tuningSustainedThresholdDropK.store(juce::jlimit(0.0f, 2.5f, tuning.sustainedThresholdDropK));
    tuningSustainedCooldownReduction.store(juce::jlimit(0.0f, 0.95f, tuning.sustainedCooldownReduction));
    tuningSustainedGateDbOffset.store(juce::jlimit(0.0f, 24.0f, tuning.sustainedGateDbOffset));
    tuningFastTauSeconds.store(juce::jmax(0.001f, tuning.fastTauSeconds));
    tuningSlowTauSeconds.store(juce::jmax(0.001f, tuning.slowTauSeconds));
    tuningLowBandCutoffHz.store(juce::jlimit(60.0f, 2000.0f, tuning.lowBandCutoffHz));
    tuningMidBandCutoffHz.store(juce::jlimit(500.0f, 8000.0f, tuning.midBandCutoffHz));
    tuningOnsetLockBaseSeconds.store(juce::jlimit(0.0f, 0.25f, tuning.onsetLockBaseSeconds));
    tuningOnsetLockByLevelSeconds.store(juce::jlimit(0.0f, 0.45f, tuning.onsetLockByLevelSeconds));
    tuningOnsetLockBandVoteMix.store(juce::jlimit(0.0f, 1.0f, tuning.onsetLockBandVoteMix));
    tuningBandMaskTauSeconds.store(juce::jlimit(0.01f, 2.0f, tuning.bandMaskTauSeconds));
    tuningBandMaskStrength.store(juce::jlimit(0.0f, 3.0f, tuning.bandMaskStrength));
    tuningBandVoteLowWeight.store(juce::jlimit(0.0f, 3.0f, tuning.bandVoteLowWeight));
    tuningBandVoteMidWeight.store(juce::jlimit(0.0f, 3.0f, tuning.bandVoteMidWeight));
    tuningBandVoteHighWeight.store(juce::jlimit(0.0f, 3.0f, tuning.bandVoteHighWeight));
    tuningStreamMatchDistance.store(juce::jlimit(0.05f, 2.0f, tuning.streamMatchDistance));
    tuningStreamUpdateBlend.store(juce::jlimit(0.01f, 1.0f, tuning.streamUpdateBlend));
    tuningStreamHoldSeconds.store(juce::jlimit(0.05f, 3.0f, tuning.streamHoldSeconds));
    tuningGunshotVeryLoudDb.store(tuning.gunshotVeryLoudDb);
    tuningGunshotLoudDb.store(tuning.gunshotLoudDb);
    tuningGunshotBrightMin.store(juce::jlimit(0.0f, 1.0f, tuning.gunshotBrightMin));
    tuningGunshotVeryBrightMin.store(juce::jlimit(0.0f, 1.0f, tuning.gunshotVeryBrightMin));
    tuningGunshotOnsetMin.store(juce::jmax(0.0f, tuning.gunshotOnsetMin));
    tuningReloadDbMin.store(tuning.reloadDbMin);
    tuningReloadHighRatioMin.store(juce::jlimit(0.0f, 1.0f, tuning.reloadHighRatioMin));
}

/**
 * @brief 从瞬态事件队列中弹出一个事件
 * @param event [输出] 接收到的瞬态事件
 * @return 是否成功弹出（队列非空）
 * 
 * 使用无锁环形缓冲区实现线程安全的事件传递。
 * 音频线程写入，UI 线程读取。
 */
bool AudioCaptureService::popNextTransientEvent(TransientEvent& event) noexcept
{
    const auto read = transientReadIndex.load(std::memory_order_relaxed);
    const auto write = transientWriteIndex.load(std::memory_order_acquire);

    if (read == write)
        return false;

    event = transientQueue[read];
    transientReadIndex.store((read + 1) % transientQueueSize, std::memory_order_release);
    return true;
}

/**
 * @brief JUCE 音频 IO 回调函数（核心音频处理入口）
 * @param inputChannelData 输入通道数据（麦克风或环回捕获）
 * @param numInputChannels 输入通道数
 * @param outputChannelData 输出通道数据（未使用，需要清空）
 * @param numOutputChannels 输出通道数
 * @param numSamples 本次处理的采样点数
 * @param context JUCE 音频回调上下文（未使用）
 * 
 * 功能：
 * 1. 清空输出缓冲区（本应用不需要输出声音）
 * 2. 检查输入数据有效性
 * 3. 获取左声道和右声道数据（如果只有单声道，右声道复用左声道）
 * 4. 调用 processStereoBlock() 进行音频分析
 */
void AudioCaptureService::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                           int numInputChannels,
                                                           float* const* outputChannelData,
                                                           int numOutputChannels,
                                                           int numSamples,
                                                           const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
    }

    if (numSamples <= 0 || numInputChannels <= 0 || inputChannelData == nullptr)
        return;

    const float* left = inputChannelData[0];
    const float* right = (numInputChannels > 1) ? inputChannelData[1] : inputChannelData[0];

    if (left == nullptr || right == nullptr)
        return;

    processStereoBlock(left, right, numSamples, activeSampleRate.load());
}

/**
 * @brief 音频设备即将开始播放
 * @param device 即将启动的音频设备对象
 * 
 * 功能：
 * 1. 获取采样率
 * 2. 记录设备名称
 * 3. 调用 prepareAnalysis() 初始化分析状态
 */
void AudioCaptureService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto sampleRate = (device != nullptr) ? device->getCurrentSampleRate() : 48000.0;
    if (device != nullptr)
        currentDeviceName = device->getName();

    activeSampleRate.store(sampleRate);
    prepareAnalysis(sampleRate);
}

/**
 * @brief 音频设备已停止
 * 
 * 功能：
 * 调用 resetAnalysis() 重置所有分析状态。
 */
void AudioCaptureService::audioDeviceStopped()
{
    resetAnalysis();
}

/**
 * @brief 处理一个立体声音频块（核心音频分析函数）
 * @param left 左声道采样数据
 * @param right 右声道采样数据
 * @param numSamples 采样点数
 * @param sampleRate 采样率（Hz）
 * 
 * 本函数是整个音频分析的核心，执行以下处理：
 * 
 * 1. 参数校验和采样率更新
 * 2. 初始化各种状态变量（RMS、频段状态、高通状态等）
 * 3. 逐采样点处理：
 *    - 计算 M/S 矩阵（Mid/Side）
 *    - 前端单声道抑制（mono rejection）
 *    - 更新 RMS 积分（LL, RR, LR, MM, SS）
 *    - 更新频段滤波状态（低通、带通）
 *    - 高通滤波（用于瞬态检测）
 * 4. 计算块级结果：
 *    - 左右 RMS、峰值、声像、宽度
 *    - 低频、中频、高频能量
 *    - 频段平衡（用于方向估计）
 * 5. 更新显示值（balance, width, leftLevel, rightLevel）
 * 6. 瞬态检测算法：
 *    - 快慢包络对比（fast envelope vs slow envelope）
 *    - 评分系统（能量流量、高频流量、对比度）
 *    - 环境自适应（动态调整阈值）
 *    - 连发检测（节奏学习）
 *    - 持续高能放宽（混战场景优化）
 * 7. 触发条件满足时：
 *    - 分类瞬态（枪声/脚步声/换弹）
 *    - 更新声流状态（stream tracking）
 *    - 推入事件队列（线程安全）
 *    - 设置冷却时间
 */
void AudioCaptureService::processStereoBlock(const float* left,
                                             const float* right,
                                             int numSamples,
                                             double sampleRate)
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    if (sampleRate <= 1.0)
        sampleRate = 48000.0;

    const auto cachedRate = activeSampleRate.load();
    if (std::abs(cachedRate - sampleRate) > 0.5)
    {
        activeSampleRate.store(sampleRate);
        prepareAnalysis(sampleRate);
    }

    const double a = alpha;
    const double oneMinusA = 1.0 - a;

    double ll = sLL;
    double rr = sRR;
    double lr = sLR;
    double mm = sMM;
    double ss = sSS;

    double blockLeftEnergy = 0.0;
    double blockRightEnergy = 0.0;
    double blockLowEnergy = 0.0;
    double blockMidEnergy = 0.0;
    double blockHighEnergy = 0.0;
    double blockLowEnergyL = 0.0;
    double blockLowEnergyR = 0.0;
    double blockMidEnergyL = 0.0;
    double blockMidEnergyR = 0.0;
    double blockHighEnergyL = 0.0;
    double blockHighEnergyR = 0.0;
    float blockLeftPeak = 0.0f;
    float blockRightPeak = 0.0f;
    double blockTransientEnergy = 0.0;

    double lowState = lowBandState;
    double lowStateL = lowBandStateL;
    double lowStateR = lowBandStateR;
    double midStateL = midBandStateL;
    double midStateR = midBandStateR;
    const auto lowCutHz = (double)tuningLowBandCutoffHz.load();
    const auto midCutHz = (double)juce::jmax(tuningLowBandCutoffHz.load() + 120.0f, tuningMidBandCutoffHz.load());
    const double lowAlpha = juce::jlimit(0.0,
                                         1.0,
                                         1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * lowCutHz
                                                        / juce::jmax(1.0, sampleRate)));
    const double midAlpha = juce::jlimit(0.0,
                                         1.0,
                                         1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * midCutHz
                                                        / juce::jmax(1.0, sampleRate)));
    const auto transientHighpassHz = (double)tuningTransientHighpassHz.load();
    const double hpDt = 1.0 / juce::jmax(1.0, sampleRate);
    const double hpRc = 1.0 / (2.0 * juce::MathConstants<double>::pi * juce::jmax(1.0, transientHighpassHz));
    const double hpCoeff = juce::jlimit(0.0, 1.0, hpRc / (hpRc + hpDt));
    double hpState = transientHpState;
    double hpPrevInput = transientHpPreviousInput;

    for (int i = 0; i < numSamples; ++i)
    {
        // Front-end mono rejection: when side information is near zero,
        // strongly attenuate the mono component before all analysis stages.
        const double inL = left[i];
        const double inR = right[i];
        const double rawM = (inL + inR) * 0.5;
        const double rawS = (inL - inR) * 0.5;
        constexpr double monoRejectStart = 0.012;
        constexpr double monoRejectFull = 0.045;
        const double sideToMono = std::abs(rawS) / (std::abs(rawM) + 1.0e-9);
        const double keepMono = juce::jlimit(0.0,
                                             1.0,
                                             (sideToMono - monoRejectStart) / (monoRejectFull - monoRejectStart));
        const double M = rawM * keepMono;
        const double S = rawS;
        const double L = M + S;
        const double R = M - S;

        ll = oneMinusA * ll + a * L * L;
        rr = oneMinusA * rr + a * R * R;
        lr = oneMinusA * lr + a * L * R;
        mm = oneMinusA * mm + a * M * M;
        ss = oneMinusA * ss + a * S * S;

        blockLeftEnergy += L * L;
        blockRightEnergy += R * R;

        blockLeftPeak = juce::jmax(blockLeftPeak, std::abs((float)L));
        blockRightPeak = juce::jmax(blockRightPeak, std::abs((float)R));

        const double mono = 0.5 * (L + R);
        hpState = hpCoeff * (hpState + mono - hpPrevInput);
        hpPrevInput = mono;
        blockTransientEnergy += hpState * hpState;
        lowState += lowAlpha * (mono - lowState);
        const double highMono = mono - lowState;
        blockLowEnergy += lowState * lowState;
        blockHighEnergy += highMono * highMono;

        lowStateL += lowAlpha * (L - lowStateL);
        lowStateR += lowAlpha * (R - lowStateR);
        midStateL += midAlpha * (L - midStateL);
        midStateR += midAlpha * (R - midStateR);

        const double lowL = lowStateL;
        const double lowR = lowStateR;
        const double midL = midStateL - lowStateL;
        const double midR = midStateR - lowStateR;
        const double highL = L - midStateL;
        const double highR = R - midStateR;

        blockLowEnergyL += lowL * lowL;
        blockLowEnergyR += lowR * lowR;
        blockMidEnergyL += midL * midL;
        blockMidEnergyR += midR * midR;
        blockHighEnergyL += highL * highL;
        blockHighEnergyR += highR * highR;

        blockMidEnergy += 0.5 * (midL * midL + midR * midR);
    }

    lowBandState = lowState;
    lowBandStateL = lowStateL;
    lowBandStateR = lowStateR;
    midBandStateL = midStateL;
    midBandStateR = midStateR;
    transientHpState = hpState;
    transientHpPreviousInput = hpPrevInput;

    sLL = ll;
    sRR = rr;
    sLR = lr;
    sMM = mm;
    sSS = ss;

    const float blockLeftRms = std::sqrt((float)(blockLeftEnergy / juce::jmax(1, numSamples)));
    const float blockRightRms = std::sqrt((float)(blockRightEnergy / juce::jmax(1, numSamples)));
    const float blockTransientRms = std::sqrt((float)(blockTransientEnergy / juce::jmax(1, numSamples)));
    const float blockMonoRms = 0.5f * (blockLeftRms + blockRightRms);
    const float blockBalance = (blockRightRms - blockLeftRms) / (blockRightRms + blockLeftRms + 1.0e-6f);
    const float blockLowRms = std::sqrt((float)(blockLowEnergy / juce::jmax(1, numSamples)));
    const float blockHighRms = std::sqrt((float)(blockHighEnergy / juce::jmax(1, numSamples)));
    const float blockMidRms = std::sqrt((float)(blockMidEnergy / juce::jmax(1, numSamples)));
    const float highFreqRatio = blockHighRms / (blockLowRms + blockMidRms + blockHighRms + 1.0e-6f);
    const float lowMidRatio = (blockLowRms + 0.45f * blockMidRms) / (blockLowRms + blockMidRms + blockHighRms + 1.0e-6f);

    const float lowBalance = (float)((blockLowEnergyR - blockLowEnergyL)
                                     / (blockLowEnergyR + blockLowEnergyL + 1.0e-9));
    const float midBalance = (float)((blockMidEnergyR - blockMidEnergyL)
                                     / (blockMidEnergyR + blockMidEnergyL + 1.0e-9));
    const float highBalance = (float)((blockHighEnergyR - blockHighEnergyL)
                                      / (blockHighEnergyR + blockHighEnergyL + 1.0e-9));

    float targetWidth = 0.0f;
    float targetBalance = 0.0f;
    float targetLeftLevel = 0.0f;
    float targetRightLevel = 0.0f;

    const double totalEnergy = sLL + sRR;
    const bool hasSignal = totalEnergy > 1.0e-8;

    if (hasSignal)
    {
        const double totalMS = sMM + sSS;
        targetWidth = (totalMS > 1.0e-20)
                          ? (float)juce::jlimit(0.0, 1.0, sSS / totalMS)
                          : 0.0f;

        targetBalance = (float)juce::jlimit(-1.0, 1.0, (sRR - sLL) / juce::jmax(1.0e-20, totalEnergy));
        if (std::abs(targetBalance) < 0.03f)
            targetBalance = 0.0f;

        // Real-time meter uses per-block peak to catch short transients.
        const float leftDb = juce::Decibels::gainToDecibels(blockLeftPeak, -100.0f);
        const float rightDb = juce::Decibels::gainToDecibels(blockRightPeak, -100.0f);
        targetLeftLevel = juce::jlimit(0.0f, 1.0f, (leftDb + 60.0f) / 60.0f);
        targetRightLevel = juce::jlimit(0.0f, 1.0f, (rightDb + 60.0f) / 60.0f);
    }

    const float blockSeconds = (float)numSamples / (float)sampleRate;

    for (auto& s : streamStates)
    {
        if (!s.active)
            continue;
        s.holdSeconds -= blockSeconds;
        if (s.holdSeconds <= 0.0f)
            s.active = false;
    }

    onsetLockRemainingSeconds = juce::jmax(0.0, onsetLockRemainingSeconds - blockSeconds);

    // Real-time display path: publish current block result directly.
    balance.store(juce::jlimit(-1.0f, 1.0f, targetBalance));
    width.store(juce::jlimit(0.0f, 1.0f, targetWidth));
    leftLevel.store(targetLeftLevel);
    rightLevel.store(targetRightLevel);

    // Transient detector: fast-vs-slow envelope with cooldown to avoid reverb retrigger spam.
    const auto fastTau = juce::jmax(0.001f, tuningFastTauSeconds.load());
    const auto slowTau = juce::jmax(0.001f, tuningSlowTauSeconds.load());
    const float fastCoeff = 1.0f - std::exp(-blockSeconds / fastTau);
    const float slowCoeff = 1.0f - std::exp(-blockSeconds / slowTau);
    fastTransientEnv += fastCoeff * (blockTransientRms - fastTransientEnv);
    slowTransientEnv += slowCoeff * (blockTransientRms - slowTransientEnv);

    transientCooldownSeconds = juce::jmax(0.0, transientCooldownSeconds - blockSeconds);

    const float onsetContrast = (float)((fastTransientEnv - slowTransientEnv) / (slowTransientEnv + 1.0e-6));
    const float blockDb = juce::Decibels::gainToDecibels(blockMonoRms, -100.0f);
    const float blockPeak = juce::jmax(blockLeftPeak, blockRightPeak);
    const float blockPeakDb = juce::Decibels::gainToDecibels(blockPeak, -100.0f);

    const auto maskTau = juce::jmax(0.01f, tuningBandMaskTauSeconds.load());
    const auto maskAttackTau = juce::jmax(0.004f, maskTau * 0.35f);
    const auto maskAttackCoeff = 1.0 - std::exp(-blockSeconds / maskAttackTau);
    const auto maskReleaseCoeff = 1.0 - std::exp(-blockSeconds / maskTau);
    const auto updateMask = [maskAttackCoeff, maskReleaseCoeff](double current, double input)
    {
        const double coeff = input > current ? maskAttackCoeff : maskReleaseCoeff;
        return current + coeff * (input - current);
    };
    maskLow = updateMask(maskLow, (double)blockLowRms);
    maskMid = updateMask(maskMid, (double)blockMidRms);
    maskHigh = updateMask(maskHigh, (double)blockHighRms);

    const auto maskStrength = tuningBandMaskStrength.load();
    const float relLow = juce::jmax(0.0f, blockLowRms - (float)(maskStrength * maskLow));
    const float relMid = juce::jmax(0.0f, blockMidRms - (float)(maskStrength * maskMid));
    const float relHigh = juce::jmax(0.0f, blockHighRms - (float)(maskStrength * maskHigh));

    const float wLow = tuningBandVoteLowWeight.load() * relLow;
    const float wMid = tuningBandVoteMidWeight.load() * relMid;
    const float wHigh = tuningBandVoteHighWeight.load() * relHigh;
    const float weightSum = wLow + wMid + wHigh;
    const float bandVoteBalance = weightSum > 1.0e-6f
                                      ? juce::jlimit(-1.0f, 1.0f, (wLow * lowBalance + wMid * midBalance + wHigh * highBalance)
                                                                  / weightSum)
                                      : blockBalance;
    const float directionConfidence = juce::jlimit(0.0f,
                                                   1.0f,
                                                   weightSum / (blockLowRms + blockMidRms + blockHighRms + 1.0e-6f));

    const auto loudEnoughDb = tuningLoudEnoughDb.load();
    const auto onsetContrastStart = tuningOnsetContrastStart.load();
    const auto onsetRatioStart = tuningOnsetRatioStart.load();
    const auto onsetContrastSettle = tuningOnsetContrastSettle.load();
    const auto onsetRatioSettle = tuningOnsetRatioSettle.load();
    const auto cooldownBase = tuningCooldownBaseSeconds.load();
    const auto cooldownByLevel = tuningCooldownByLevelSeconds.load();

    const bool loudEnough = blockPeakDb > loudEnoughDb;
    const bool ratioStartOk = fastTransientEnv > (slowTransientEnv * onsetRatioStart);
    const bool ratioSettleOk = fastTransientEnv <= (slowTransientEnv * onsetRatioSettle);
    const bool contrastSettleOk = onsetContrast < onsetContrastSettle;
    const float ratioValue = (float)(fastTransientEnv / (slowTransientEnv + 1.0e-6));
    const float ratioExcess = juce::jmax(0.0f, ratioValue - onsetRatioStart);

    const float relEnergy = relLow + relMid + relHigh;
    const float logMonoEnergy = std::log(blockTransientRms + 1.0e-6f);
    const float logRelHigh = std::log(relHigh + 1.0e-6f);
    const float fluxEnergy = juce::jmax(0.0f, logMonoEnergy - (float)previousLogMonoEnergy);
    const float fluxHigh = juce::jmax(0.0f, logRelHigh - (float)previousLogHighResidual);
    previousLogMonoEnergy = logMonoEnergy;
    previousLogHighResidual = logRelHigh;

    const float scoreContrastWeight = tuningScoreContrastWeight.load();
    const float scoreEnergyFluxWeight = tuningScoreEnergyFluxWeight.load();
    const float scoreHighFluxWeight = tuningScoreHighFluxWeight.load();
    const float rawScore = scoreContrastWeight * onsetContrast
                         + scoreEnergyFluxWeight * fluxEnergy
                         + scoreHighFluxWeight * fluxHigh
                         + 0.45f * ratioExcess;
    const double scoreDelta = (double)rawScore - scoreMean;
    const double scoreAdaptTau = juce::jmax(0.05f, tuningScoreAdaptTauSeconds.load());
    const double scoreAdapt = 1.0 - std::exp(-blockSeconds / scoreAdaptTau);
    scoreMean += scoreAdapt * scoreDelta;
    scoreVar = juce::jmax(1.0e-6, scoreVar + scoreAdapt * (scoreDelta * scoreDelta - scoreVar));
    const float scoreStd = std::sqrt((float)scoreVar);

    const float sustainedGateDb = loudEnoughDb + tuningSustainedGateDbOffset.load();
    const bool sustainedHot = blockPeakDb > sustainedGateDb;
    if (sustainedHot)
        sustainedHighSeconds += blockSeconds;
    else
        sustainedHighSeconds = juce::jmax(0.0, sustainedHighSeconds - blockSeconds * 1.5);

    const float sustainedStart = tuningSustainedRelaxStartSeconds.load();
    const float sustainedFull = juce::jmax(sustainedStart + 0.01f, tuningSustainedRelaxFullSeconds.load());
    const float sustainedAmount = juce::jlimit(0.0f,
                                               1.0f,
                                               ((float)sustainedHighSeconds - sustainedStart)
                                                   / juce::jmax(0.01f, sustainedFull - sustainedStart));

    timeSinceLastTriggerSeconds += blockSeconds;
    const float burstDecayPerSecond = tuningBurstDecayPerSecond.load();
    burstConfidence = juce::jmax(0.0, burstConfidence - blockSeconds * burstDecayPerSecond);
    const float predictedIoi = (float)juce::jlimit(0.045, 0.220, burstIoiMeanSeconds);
    const float phaseError = std::abs((float)timeSinceLastTriggerSeconds - predictedIoi);
    const float burstWindow = juce::jmax(0.015f, predictedIoi * 0.40f);
    const float phaseProximity = std::exp(-0.5f * (phaseError / burstWindow) * (phaseError / burstWindow));
    const float burstAssist = (float)burstConfidence * phaseProximity;

    const float highK = tuningScoreHighThresholdK.load();
    const float lowK = tuningScoreLowThresholdK.load();
    const float burstThresholdDropScale = tuningBurstThresholdDropScale.load();
    const float burstThresholdDrop = burstAssist * burstThresholdDropScale * scoreStd;
    const float sustainedDropK = tuningSustainedThresholdDropK.load() * sustainedAmount;
    const float scoreHigh = (float)scoreMean + juce::jmax(0.25f, highK - sustainedDropK) * scoreStd - burstThresholdDrop;
    const float scoreLow = (float)scoreMean + juce::jmax(-0.75f, lowK - sustainedDropK * 0.6f) * scoreStd
                         - burstThresholdDrop * 0.30f;

    const bool scoreHighOk = rawScore > scoreHigh && onsetContrast > onsetContrastStart * 0.35f;
    const bool scoreSettled = rawScore < scoreLow;
    const bool onsetSettled = scoreSettled || contrastSettleOk || ratioSettleOk;

    if (!transientArmed)
    {
        unarmedSeconds += blockSeconds;
        const float maxUnarmedSeconds = tuningForcedRearmSeconds.load();
        if (onsetSettled || unarmedSeconds >= maxUnarmedSeconds)
        {
            transientArmed = true;
            unarmedSeconds = 0.0;
        }
    }

    const bool cooldownReady = transientCooldownSeconds <= 0.0f;
    const bool strongScoreOverride = rawScore > (scoreHigh + 0.80f * scoreStd) && timeSinceLastTriggerSeconds > 0.024;

    const float relEnergyGate = juce::jmax(2.0e-5f, 1.0e-4f * (1.0f - 0.65f * sustainedAmount));

    if (loudEnough
        && scoreHighOk
        && transientArmed
        && relEnergy > relEnergyGate
        && (cooldownReady || strongScoreOverride)
        && (ratioStartOk || rawScore > (scoreHigh + 0.30f * scoreStd)))
    {
        const auto lockMix = tuningOnsetLockBandVoteMix.load();
        const float onsetCandidateBalance = juce::jlimit(-1.0f,
                                                         1.0f,
                                                         blockBalance + lockMix * (bandVoteBalance - blockBalance));

        onsetLockBalance = onsetCandidateBalance;
        onsetLockConfidence = directionConfidence;
        onsetLockRemainingSeconds = tuningOnsetLockBaseSeconds.load() + tuningOnsetLockByLevelSeconds.load();

        TransientEvent event;
        event.balance = juce::jlimit(-1.0f, 1.0f, onsetLockBalance);
        event.level = juce::jlimit(0.0f, 1.0f, (blockPeakDb + 54.0f) / 48.0f);
        event.lowMidRatio = juce::jlimit(0.0f, 1.0f, lowMidRatio);
        event.directionConfidence = onsetLockConfidence;
        event.streamId = updateStreamState(event.balance, event.directionConfidence, blockSeconds);
        event.transientClass = classifyTransient(blockDb, highFreqRatio, onsetContrast);
        pushTransientEvent(event);

        const float ioi = (float)timeSinceLastTriggerSeconds;
        if (ioi > 0.040f && ioi < 0.250f)
        {
            burstIoiMeanSeconds += tuningBurstIoiBlend.load() * ((double)ioi - burstIoiMeanSeconds);
            burstConfidence = juce::jmin(1.0, burstConfidence + 0.24);
        }
        else
        {
            burstConfidence *= 0.82;
        }

        transientArmed = false;
        unarmedSeconds = 0.0;
        timeSinceLastTriggerSeconds = 0.0;
        onsetLockRemainingSeconds += tuningOnsetLockByLevelSeconds.load() * event.level;
        const float burstMinCooldownScale = tuningBurstMinCooldownScale.load();
        const float burstCooldownReduction = tuningBurstCooldownReduction.load();
        const float burstCooldownScale = juce::jlimit(burstMinCooldownScale,
                                                      1.0f,
                                                      1.0f - burstCooldownReduction * (float)burstConfidence);
        const float sustainedCooldownScale = juce::jlimit(0.25f,
                                                          1.0f,
                                                          1.0f - tuningSustainedCooldownReduction.load() * sustainedAmount);
        transientCooldownSeconds = (cooldownBase + cooldownByLevel * event.level) * burstCooldownScale * sustainedCooldownScale;
    }
}

/**
 * @brief 分类瞬态类型
 * @param blockDb 块峰值 dB
 * @param highFreqRatio 高频占比
 * @param onsetContrast 起音对比度
 * @return 瞬态分类（gunshot / reload / footstep）
 * 
 * 分类规则：
 * - 枪声：很响且明亮，或响且很明亮且起音强
 * - 换弹：够响且高频占比够高
 * - 其他：脚步声
 */
AudioCaptureService::TransientClass AudioCaptureService::classifyTransient(float blockDb,
                                                                           float highFreqRatio,
                                                                           float onsetContrast) const noexcept
{
    const bool veryLoud = blockDb > tuningGunshotVeryLoudDb.load();
    const bool loud = blockDb > tuningGunshotLoudDb.load();
    const bool bright = highFreqRatio > tuningGunshotBrightMin.load();
    const bool veryBright = highFreqRatio > tuningGunshotVeryBrightMin.load();

    if ((veryLoud && bright) || (loud && veryBright && onsetContrast > tuningGunshotOnsetMin.load()))
        return TransientClass::gunshot;

    if (blockDb > tuningReloadDbMin.load() && highFreqRatio > tuningReloadHighRatioMin.load())
        return TransientClass::reload;

    return TransientClass::footstep;
}

/**
 * @brief 更新声流状态（用于跟踪同一个声源）
 * @param proposedBalance 提议的声像位置
 * @param confidence 方向置信度
 * @param dtSeconds 时间增量（未使用）
 * @return 匹配的声流 ID
 * 
 * 功能：
 * 1. 查找距离最近的活跃声流
 * 2. 如果在匹配距离内，更新该声流
 * 3. 否则，激活一个新的声流（或复用最旧的）
 * 
 * 声流用于：在多次触发之间保持方向连续性。
 */
uint32_t AudioCaptureService::updateStreamState(float proposedBalance, float confidence, float dtSeconds) noexcept
{
    juce::ignoreUnused(dtSeconds);
    const auto holdSeconds = tuningStreamHoldSeconds.load();
    const auto matchDistance = tuningStreamMatchDistance.load();
    const auto blend = tuningStreamUpdateBlend.load();

    int bestIndex = -1;
    float bestDistance = 1.0e9f;

    for (int i = 0; i < (int)streamStates.size(); ++i)
    {
        auto& s = streamStates[(size_t)i];
        if (!s.active)
            continue;

        const auto d = std::abs(s.balance - proposedBalance);
        if (d < bestDistance)
        {
            bestDistance = d;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0 && bestDistance <= matchDistance)
    {
        auto& s = streamStates[(size_t)bestIndex];
        s.balance += blend * (proposedBalance - s.balance);
        s.confidence += 0.45f * (confidence - s.confidence);
        s.holdSeconds = holdSeconds;
        return s.id;
    }

    for (auto& s : streamStates)
    {
        if (!s.active)
        {
            s.active = true;
            s.id = nextStreamId++;
            if (nextStreamId == 0)
                nextStreamId = 1;
            s.balance = proposedBalance;
            s.confidence = confidence;
            s.holdSeconds = holdSeconds;
            return s.id;
        }
    }

    auto& reuse = streamStates.front();
    reuse.active = true;
    reuse.id = nextStreamId++;
    if (nextStreamId == 0)
        nextStreamId = 1;
    reuse.balance = proposedBalance;
    reuse.confidence = confidence;
    reuse.holdSeconds = holdSeconds;
    return reuse.id;
}

/**
 * @brief 初始化分析状态（采样率变化时调用）
 * @param sampleRate 新的采样率（Hz）
 * 
 * 计算 alpha 系数（用于 RMS 积分的一阶低通滤波）。
 */
void AudioCaptureService::prepareAnalysis(double sampleRate)
{
    const double tauSeconds = 0.140;
    alpha = 1.0 - std::exp(-1.0 / (tauSeconds * juce::jmax(1.0, sampleRate)));
    resetAnalysis();
}

/**
 * @brief 重置所有分析状态
 * 
 * 重置内容：
 * - M/S 状态变量（sLL, sRR, sLR, sMM, sSS）
 * - 瞬态包络（fastTransientEnv, slowTransientEnv）
 * - 冷却和武装状态
 * - 评分系统状态（scoreMean, scoreVar）
 * - 连发检测状态（burstIoiMeanSeconds, burstConfidence）
 * - 频段滤波状态
 * - 声流状态
 * - 事件队列
 * - 显示值
 */
void AudioCaptureService::resetAnalysis()
{
    sLL = 0.0;
    sRR = 0.0;
    sLR = 0.0;
    sMM = 0.0;
    sSS = 0.0;

    fastTransientEnv = 0.0;
    slowTransientEnv = 0.0;
    transientCooldownSeconds = 0.0;
    transientArmed = true;
    unarmedSeconds = 0.0;
    scoreMean = 0.0;
    scoreVar = 1.0;
    previousLogMonoEnergy = 0.0;
    previousLogHighResidual = 0.0;
    timeSinceLastTriggerSeconds = 10.0;
    burstIoiMeanSeconds = 0.095;
    burstConfidence = 0.0;
    sustainedHighSeconds = 0.0;
    transientHpState = 0.0;
    transientHpPreviousInput = 0.0;
    onsetLockRemainingSeconds = 0.0;
    onsetLockBalance = 0.0f;
    onsetLockConfidence = 0.0f;
    lowBandState = 0.0;
    lowBandStateL = 0.0;
    lowBandStateR = 0.0;
    midBandStateL = 0.0;
    midBandStateR = 0.0;
    maskLow = 0.0;
    maskMid = 0.0;
    maskHigh = 0.0;

    for (auto& s : streamStates)
        s = {};
    nextStreamId = 1;

    transientReadIndex.store(0, std::memory_order_relaxed);
    transientWriteIndex.store(0, std::memory_order_relaxed);

    balance.store(0.0f);
    width.store(0.0f);
    leftLevel.store(0.0f);
    rightLevel.store(0.0f);
}

/**
 * @brief 启动 WASAPI 环回捕获（Windows 专用）
 * @return 是否成功启动
 * 
 * 功能：
 * 1. 创建 WasapiLoopbackCapture 对象
 * 2. 设置音频回调（接收捕获的音频数据）
 * 3. 启动捕获
 * 4. 设置采样率和 captureMode
 * 
 * 如果失败，记录错误信息到 lastError。
 */
bool AudioCaptureService::startLoopbackCapture()
{
   #if JUCE_WINDOWS
    loopbackCapture = std::make_unique<WasapiLoopbackCapture>();
    loopbackCapture->onAudio = [this](const float* left, const float* right, int numSamples, double sampleRate)
    {
        processStereoBlock(left, right, numSamples, sampleRate);
    };

    if (loopbackCapture->start())
    {
        activeSampleRate.store(48000.0);
        prepareAnalysis(48000.0);
        captureMode = "WASAPI Loopback";
        currentDeviceName = "Default Output Device";
        lastError.clear();
        return true;
    }

    lastError = loopbackCapture->getLastError();
   #endif

    return false;
}

/**
 * @brief 启动默认输入设备捕获（回退方案）
 * @return 是否成功启动
 * 
 * 功能：
 * 1. 初始化 JUCE AudioDeviceManager
 * 2. 使用默认输入设备（麦克风）
 * 3. 注册音频回调（this）
 * 
 * 如果失败，记录错误信息到 lastError。
 */
bool AudioCaptureService::startDefaultInputCapture()
{
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = false;

    const auto error = deviceManager.initialise(2, 0, nullptr, true, {}, &setup);
    if (error.isNotEmpty())
    {
        if (lastError.isNotEmpty())
            lastError << " | ";
        lastError << "Input init failed: " << error;
        return false;
    }

    if (auto* device = deviceManager.getCurrentAudioDevice())
        currentDeviceName = device->getName();

    captureMode = "Default Input";
    deviceManager.addAudioCallback(this);
    return true;
}

/**
 * @brief 将瞬态事件推入队列（线程安全）
 * @param event 要推入的瞬态事件
 * 
 * 使用无锁环形缓冲区实现。
 * 如果队列满，丢弃本次事件。
 * 
 * 调用者：音频线程
 * 消费者：UI 线程（通过 popNextTransientEvent）
 */
void AudioCaptureService::pushTransientEvent(const TransientEvent& event) noexcept
{
    const auto write = transientWriteIndex.load(std::memory_order_relaxed);
    const auto next = (write + 1) % transientQueueSize;
    const auto read = transientReadIndex.load(std::memory_order_acquire);

    if (next == read)
        return; // queue full, drop this event

    transientQueue[write] = event;
    transientWriteIndex.store(next, std::memory_order_release);
}