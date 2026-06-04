#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

#include "AudioCaptureService.h"

/**
 * @brief 主组件类 - SeePosition 应用的核心 UI 组件
 * 
 * 负责：
 * - 音频可视化渲染（声像位置、电平、瞬态事件）
 * - 用户交互处理（鼠标悬停、拖拽、设置面板）
 * - 参数调优与持久化
 * - 定时器回调更新（30Hz）
 */
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    /** 构造函数 - 初始化 UI 组件和音频服务 */
    MainComponent();
    /** 析构函数 - 清理资源 */
    ~MainComponent() override;

    //==============================================================================
    // UI 绘制与布局
    
    /** @brief 绘制组件内容（背景、轨道、圆点、音量条等） */
    void paint(juce::Graphics& g) override;
    /** @brief 组件大小改变时重新计算布局 */
    void resized() override;

    //==============================================================================
    // 鼠标事件处理
    
    /** @brief 鼠标移动事件 - 用于检测悬停状态 */
    void mouseMove(const juce::MouseEvent& event) override;
    /** @brief 鼠标进入组件事件 */
    void mouseEnter(const juce::MouseEvent& event) override;
    /** @brief 鼠标离开组件事件 */
    void mouseExit(const juce::MouseEvent& event) override;
    /** @brief 鼠标按下事件 - 检测点击设置按钮/关闭按钮/版本链接 */
    void mouseDown(const juce::MouseEvent& event) override;
    /** @brief 鼠标拖拽事件 - 支持窗口拖拽 */
    void mouseDrag(const juce::MouseEvent& event) override;
    /** @brief 鼠标释放事件 */
    void mouseUp(const juce::MouseEvent& event) override;

private:
    //==============================================================================
    // 内部设置面板类（前向声明）
    class MainComponentSettingsPanel;
    friend class MainComponentSettingsPanel;

    //==============================================================================
    // 视觉调优参数结构体
    
    /**
     * @brief 视觉调优参数集合
     * 
     * 这些参数控制 UI 的显示效果，可通过设置面板实时调整。
     */
    struct VisualTuning
    {
        //--- 声像位置相关 ---
        float balanceDeadzone = 0.03f;          ///< 声像死区：小于该值时主圆点归中（避免微小抖动）
        float snapNearZeroEpsilon = 0.001f;     ///< 归零阈值：小于此值时视为零
        float centerBiasLearningRate = 0.02f;   ///< 中心偏置学习率：静音时自动校正中心偏置的速度
        float centerBiasMonoGate = 0.25f;       ///< 单声道门限：低于此响度时不更新中心偏置

        //--- 信号强度与门控 ---
        float levelGateLow = 0.06f;             ///< 低门限：信号低于此值时降低显示权重
        float levelGateHigh = 0.32f;            ///< 高门限：信号高于此值时显示权重最大
        float positionWeightMin = 0.12f;        ///< 位置权重最小值：确保低信号时仍有基本显示
        float dotAlphaMin = 0.02f;              ///< 主点最小透明度
        float dotAlphaPower = 1.8f;             ///< 透明度曲线指数：控制透明度变化曲线

        //--- 颜色映射 ---
        float mainColourMapStart = 0.03f;       ///< 颜色映射起始值（绿色端）
        float mainColourMapSpan = 0.27f;        ///< 颜色映射范围
        float mainColourMapExponent = 0.55f;    ///< 颜色映射指数：控制颜色变化曲线

        //--- 历史点显示 ---
        float historyRadiusMin = 3.5f;          ///< 历史点最小半径
        float historyRadiusMax = 7.0f;          ///< 历史点最大半径
        float historyFadeExponent = 1.35f;      ///< 历史点淡出指数
        float historyLifeMinSeconds = 0.85f;    ///< 历史点最短存活时间（秒）
        float historyLifeMaxSeconds = 1.5f;     ///< 历史点最长存活时间（秒）
    };

    //==============================================================================
    // 历史瞬态点结构体
    
    /**
     * @brief 历史瞬态点数据结构
     * 
     * 用于存储和绘制历史瞬态事件（枪声、脚步声、换弹）
     */
    struct TransientDot
    {
        float balance = 0.0f;                                   ///< 声像位置 (-1.0 左 ~ +1.0 右)
        float level = 0.0f;                                     ///< 响度等级 (0.0 ~ 1.0)
        float lowMidRatio = 0.0f;                               ///< 低频/中频能量比 (0.0 ~ 1.0，越高低频越多)
        float directionConfidence = 0.0f;                       ///< 方向置信度 (0.0 ~ 1.0)
        uint32_t streamId = 0;                                  ///< 音频流 ID（用于连发追踪）
        float ageSeconds = 0.0f;                                ///< 已存活时间（秒）
        float lifeSeconds = 1.0f;                               ///< 总存活时间（秒）
        AudioCaptureService::TransientClass transientClass =     ///< 瞬态类型（枪声/脚步/换弹）
            AudioCaptureService::TransientClass::footstep;
    };

    //==============================================================================
    // 定时器回调
    
    /** @brief 定时器回调（30Hz）- 更新音频数据并触发重绘 */
    void timerCallback() override;

    //==============================================================================
    // 设置面板相关
    
    /** @brief 打开设置面板（弹出式） */
    void openSettingsPopup();
    /** @brief 判断鼠标是否悬停在设置按钮上 */
    bool isOverSettingsButton() const;
    /** @brief 判断鼠标是否悬停在版本链接上 */
    bool isOverVersionLink() const;

    //==============================================================================
    // 参数获取与设置
    
    /** @brief 获取当前视觉调优参数 */
    VisualTuning getVisualTuning() const noexcept;
    /** @brief 设置视觉调优参数 */
    void setVisualTuning(const VisualTuning& tuning) noexcept;
    /** @brief 重置所有参数为默认值 */
    void resetParametersToDefaults();
    /** @brief 标记设置为"已修改"（触发自动保存） */
    void markSettingsDirty() noexcept;
    
    /** @brief 判断侧边音量条是否隐藏 */
    bool areSideMetersHidden() const noexcept;
    /** @brief 设置侧边音量条的显示/隐藏 */
    void setSideMetersHidden(bool shouldHide);

    //==============================================================================
    // 预设导入导出
    
    /** @brief 导出当前参数预设到文件 */
    void exportPresetToFile();
    /** @brief 从文件导入参数预设 */
    void importPresetFromFile(std::function<void(bool)> onFinished = {});

    //==============================================================================
    // 设置持久化
    
    /** @brief 获取设置文件路径 */
    juce::File getSettingsFile() const;
    /** @brief 将当前设置序列化为 ValueTree */
    juce::ValueTree createSettingsTree() const;
    /** @brief 从 ValueTree 恢复设置 */
    void applySettingsTree(const juce::ValueTree& tree);
    /** @brief 从磁盘加载设置 */
    void loadSettings();
    /** @brief 保存设置到磁盘 */
    void saveSettings();

    //==============================================================================
    // 显示辅助函数
    
    /** @brief 获取轨道（声像位置显示区域）的边界 */
    juce::Rectangle<float> getTrackBounds() const;
    /** @brief 根据 Width 值获取指示颜色（绿→黄→红） */
    juce::Colour getIndicatorColour(float widthValue) const;
    /** @brief 获取音量条颜色（根据音量大小） */
    juce::Colour getMeterColour(float amount) const;
    /** @brief 将平衡值映射为显示值（应用死区、归零等处理） */
    float mapBalanceForDisplay(float balanceValue) const;
    /** @brief 将平衡值映射为 X 坐标（在轨道区域内） */
    float mapBalanceToX(float balanceValue, const juce::Rectangle<float>& track) const;

    //==============================================================================
    // 成员变量
    
    AudioCaptureService audioCapture;   ///< 音频捕获与处理服务
    
    //--- 显示数据（由定时器回调更新）---
    float displayBalance = 0.0f;       ///< 当前显示的声像位置（-1.0 ~ +1.0）
    float displayWidth = 0.0f;         ///< 当前显示的立体声宽度（0.0 ~ 1.0）
    float displayLeftLevel = 0.0f;     ///< 当前显示的左声道电平
    float displayRightLevel = 0.0f;    ///< 当前显示的右声道电平
    float displaySignal = 0.0f;        ///< 当前信号强度
    float displayConfidence = 0.0f;    ///< 当前方向置信度
    float displayDotAlpha = 0.0f;      ///< 当前主点透明度
    float displayPositionWeight = 0.0f; ///< 当前位置权重
    float centerBias = 0.0f;          ///< 中心偏置校准值
    
    //--- 交互状态 ---
    bool isHovering = false;           ///< 鼠标是否悬停在组件上
    bool altDragActive = false;        ///< 是否正在拖拽窗口（Alt+拖拽）
    
    //--- 交互区域边界 ---
    juce::Rectangle<float> closeButtonBounds;    ///< 关闭按钮的点击区域
    juce::Rectangle<float> settingsButtonBounds; ///< 设置按钮的点击区域
    juce::Rectangle<float> versionLinkBounds;    ///< 版本链接的点击区域
    
    //--- 历史瞬态点 ---
    std::vector<TransientDot> transientDots;     ///< 历史瞬态点列表
    
    //--- 时间追踪 ---
    double lastFrameTimeMs = 0.0;               ///< 上一帧的时间（毫秒）
    double lastSettingsSaveTimeMs = 0.0;        ///< 上次自动保存设置的时间
    
    //--- 设置状态 ---
    bool settingsDirty = false;                  ///< 设置是否已修改（需要保存）
    bool hideSideMeters = false;                ///< 是否隐藏侧边音量条
    
    //--- 视觉调优参数实例 ---
    VisualTuning visualTuning;                  ///< 当前视觉调优参数
    
    //--- UI 组件 ---
    juce::Component::SafePointer<juce::DialogWindow> settingsDialog; ///< 设置面板对话框
    std::unique_ptr<juce::FileChooser> presetFileChooser;            ///< 预设文件选择器
    
    //--- 窗口拖拽 ---
    juce::ComponentDragger windowDragger;                        ///< 窗口拖拽器
    juce::ComponentBoundsConstrainer windowBoundsConstrainer;    ///< 窗口大小约束器

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
