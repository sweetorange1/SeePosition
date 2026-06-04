/**
 * @file MainComponent.cpp
 * @brief SeePosition 主组件实现文件
 * 
 * 本文件包含：
 * - ParameterRow：设置面板中的参数行组件
 * - MainComponentSettingsPanel：设置面板主组件
 * - MainComponent：主组件（UI 渲染、交互、定时器回调）
 * 
 * @author iisaacbeats
 * @date 2024
 */

#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <functional>

//==============================================================================
// 匿名命名空间：存放设置面板相关辅助类
//==============================================================================

namespace
{
    // 设置和面板的 XML 节点 ID
    constexpr const char* settingsRootId = "SeePositionSettings";  ///< 设置文件根节点 ID
    constexpr const char* visualNodeId = "Visual";               ///< 视觉调优参数节点 ID
    constexpr const char* audioNodeId = "Audio";                 ///< 音频调优参数节点 ID

/**
 * @brief 设置面板中的参数行组件
 * 
 * 每个 ParameterRow 包含：
 * - 参数名称标签（左侧）
 * - 参数描述标签（第二行）
 * - 滑块控件（右侧）
 * - 数值显示/编辑框（最右侧）
 * 
 * 支持滑块拖动和直接输入数值两种方式修改参数，
 * 数值会自动限制在 [minValue, maxValue] 范围内。
 */
class ParameterRow final : public juce::Component
{
public:
    /**
     * @brief 构造函数
     * @param title 参数名称
     * @param description 参数描述（显示在第二行）
     * @param minValue 最小值
     * @param maxValue 最大值
     * @param interval 滑块步进间隔
     * @param initialValue 初始值
     * @param onValueChanged 数值变化回调函数
     */
    ParameterRow(const juce::String& title,
                 const juce::String& description,
                 float minValue,
                 float maxValue,
                 float interval,
                 float initialValue,
                 std::function<void(float)> onValueChanged)
        : callback(std::move(onValueChanged))
    {
        nameLabel.setText(title, juce::dontSendNotification);
        nameLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(nameLabel);

        descLabel.setText(description, juce::dontSendNotification);
        descLabel.setJustificationType(juce::Justification::centredLeft);
        descLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGBA(196, 201, 210, 165));
        descLabel.setFont(juce::FontOptions(11.0f));
        addAndMakeVisible(descLabel);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setRange(minValue, maxValue, interval);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(slider);

        valueLabel.setEditable(false, true, false);
        valueLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(valueLabel);

        slider.onValueChange = [this]
        {
            if (isUpdating)
                return;

            const auto value = (float)slider.getValue();
            isUpdating = true;
            valueLabel.setText(juce::String(value, 3), juce::dontSendNotification);
            isUpdating = false;
            if (callback)
                callback(value);
        };

        valueLabel.onTextChange = [this]
        {
            if (isUpdating)
                return;

            const auto entered = valueLabel.getText().getFloatValue();
            const auto clamped = juce::jlimit((float)slider.getMinimum(),
                                              (float)slider.getMaximum(),
                                              entered);
            isUpdating = true;
            slider.setValue(clamped, juce::sendNotificationSync);
            valueLabel.setText(juce::String(clamped, 3), juce::dontSendNotification);
            isUpdating = false;
        };

        slider.setValue(initialValue, juce::dontSendNotification);
        valueLabel.setText(juce::String(initialValue, 3), juce::dontSendNotification);
    }

    /**
     * @brief 布局子组件
     * 
     * 布局结构：
     * - 第一行：参数名称（左侧）、数值显示（右侧）
     * - 第二行：参数描述（左侧）、滑块控件（填充剩余空间）
     */
    void resized() override
    {
        auto area = getLocalBounds();

        auto titleRow = area.removeFromTop(18);
        auto controlRow = area;

        nameLabel.setBounds(titleRow.removeFromLeft(260));
        valueLabel.setBounds(titleRow.removeFromRight(78));
        descLabel.setBounds(controlRow.removeFromLeft(260).reduced(0, 1));
        slider.setBounds(controlRow.reduced(6, 2));
    }

private:
    juce::Label nameLabel;
    juce::Label descLabel;
    juce::Slider slider;
    juce::Label valueLabel;
    std::function<void(float)> callback;
    bool isUpdating = false;
};

}  // namespace

/**
 * @brief 设置面板主组件类
 * 
 * 此类是 MainComponent 的内部类，负责显示所有可调参数。
 * 设置面板包含以下部分：
 * - 主圆点：控制主指示圆点的显示行为
 * - 历史点：控制历史瞬态点的显示行为
 * - 瞬态检测：控制音频瞬态检测算法参数
 * - 瞬态分类：控制枪声/脚步声/换弹的分类参数
 * 
 * 功能：
 * - 重置为默认参数
 * - 隐藏/显示左右音量条
 * - 保存/读取预设文件
 * - 实时调整参数并预览效果
 */
class MainComponent::MainComponentSettingsPanel final : public juce::Component
{
public:
    /**
     * @brief 构造函数
     * @param ownerRef 所属的 MainComponent 引用（用于回调参数修改）
     */
    explicit MainComponentSettingsPanel(MainComponent& ownerRef) : owner(ownerRef)
    {
        addAndMakeVisible(resetButton);
        resetButton.setButtonText(u8"重置为默认参数");
        resetButton.onClick = [this]
        {
            owner.resetParametersToDefaults();
            // 同步隐藏音量条按钮状态
            hideMetersToggle.setToggleState(owner.areSideMetersHidden(), juce::dontSendNotification);
            rebuildRows();
        };

        addAndMakeVisible(hideMetersToggle);
        hideMetersToggle.setButtonText(u8"隐藏左右音量条");
        hideMetersToggle.setToggleState(owner.areSideMetersHidden(), juce::dontSendNotification);
        hideMetersToggle.onClick = [this]
        {
            owner.setSideMetersHidden(hideMetersToggle.getToggleState());
        };

        addAndMakeVisible(savePresetButton);
        savePresetButton.setButtonText(u8"保存预设");
        savePresetButton.onClick = [this]
        {
            owner.exportPresetToFile();
        };

        addAndMakeVisible(loadPresetButton);
        loadPresetButton.setButtonText(u8"读取预设");
        loadPresetButton.onClick = [this]
        {
            juce::Component::SafePointer<MainComponentSettingsPanel> safeThis(this);
            owner.importPresetFromFile([safeThis](bool applied)
            {
                if (safeThis == nullptr || !applied)
                    return;

                safeThis->hideMetersToggle.setToggleState(safeThis->owner.areSideMetersHidden(), juce::dontSendNotification);
                safeThis->rebuildRows();
            });
        };

        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);

        buildRows();
        setSize(620, 540);
    }

    /**
     * @brief 布局所有子组件
     * 
     * 布局结构：
     * - 顶部：隐藏音量条按钮、读取预设按钮、保存预设按钮、重置按钮
     * - 剩余区域：可滚动的参数列表（通过 Viewport 实现）
     */
    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(30);
        hideMetersToggle.setBounds(top.removeFromLeft(160));
        loadPresetButton.setBounds(top.removeFromRight(110));
        savePresetButton.setBounds(top.removeFromRight(110));
        resetButton.setBounds(top.removeFromRight(170));
        area.removeFromTop(6);
        viewport.setBounds(area);
        content.setBounds(0, 0, juce::jmax(560, viewport.getWidth() - 18), contentHeight + 10);
        layoutRows();
    }

private:
    /**
     * @brief 重建所有参数行
     * 
     * 清空现有的参数行和分区标题，重新调用 buildRows() 构建。
     * 在重置参数或读取预设后调用。
     */
    void rebuildRows()
    {
        rows.clear(true);
        sections.clear(true);
        sectionOrder.clear();
        rowOrder.clear();
        contentHeight = 8;
        rowCursor = 0;
        buildRows();
        resized();
        repaint();
    }

    /**
     * @brief 添加一个分区标题
     * @param title 分区标题文本
     * 
     * 在参数列表中添加一个分组标题（如"主圆点"、"历史点"等）。
     */
    void addSection(const juce::String& title)
    {
        auto* section = sections.add(new juce::Label());
        section->setText(title, juce::dontSendNotification);
        section->setJustificationType(juce::Justification::centredLeft);
        section->setFont(juce::FontOptions(14.0f, juce::Font::bold));
        content.addAndMakeVisible(section);
        sectionOrder.add(section);
        contentHeight += 6;
    }

    /**
     * @brief 添加一个参数行
     * @param title 参数名称
     * @param description 参数描述
     * @param minValue 最小值
     * @param maxValue 最大值
     * @param interval 滑块步进间隔
     * @param initialValue 初始值
     * @param setter 参数设置回调函数
     * 
     * 创建一个 ParameterRow 并添加到参数列表中。
     * setter 会被包装为 trackedSetter，每次调用时标记设置为"脏"。
     */
    void addParam(const juce::String& title,
                  const juce::String& description,
                  float minValue,
                  float maxValue,
                  float interval,
                  float initialValue,
                  std::function<void(float)> setter)
    {
        auto trackedSetter = [this, cb = std::move(setter)](float v)
        {
            cb(v);
            owner.markSettingsDirty();
        };

        auto* row = rows.add(new ParameterRow(title,
                                              description,
                                              minValue,
                                              maxValue,
                                              interval,
                                              initialValue,
                                              std::move(trackedSetter)));
        content.addAndMakeVisible(row);
        rowOrder.add(row);
        contentHeight += 52;
    }

    /**
     * @brief 构建所有参数行
     * 
     * 此函数定义所有可调参数，分为以下分区：
     * 1. 主圆点：balanceDeadzone, snapNearZeroEpsilon, centerBiasLearningRate 等
     * 2. 历史点：historyRadiusMin, historyRadiusMax, historyFadeExponent 等
     * 3. 瞬态检测：loudEnoughDb, onsetContrastStart, cooldownBaseSeconds 等
     * 4. 瞬态分类：gunshotVeryLoudDb, reloadDbMin 等
     */
    void buildRows()
    {
        const auto visual = owner.getVisualTuning();
        const auto audio = owner.audioCapture.getTuning();

        addSection(u8"主圆点");
        addParam(u8"声像死区", u8"小于该值时主圆点归中，减小抖动", 0.0f, 0.20f, 0.001f, visual.balanceDeadzone, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.balanceDeadzone = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"归零阈值", u8"绝对值小于该阈值时显示为 0", 0.0001f, 0.0200f, 0.0001f, visual.snapNearZeroEpsilon, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.snapNearZeroEpsilon = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"中心偏置学习率", u8"静音时自动校正中心的速度", 0.0f, 0.30f, 0.001f, visual.centerBiasLearningRate, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.centerBiasLearningRate = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"中心偏置电平门限", u8"低于该电平才进行中心偏置校正", 0.0f, 1.0f, 0.001f, visual.centerBiasMonoGate, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.centerBiasMonoGate = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"强度门限下限", u8"用于计算可见度和置信度门控的低端", 0.0f, 0.8f, 0.001f, visual.levelGateLow, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.levelGateLow = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"强度门限上限", u8"用于计算可见度和置信度门控的高端", 0.01f, 1.0f, 0.001f, visual.levelGateHigh, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.levelGateHigh = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"位置最小权重", u8"弱信号时仍保留的最小位移比例", 0.0f, 1.0f, 0.001f, visual.positionWeightMin, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.positionWeightMin = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"主点最小透明度", u8"主圆点的最低可见度", 0.0f, 0.8f, 0.001f, visual.dotAlphaMin, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.dotAlphaMin = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"主点透明度曲线", u8"值越大，弱信号越不透明度低", 0.2f, 4.0f, 0.01f, visual.dotAlphaPower, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.dotAlphaPower = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"主点颜色起点", u8"置信度从该值开始进入颜色映射", 0.0f, 1.0f, 0.001f, visual.mainColourMapStart, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.mainColourMapStart = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"主点颜色跨度", u8"颜色从绿到红覆盖的置信度区间", 0.01f, 1.0f, 0.001f, visual.mainColourMapSpan, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.mainColourMapSpan = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"主点颜色曲线", u8"颜色变化非线性指数", 0.2f, 2.5f, 0.01f, visual.mainColourMapExponent, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.mainColourMapExponent = v;
            owner.setVisualTuning(t);
        });

        addSection(u8"历史点");
        addParam(u8"半径最小值", u8"弱瞬态历史点半径", 1.0f, 16.0f, 0.1f, visual.historyRadiusMin, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.historyRadiusMin = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"半径最大值", u8"强瞬态历史点半径", 1.0f, 20.0f, 0.1f, visual.historyRadiusMax, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.historyRadiusMax = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"淡出曲线", u8"历史点透明度随时间衰减曲线", 0.3f, 4.0f, 0.01f, visual.historyFadeExponent, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.historyFadeExponent = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"寿命最短(秒)", u8"弱瞬态历史点存在时间", 0.1f, 3.0f, 0.01f, visual.historyLifeMinSeconds, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.historyLifeMinSeconds = v;
            owner.setVisualTuning(t);
        });
        addParam(u8"寿命最长(秒)", u8"强瞬态历史点存在时间", 0.1f, 4.0f, 0.01f, visual.historyLifeMaxSeconds, [this](float v)
        {
            auto t = owner.getVisualTuning();
            t.historyLifeMaxSeconds = v;
            owner.setVisualTuning(t);
        });

        addSection(u8"瞬态检测");
        addParam(u8"响度门限(dB)", u8"峰值超过该门限才进入触发判定；调低=更灵敏，调高=更抗噪", -90.0f, -10.0f, 0.1f, audio.loudEnoughDb, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.loudEnoughDb = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音对比起始", u8"快包络相对慢包络的增幅下限；调低=更易触发连发，调高=更少误报", 0.05f, 3.0f, 0.01f, audio.onsetContrastStart, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetContrastStart = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音倍率起始", u8"快包络/慢包络最小倍率；与起音对比一起决定音头是否足够" , 1.0f, 2.5f, 0.01f, audio.onsetRatioStart, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetRatioStart = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音对比复位", u8"评分回落到该区间后重新上膛；调高=更快连发复位", 0.0f, 2.0f, 0.01f, audio.onsetContrastSettle, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetContrastSettle = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音倍率复位", u8"快/慢包络倍率回落阈值；通常应不高于起音倍率起始", 1.0f, 2.5f, 0.01f, audio.onsetRatioSettle, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetRatioSettle = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"冷却基础时间", u8"每次触发后必等时长；调短=连发更容易全捕捉，调长=更稳", 0.0f, 0.5f, 0.001f, audio.cooldownBaseSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.cooldownBaseSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"冷却随强度", u8"强音头附加冷却；调低可减少强枪声后漏检下一发", 0.0f, 0.8f, 0.001f, audio.cooldownByLevelSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.cooldownByLevelSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分能量流量权重", u8"整体能量突增对触发评分的贡献；调高对爆发声更敏感", 0.0f, 4.0f, 0.01f, audio.scoreEnergyFluxWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreEnergyFluxWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分高频流量权重", u8"高频突增对触发评分的贡献；调高可更偏向枪声/金属击发音", 0.0f, 4.0f, 0.01f, audio.scoreHighFluxWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreHighFluxWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分对比权重", u8"快慢包络差值在评分中的权重；调高可强化【音头感】", 0.0f, 4.0f, 0.01f, audio.scoreContrastWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreContrastWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分统计时间常数", u8"环境自适应速度；调小=更快跟随场景变化，调大=更稳定", 0.05f, 2.0f, 0.01f, audio.scoreAdaptTauSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreAdaptTauSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分高阈值系数", u8"主触发阈值；调低=抓得更多，调高=误报更少", 0.2f, 5.0f, 0.01f, audio.scoreHighThresholdK, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreHighThresholdK = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"评分低阈值系数", u8"复位阈值(迟滞下沿)；调高=更快重触发，调低=更稳", -1.0f, 4.0f, 0.01f, audio.scoreLowThresholdK, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.scoreLowThresholdK = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"强制复位时间", u8"长时间高能未回落时的兜底上膛时间；调短可减少【只触发前几次】", 0.02f, 0.40f, 0.001f, audio.forcedRearmSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.forcedRearmSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"连发IOI学习率", u8"连发节奏学习速度；调高=更快进入连发预测", 0.01f, 1.0f, 0.01f, audio.burstIoiBlend, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.burstIoiBlend = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"连发置信衰减", u8"连发状态遗忘速度；调低=连发状态保持更久", 0.05f, 4.0f, 0.01f, audio.burstDecayPerSecond, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.burstDecayPerSecond = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"连发降阈强度", u8"预测到下一发时额外降阈力度；调高可提升连发召回", 0.0f, 1.5f, 0.01f, audio.burstThresholdDropScale, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.burstThresholdDropScale = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"连发冷却减免", u8"连发状态下缩短冷却的幅度；调高可减少漏掉中间几发", 0.0f, 0.95f, 0.01f, audio.burstCooldownReduction, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.burstCooldownReduction = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"连发最小冷却倍率", u8"连发可压缩到的最短冷却比例下限", 0.2f, 1.0f, 0.01f, audio.burstMinCooldownScale, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.burstMinCooldownScale = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"瞬态高通截止(Hz)", u8"检测前先做高通；调高=更压低频拖尾，可能损失低频脚步", 40.0f, 1200.0f, 1.0f, audio.transientHighpassHz, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.transientHighpassHz = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"持续高能起始(秒)", u8"高能持续超过该时长开始自动放宽检测", 0.0f, 2.0f, 0.01f, audio.sustainedRelaxStartSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.sustainedRelaxStartSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"持续高能满额(秒)", u8"达到该时长后放宽效果达到上限", 0.05f, 4.0f, 0.01f, audio.sustainedRelaxFullSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.sustainedRelaxFullSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"持续高能降阈系数", u8"持续高能时阈值下调幅度；调高可提升混战中的瞬态召回", 0.0f, 2.5f, 0.01f, audio.sustainedThresholdDropK, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.sustainedThresholdDropK = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"持续高能冷却减免", u8"持续高能时额外缩短冷却；调高可减少密集枪声漏检", 0.0f, 0.95f, 0.01f, audio.sustainedCooldownReduction, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.sustainedCooldownReduction = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"持续高能门限偏置(dB)", u8"峰值高于(响度门限+该值)时记为高能持续状态", 0.0f, 24.0f, 0.1f, audio.sustainedGateDbOffset, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.sustainedGateDbOffset = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"快包络时间常数", u8"响应瞬时冲击速度；调小=更灵敏但更易抖动", 0.001f, 0.2f, 0.001f, audio.fastTauSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.fastTauSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"慢包络时间常数", u8"背景包络平滑速度；调大=更稳但连发复位可能变慢", 0.010f, 0.8f, 0.001f, audio.slowTauSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.slowTauSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"低频截止(Hz)", u8"低频子带上限；调高会把更多能量算作低频", 60.0f, 2000.0f, 1.0f, audio.lowBandCutoffHz, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.lowBandCutoffHz = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"中频截止(Hz)", u8"中频子带上限；影响中/高频分配与方向投票", 500.0f, 8000.0f, 1.0f, audio.midBandCutoffHz, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.midBandCutoffHz = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音锁定基础时长", u8"当前版本仅影响内部锁定窗口记录，对触发结果影响较弱", 0.0f, 0.25f, 0.001f, audio.onsetLockBaseSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetLockBaseSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音锁定强度增益", u8"当前版本主要用于记录锁定窗口，影响比其他参数小", 0.0f, 0.45f, 0.001f, audio.onsetLockByLevelSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetLockByLevelSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"起音频带混合", u8"方向估计混合比：0=整体声道差，1=低/中/高频投票", 0.0f, 1.0f, 0.001f, audio.onsetLockBandVoteMix, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.onsetLockBandVoteMix = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"频带掩蔽时间常数", u8"背景掩蔽更新速度；调大=更抑制尾音但响应更慢", 0.01f, 2.0f, 0.001f, audio.bandMaskTauSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.bandMaskTauSeconds = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"频带掩蔽强度", u8"对旧能量的扣减力度；调高可减少混响拖尾影响", 0.0f, 3.0f, 0.01f, audio.bandMaskStrength, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.bandMaskStrength = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"低频投票权重", u8"方向投票时低频通道的话语权；调高更信任低频方向", 0.0f, 3.0f, 0.01f, audio.bandVoteLowWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.bandVoteLowWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"中频投票权重", u8"方向投票时中频通道的话语权；通常是最稳妥的主权重", 0.0f, 3.0f, 0.01f, audio.bandVoteMidWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.bandVoteMidWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"高频投票权重", u8"方向投票时高频通道的话语权；调高可强化枪声等尖锐音定位", 0.0f, 3.0f, 0.01f, audio.bandVoteHighWeight, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.bandVoteHighWeight = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"流匹配距离", u8"新音头与已有声流的方位距离阈值；调大更容易并入同一声源", 0.05f, 2.0f, 0.01f, audio.streamMatchDistance, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.streamMatchDistance = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"流更新混合", u8"声流方位更新速度；调高跟随更快，调低更平滑", 0.01f, 1.0f, 0.01f, audio.streamUpdateBlend, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.streamUpdateBlend = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"流保持时间", u8"声流在无新事件时的保留时长；调大可避免目标频繁丢失", 0.05f, 3.0f, 0.01f, audio.streamHoldSeconds, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.streamHoldSeconds = v;
            owner.audioCapture.setTuning(t);
        });

        addSection(u8"瞬态分类");
        addParam(u8"枪声很响阈值(dB)", u8"越低越容易判为枪声", -90.0f, -5.0f, 0.1f, audio.gunshotVeryLoudDb, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.gunshotVeryLoudDb = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"枪声响阈值(dB)", u8"配合高频条件判断枪声", -90.0f, -5.0f, 0.1f, audio.gunshotLoudDb, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.gunshotLoudDb = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"枪声高频占比", u8"达到该高频占比可增强枪声判定", 0.0f, 1.0f, 0.001f, audio.gunshotBrightMin, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.gunshotBrightMin = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"枪声高频占比(强)", u8"更亮音头时的高频判定阈值", 0.0f, 1.0f, 0.001f, audio.gunshotVeryBrightMin, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.gunshotVeryBrightMin = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"枪声起音最小值", u8"起音强于该值才参与枪声路径", 0.0f, 3.0f, 0.01f, audio.gunshotOnsetMin, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.gunshotOnsetMin = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"换弹响度阈值(dB)", u8"越低越容易判为换弹", -90.0f, -5.0f, 0.1f, audio.reloadDbMin, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.reloadDbMin = v;
            owner.audioCapture.setTuning(t);
        });
        addParam(u8"换弹高频占比", u8"换弹分类所需的最小高频占比", 0.0f, 1.0f, 0.001f, audio.reloadHighRatioMin, [this](float v)
        {
            auto t = owner.audioCapture.getTuning();
            t.reloadHighRatioMin = v;
            owner.audioCapture.setTuning(t);
        });
    }

    /**
     * @brief 布局所有参数行和分区标题
     * 
     * 根据 sectionOrder 和 rowOrder 数组，
     * 依次布局分区标题和参数行。
     */
    void layoutRows()
    {
        int y = 8;
        for (auto* section : sectionOrder)
        {
            section->setBounds(12, y, content.getWidth() - 24, 22);
            y += 26;

            for (int i = 0; i < 10000; ++i)
            {
                if (rowCursor >= rowOrder.size())
                    break;

                auto* row = rowOrder[rowCursor];
                row->setBounds(12, y, content.getWidth() - 24, 48);
                y += 52;
                ++rowCursor;

                if (rowCursor == rowOrder.size() || shouldBreakSection(rowCursor))
                    break;
            }
        }
        rowCursor = 0;
        contentHeight = y;
    }

    /**
     * @brief 判断是否在指定行索引后换行到下一个分区
     * @param rowIndex 当前行索引
     * @return 是否需要换行
     * 
     * 根据预设的行索引（12, 17, 57）判断是否结束当前分区。
     */
    bool shouldBreakSection(int rowIndex) const
    {
        return rowIndex == 12 || rowIndex == 17 || rowIndex == 57;
    }

    //==============================================================================
    // 成员变量
    
    MainComponent& owner;                          ///< 所属的 MainComponent 引用
    juce::ToggleButton hideMetersToggle;           ///< "隐藏左右音量条" 切换按钮
    juce::TextButton savePresetButton;             ///< "保存预设" 按钮
    juce::TextButton loadPresetButton;             ///< "读取预设" 按钮
    juce::TextButton resetButton;                  ///< "重置为默认参数" 按钮
    juce::Viewport viewport;                      ///< 滚动视图容器
    juce::Component content;                      ///< 滚动内容容器
    juce::OwnedArray<ParameterRow> rows;          ///< 所有参数行
    juce::OwnedArray<juce::Label> sections;       ///< 所有分区标题
    juce::Array<juce::Label*> sectionOrder;       ///< 分区标题排序
    juce::Array<ParameterRow*> rowOrder;          ///< 参数行排序
    int contentHeight = 8;                        ///< 内容总高度
    mutable int rowCursor = 0;                    ///< 布局时的行游标
};

/**
 * @brief MainComponent 构造函数
 * 
 * 初始化工作：
 * 1. 设置窗口大小约束（最小 24x24）
 * 2. 记录初始时间戳
 * 3. 加载上次的设置（从 SeePosition.settings 文件）
 * 4. 启动定时器（30Hz，约 33ms 一帧）
 * 5. 设置初始窗口大小（760x90）
 */
MainComponent::MainComponent()
{
    windowBoundsConstrainer.setMinimumOnscreenAmounts(24, 24, 24, 24);
    lastFrameTimeMs = juce::Time::getMillisecondCounterHiRes();
    lastSettingsSaveTimeMs = lastFrameTimeMs;

    loadSettings();

    startTimerHz(30);
    setSize(760, 90);
}

/**
 * @brief MainComponent 析构函数
 * 
 * 清理工作：
 * 1. 保存当前设置到文件
 * 2. 停止定时器
 */
MainComponent::~MainComponent()
{
    saveSettings();
    stopTimer();
}

/**
 * @brief 绘制组件内容（30fps 定时调用）
 * @param g JUCE Graphics 对象，用于绘制
 * 
 * 绘制内容（按绘制顺序）：
 * 1. 背景：鼠标悬停时显示圆角矩形底图
 * 2. 轨道：中央灰色轨道（声像位置指示器滑动的轨迹）
 * 3. 中线：轨道中心的白色竖线（标志声像中心）
 * 4. 音量条：左侧和右侧的音量计量条（可隐藏）
 * 5. 历史点：之前检测到的瞬态位置（圆形=枪声，圆角矩形=脚步声，三角形=换弹）
 * 6. 主圆点：当前声像位置的主指示器（颜色表示置信度）
 * 7. 悬停 UI：版本信息、设置按钮、关闭按钮、拖动提示
 */
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);

    const auto fullBar = getLocalBounds().toFloat().reduced(8.0f, 12.0f);
    if (isHovering)
    {
        g.setColour(juce::Colour::fromRGB(22, 24, 28));
        g.fillRoundedRectangle(fullBar, 16.0f);
    }

    const auto track = getTrackBounds();
    g.setColour(juce::Colour::fromRGB(62, 66, 74));
    g.fillRoundedRectangle(track, track.getHeight() * 0.5f);

    const float centerX = std::floor(track.getCentreX()) + 0.5f;
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 90));
    g.drawLine(centerX, track.getY() - 7.0f, centerX, track.getBottom() + 7.0f, 1.6f);

    const float meterWidth = 26.0f;
    const float meterHeight = 44.0f;
    const float meterY = fullBar.getCentreY() - meterHeight * 0.5f;
    const juce::Rectangle<float> leftMeter(fullBar.getX() + 8.0f, meterY, meterWidth, meterHeight);
    const juce::Rectangle<float> rightMeter(fullBar.getRight() - meterWidth - 8.0f, meterY, meterWidth, meterHeight);

    const auto drawMeter = [&](const juce::Rectangle<float>& meterBounds, float level)
    {
        g.setColour(juce::Colour::fromRGBA(215, 220, 230, isHovering ? 120 : 95));
        g.drawRoundedRectangle(meterBounds, 8.0f, 1.2f);

        const float clamped = juce::jlimit(0.0f, 1.0f, level);
        const float innerPad = 3.0f;
        const auto inner = meterBounds.reduced(innerPad);
        const float fillHeight = inner.getHeight() * clamped;
        juce::Rectangle<float> fillRect(inner.getX(), inner.getBottom() - fillHeight, inner.getWidth(), fillHeight);

        const auto fillColour = getMeterColour(clamped);
        g.setColour(fillColour.withAlpha(isHovering ? 0.95f : 0.88f));
        g.fillRoundedRectangle(fillRect, 4.0f);
    };

    if (!hideSideMeters)
    {
        drawMeter(leftMeter, displayLeftLevel);
        drawMeter(rightMeter, displayRightLevel);
    }

    for (const auto& dot : transientDots)
    {
        const float t = juce::jlimit(0.0f, 1.0f, dot.ageSeconds / juce::jmax(0.001f, dot.lifeSeconds));
        const float fadeExponent = juce::jmax(0.05f, visualTuning.historyFadeExponent);
        const float confidenceAlpha = 0.25f + 0.75f * juce::jlimit(0.0f, 1.0f, dot.directionConfidence);
        const float alpha = std::pow(1.0f - t, fadeExponent) * confidenceAlpha;
        if (alpha <= 0.001f)
            continue;

        const float x = mapBalanceToX(dot.balance, track);
        const float radiusMin = juce::jmax(0.5f, visualTuning.historyRadiusMin);
        const float radiusMax = juce::jmax(radiusMin, visualTuning.historyRadiusMax);
        const float radius = radiusMin + (radiusMax - radiusMin) * juce::jlimit(0.0f, 1.0f, dot.level);
        const juce::Rectangle<float> hitDot(x - radius,
                                            track.getCentreY() - radius,
                                            radius * 2.0f,
                                            radius * 2.0f);

        const auto hitColour = getMeterColour(dot.lowMidRatio).withAlpha(alpha);
        g.setColour(hitColour);

        switch (dot.transientClass)
        {
            case AudioCaptureService::TransientClass::gunshot:
            {
                g.fillEllipse(hitDot);
                break;
            }
            case AudioCaptureService::TransientClass::footstep:
            {
                g.fillRoundedRectangle(hitDot, 2.0f);
                break;
            }
            case AudioCaptureService::TransientClass::reload:
            {
                juce::Path triangle;
                triangle.startNewSubPath(hitDot.getCentreX(), hitDot.getY());
                triangle.lineTo(hitDot.getRight(), hitDot.getBottom());
                triangle.lineTo(hitDot.getX(), hitDot.getBottom());
                triangle.closeSubPath();
                g.fillPath(triangle);
                break;
            }
        }
    }

    const float effectiveBalance = displayBalance * displayPositionWeight;
    const float mainDotX = mapBalanceToX(effectiveBalance, track);
    const float mainDotRadius = track.getHeight() * 0.38f;
    const juce::Rectangle<float> mainDot(mainDotX - mainDotRadius,
                                         track.getCentreY() - mainDotRadius,
                                         mainDotRadius * 2.0f,
                                         mainDotRadius * 2.0f);

    g.setColour(getIndicatorColour(displayConfidence).withAlpha(displayDotAlpha));
    g.fillEllipse(mainDot);

    g.setColour(juce::Colours::black.withAlpha(0.10f + 0.25f * displayDotAlpha));
    g.drawEllipse(mainDot, 1.2f);

    if (isHovering)
    {
        g.setColour(juce::Colour::fromRGBA(206, 212, 222, 190));
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText(u8"按住ALT拖动软件位置",
                         fullBar.withTrimmedTop(fullBar.getHeight() * 0.62f).toNearestInt(),
                         juce::Justification::centred,
                         1);

        // Draw version text with clickable link style
        // Calculate versionLinkBounds dynamically to ensure it's properly initialized
        {
            juce::Font versionFont(juce::FontOptions(14.0f));
            juce::GlyphArrangement ga;
            const juce::String versionText = u8"SeePosition v0.1.0 iisaacbeats.cn";
            ga.addLineOfText(versionFont, versionText, 0.0f, 0.0f);
            const float textWidth = ga.getBoundingBox(0, versionText.length(), true).getWidth() + 4.0f;
            const float textHeight = versionFont.getHeight();
            versionLinkBounds = { fullBar.getX() + 8.0f, fullBar.getY() + 6.0f, textWidth, textHeight };
        }

        const bool versionHot = isOverVersionLink();
        g.setFont(juce::FontOptions(14.0f));
        g.setColour(versionHot ? juce::Colours::white : juce::Colour::fromRGBA(206, 212, 222, 200));
        g.drawSingleLineText(u8"SeePosition v0.1.0 iisaacbeats.cn",
                            (int)std::round(versionLinkBounds.getX()),
                            (int)std::round(versionLinkBounds.getBottom() - 2.0f));

        // Underline the website part when hovering
        if (versionHot)
        {
            juce::Font linkFont(juce::FontOptions(14.0f));
            juce::GlyphArrangement gaPrefix, gaLink;
            const juce::String prefix = u8"SeePosition v0.1.0 ";
            const juce::String linkPart = u8"iisaacbeats.cn";
            gaPrefix.addLineOfText(linkFont, prefix, 0.0f, 0.0f);
            gaLink.addLineOfText(linkFont, linkPart, 0.0f, 0.0f);
            const float prefixWidth = gaPrefix.getBoundingBox(0, prefix.length(), true).getWidth();
            const float linkWidth = gaLink.getBoundingBox(0, linkPart.length(), true).getWidth();
            const float underlineY = versionLinkBounds.getBottom() + 1.0f;
            g.drawLine(versionLinkBounds.getX() + prefixWidth,
                       underlineY,
                       versionLinkBounds.getX() + prefixWidth + linkWidth,
                       underlineY,
                       0.8f);
        }

        const bool settingsHot = isOverSettingsButton();
        g.setColour(juce::Colour::fromRGB(43, 47, 54));
        g.fillRoundedRectangle(settingsButtonBounds, 8.0f);
        g.setColour(settingsHot ? juce::Colours::white : juce::Colour::fromRGB(185, 189, 198));
        const auto icon = settingsButtonBounds.reduced(4.0f, 5.0f);
        g.drawLine(icon.getX(), icon.getY() + 2.0f, icon.getRight(), icon.getY() + 2.0f, 1.4f);
        g.drawLine(icon.getX(), icon.getCentreY(), icon.getRight(), icon.getCentreY(), 1.4f);
        g.drawLine(icon.getX(), icon.getBottom() - 2.0f, icon.getRight(), icon.getBottom() - 2.0f, 1.4f);

        g.setColour(juce::Colour::fromRGB(43, 47, 54));
        g.fillRoundedRectangle(closeButtonBounds, 8.0f);

        const float pad = 5.0f;
        const float x1 = closeButtonBounds.getX() + pad;
        const float y1 = closeButtonBounds.getY() + pad;
        const float x2 = closeButtonBounds.getRight() - pad;
        const float y2 = closeButtonBounds.getBottom() - pad;

        const bool closeHot = closeButtonBounds.contains((float)getMouseXYRelative().x,
                                                         (float)getMouseXYRelative().y);
        g.setColour(closeHot ? juce::Colours::white : juce::Colour::fromRGB(185, 189, 198));
        g.drawLine(x1, y1, x2, y2, 1.8f);
        g.drawLine(x1, y2, x2, y1, 1.8f);
    }
}

/**
 * @brief 组件大小变化时的回调
 * 
 * 重新计算以下区域：
 * - closeButtonBounds：右上角关闭按钮的边界
 * - settingsButtonBounds：左下角设置按钮的边界
 * - versionLinkBounds：左上角版本信息的边界（仅在悬停时计算）
 */
void MainComponent::resized()
{
    const auto bounds = getLocalBounds().toFloat();
    const auto fullBar = bounds.reduced(8.0f, 12.0f);
    const float buttonSize = 20.0f;
    closeButtonBounds = { fullBar.getRight() - buttonSize - 6.0f, fullBar.getY() + 6.0f, buttonSize, buttonSize };
    settingsButtonBounds = { fullBar.getX() + 6.0f, fullBar.getBottom() - buttonSize - 6.0f, buttonSize, buttonSize };

    // Version link in top-left corner
    if (isHovering)
    {
        juce::Font versionFont(juce::FontOptions(11.0f));
        juce::GlyphArrangement ga;
        const juce::String versionText = u8"SeePosition v0.1.0 iisaacbeats.cn";
        ga.addLineOfText(versionFont, versionText, 0.0f, 0.0f);
        const float textWidth = ga.getBoundingBox(0, versionText.length(), true).getWidth() + 4.0f;
        const float textHeight = versionFont.getHeight();
        versionLinkBounds = { fullBar.getX() + 8.0f, fullBar.getY() + 6.0f, textWidth, textHeight };
    }
}

/**
 * @brief 鼠标移动事件处理
 * @param event JUCE 鼠标事件对象（未使用，通过 getMouseXYRelative() 获取位置）
 * 
 * 功能：
 * 1. 检测鼠标是否悬停在关闭按钮、设置按钮或版本链接上
 * 2. 检测是否按住了 ALT 键（用于拖动窗口）
 * 3. 根据状态切换鼠标光标：
 *    - 悬停在按钮/链接上：PointingHandCursor（手型）
 *    - 按住 ALT：DraggingHandCursor（拖动手型）
 *    - 其他：NormalCursor（箭头）
 * 4. 触发相关区域的重绘（用于按钮悬停效果）
 */
void MainComponent::mouseMove(const juce::MouseEvent&)
{
    const auto mousePos = getMouseXYRelative();
    const bool onCloseButton = isHovering
                               && closeButtonBounds.contains((float)mousePos.x,
                                                             (float)mousePos.y);
    const bool onSettingsButton = isHovering
                                  && settingsButtonBounds.contains((float)mousePos.x,
                                                                   (float)mousePos.y);
    const bool onVersionLink = isHovering
                               && versionLinkBounds.contains((float)mousePos.x,
                                                            (float)mousePos.y);
    const bool altDown = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();

    if (onCloseButton || onSettingsButton || onVersionLink)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else if (altDown)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);

    repaint(closeButtonBounds.getSmallestIntegerContainer());
    repaint(settingsButtonBounds.getSmallestIntegerContainer());
    repaint(versionLinkBounds.toNearestInt());
}

/**
 * @brief 鼠标进入组件事件处理
 * @param event JUCE 鼠标事件对象（未使用）
 * 
 * 功能：
 * 1. 设置 isHovering = true（显示底图、按钮等 UI）
 * 2. 计算 versionLinkBounds（版本信息的点击区域）
 * 3. 触发重绘（显示悬停 UI）
 */
void MainComponent::mouseEnter(const juce::MouseEvent&)
{
    isHovering = true;
    // Calculate versionLinkBounds when hovering starts
    const auto bounds = getLocalBounds().toFloat();
    const auto fullBar = bounds.reduced(8.0f, 12.0f);
    juce::Font versionFont(juce::FontOptions(14.0f));
    juce::GlyphArrangement ga;
    const juce::String versionText = u8"SeePosition v0.1.0 iisaacbeats.cn";
    ga.addLineOfText(versionFont, versionText, 0.0f, 0.0f);
    const float textWidth = ga.getBoundingBox(0, versionText.length(), true).getWidth() + 4.0f;
    const float textHeight = versionFont.getHeight();
    versionLinkBounds = { fullBar.getX() + 8.0f, fullBar.getY() + 6.0f, textWidth, textHeight };
    repaint();
}

/**
 * @brief 鼠标离开组件事件处理
 * @param event JUCE 鼠标事件对象（未使用）
 * 
 * 功能：
 * 1. 设置 isHovering = false（隐藏底图、按钮等 UI）
 * 2. 设置 altDragActive = false（取消拖动状态）
 * 3. 恢复默认鼠标光标
 * 4. 触发重绘（隐藏悬停 UI）
 */
void MainComponent::mouseExit(const juce::MouseEvent&)
{
    isHovering = false;
    altDragActive = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

/**
 * @brief 鼠标按下事件处理
 * @param event JUCE 鼠标事件对象
 * 
 * 功能：
 * 1. 检测是否点击了关闭按钮（触发退出应用）
 * 2. 检测是否点击了设置按钮（打开设置面板）
 * 3. 检测是否按住 ALT 键（启动窗口拖动）
 * 4. 如果按住 ALT 且不在按钮上，启动窗口拖动
 */
void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    const bool onCloseButton = isHovering && closeButtonBounds.contains((float)event.position.x, (float)event.position.y);
    const bool onSettingsButton = isHovering && settingsButtonBounds.contains((float)event.position.x, (float)event.position.y);
    const bool canDrag = event.mods.isAltDown() && !onCloseButton && !onSettingsButton;

    altDragActive = false;
    if (!canDrag)
        return;

    if (auto* topLevel = getTopLevelComponent())
    {
        altDragActive = true;
        windowDragger.startDraggingComponent(topLevel, event);
    }
}

/**
 * @brief 鼠标拖动事件处理
 * @param event JUCE 鼠标事件对象
 * 
 * 功能：
 * 如果 altDragActive == true，持续拖动窗口
 */
void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!altDragActive)
        return;

    if (auto* topLevel = getTopLevelComponent())
        windowDragger.dragComponent(topLevel, event, &windowBoundsConstrainer);
}

/**
 * @brief 鼠标释放事件处理
 * @param event JUCE 鼠标事件对象
 * 
 * 功能：
 * 1. 如果正在拖动（altDragActive），停止拖动
 * 2. 如果点击了关闭按钮，退出应用
 * 3. 如果点击了设置按钮，打开设置面板
 * 4. 如果点击了版本链接，打开网站（https://iisaacbeats.cn）
 */
void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    if (altDragActive)
    {
        altDragActive = false;
        return;
    }

    if (isHovering && closeButtonBounds.contains((float)event.position.x, (float)event.position.y))
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
        return;
    }

    if (isHovering && settingsButtonBounds.contains((float)event.position.x, (float)event.position.y))
    {
        openSettingsPopup();
        return;
    }

    if (isHovering && versionLinkBounds.contains((float)event.position.x, (float)event.position.y))
    {
        juce::URL url("https://iisaacbeats.cn");
        url.launchInDefaultBrowser();
    }
}

/**
 * @brief 定时器回调函数（30Hz 调用）
 * 
 * 主要功能：
 * 1. 计算帧间隔时间 dt
 * 2. 获取音频捕获服务的当前数据（balance, width, left/right level）
 * 3. 应用中心偏置校正（静音时自动校正中心偏移）
 * 4. 应用声像死区和归零阈值
 * 5. 计算显示参数（signal, confidence, alpha, positionWeight）
 * 6. 平滑更新显示值（避免跳变）
 * 7. 处理瞬态事件队列（从 audioCapture 弹出事件，添加到 transientDots）
 * 8. 更新历史点的年龄，删除过期的历史点
 * 9. 触发重绘（如果数据有变化或鼠标悬停）
 * 10. 定期保存设置（如果 settingsDirty 且距离上次保存超过 800ms）
 */
void MainComponent::timerCallback()
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float)juce::jlimit(0.001, 0.100, (nowMs - lastFrameTimeMs) * 0.001);
    lastFrameTimeMs = nowMs;

    const float rawBalance = juce::jlimit(-1.0f, 1.0f, audioCapture.getBalance());
    const float targetWidth = juce::jlimit(0.0f, 1.0f, audioCapture.getWidth());
    const float targetLeftLevel = juce::jlimit(0.0f, 1.0f, audioCapture.getLeftLevel());
    const float targetRightLevel = juce::jlimit(0.0f, 1.0f, audioCapture.getRightLevel());

    // Auto-calibrate a tiny resting offset so the idle dot sits on exact center.
    const float monoLevel = 0.5f * (targetLeftLevel + targetRightLevel);
    if (monoLevel < visualTuning.centerBiasMonoGate && std::abs(rawBalance - centerBias) < 0.22f)
        centerBias += visualTuning.centerBiasLearningRate * (rawBalance - centerBias);

    float targetBalance = juce::jlimit(-1.0f, 1.0f, rawBalance - centerBias);
    if (std::abs(targetBalance) < visualTuning.balanceDeadzone)
        targetBalance = 0.0f;

    const auto snapNearZero = [this](float v)
    {
        return std::abs(v) < visualTuning.snapNearZeroEpsilon ? 0.0f : v;
    };

    const float nextBalance = snapNearZero(targetBalance);
    const float nextWidth = snapNearZero(targetWidth);
    const float nextLeftLevel = snapNearZero(targetLeftLevel);
    const float nextRightLevel = snapNearZero(targetRightLevel);

    const float nextSignal = 0.5f * (nextLeftLevel + nextRightLevel);
    const float gateSpan = juce::jmax(0.001f, visualTuning.levelGateHigh - visualTuning.levelGateLow);
    const float levelGate = juce::jlimit(0.0f, 1.0f, (nextSignal - visualTuning.levelGateLow) / gateSpan);
    const float minWeight = juce::jlimit(0.0f, 1.0f, visualTuning.positionWeightMin);
    const float nextPositionWeight = minWeight + (1.0f - minWeight) * levelGate;

    const float lrDiff = std::abs(nextRightLevel - nextLeftLevel);
    const float diffRatio = lrDiff / (nextLeftLevel + nextRightLevel + 1.0e-4f);
    const float nextConfidence = juce::jlimit(0.0f, 1.0f, diffRatio * levelGate);
    const float minAlpha = juce::jlimit(0.0f, 1.0f, visualTuning.dotAlphaMin);
    const float alphaPower = juce::jmax(0.05f, visualTuning.dotAlphaPower);
    const float nextDotAlpha = juce::jlimit(minAlpha,
                                            1.0f,
                                            minAlpha + (1.0f - minAlpha) * std::pow(levelGate, alphaPower));

    bool changed = false;
    if (std::abs(nextBalance - displayBalance) > 0.0001f)
    {
        displayBalance = nextBalance;
        changed = true;
    }

    if (std::abs(nextWidth - displayWidth) > 0.0001f)
    {
        displayWidth = nextWidth;
        changed = true;
    }

    if (std::abs(nextLeftLevel - displayLeftLevel) > 0.0001f)
    {
        displayLeftLevel = nextLeftLevel;
        changed = true;
    }

    if (std::abs(nextRightLevel - displayRightLevel) > 0.0001f)
    {
        displayRightLevel = nextRightLevel;
        changed = true;
    }

    if (std::abs(nextSignal - displaySignal) > 0.0001f)
    {
        displaySignal = nextSignal;
        changed = true;
    }

    if (std::abs(nextConfidence - displayConfidence) > 0.0001f)
    {
        displayConfidence = nextConfidence;
        changed = true;
    }

    if (std::abs(nextDotAlpha - displayDotAlpha) > 0.0001f)
    {
        displayDotAlpha = nextDotAlpha;
        changed = true;
    }

    if (std::abs(nextPositionWeight - displayPositionWeight) > 0.0001f)
    {
        displayPositionWeight = nextPositionWeight;
        changed = true;
    }

    AudioCaptureService::TransientEvent event;
    while (audioCapture.popNextTransientEvent(event))
    {
        TransientDot dot;
        dot.balance = juce::jlimit(-1.0f, 1.0f, event.balance);
        dot.level = event.level;
        dot.lowMidRatio = event.lowMidRatio;
        dot.directionConfidence = event.directionConfidence;
        dot.streamId = event.streamId;
        dot.transientClass = event.transientClass;
        dot.ageSeconds = 0.0f;
        const float lifeMin = juce::jmax(0.05f, visualTuning.historyLifeMinSeconds);
        const float lifeMax = juce::jmax(lifeMin, visualTuning.historyLifeMaxSeconds);
        const float confidenceBoost = 0.80f + 0.40f * juce::jlimit(0.0f, 1.0f, event.directionConfidence);
        dot.lifeSeconds = (lifeMin + (lifeMax - lifeMin) * juce::jlimit(0.0f, 1.0f, event.level)) * confidenceBoost;
        transientDots.push_back(dot);
        changed = true;
    }

    for (auto& dot : transientDots)
        dot.ageSeconds += dt;

    const auto oldSize = transientDots.size();
    transientDots.erase(std::remove_if(transientDots.begin(), transientDots.end(),
                                       [](const TransientDot& dot) { return dot.ageSeconds >= dot.lifeSeconds; }),
                        transientDots.end());

    if (transientDots.size() != oldSize)
        changed = true;

    if (changed || isHovering)
        repaint();

    if (settingsDirty && nowMs - lastSettingsSaveTimeMs > 800.0)
        saveSettings();
}

/**
 * @brief 获取轨道（声像指示器轨迹）的边界矩形
 * @return 轨道的矩形区域
 * 
 * 轨道位于组件中央，高度为 16 像素，左右各留 72 像素边距，上下各留 26 像素边距。
 */
juce::Rectangle<float> MainComponent::getTrackBounds() const
{
    const auto area = getLocalBounds().toFloat().reduced(72.0f, 26.0f);
    return { area.getX(), area.getCentreY() - 8.0f, area.getWidth(), 16.0f };
}

/**
 * @brief 根据置信度获取主圆点的颜色
 * @param widthValue 置信度值（0.0 ~ 1.0）
 * @return 对应的颜色（绿色=低置信度，红色=高置信度）
 * 
 * 颜色映射规则：
 * - 低置信度：绿色 (66, 211, 99)
 * - 高置信度：红色 (234, 74, 63)
 * - 通过 mainColourMapStart、mainColourMapSpan、mainColourMapExponent 控制映射曲线
 */
juce::Colour MainComponent::getIndicatorColour(float widthValue) const
{
    const auto lowConfidence = juce::Colour::fromRGB(66, 211, 99);
    const auto highConfidence = juce::Colour::fromRGB(234, 74, 63);

    // Inverse mapping: low confidence stays green, high confidence trends red earlier.
    const float mapSpan = juce::jmax(0.001f, visualTuning.mainColourMapSpan);
    const float normalized = juce::jlimit(0.0f, 1.0f, (widthValue - visualTuning.mainColourMapStart) / mapSpan);
    const float t = std::pow(normalized, juce::jmax(0.05f, visualTuning.mainColourMapExponent));
    return lowConfidence.interpolatedWith(highConfidence, t);
}

/**
 * @brief 根据音量获取音量条的颜色
 * @param amount 音量值（0.0 ~ 1.0）
 * @return 对应的颜色（绿色=低音量，红色=高音量）
 * 
 * 颜色映射规则：
 * - 低音量：绿色 (66, 211, 99)
 * - 高音量：红色 (234, 74, 63)
 */
juce::Colour MainComponent::getMeterColour(float amount) const
{
    const auto low = juce::Colour::fromRGB(66, 211, 99);
    const auto high = juce::Colour::fromRGB(234, 74, 63);
    return low.interpolatedWith(high, juce::jlimit(0.0f, 1.0f, amount));
}

/**
 * @brief 对声像值进行非线性映射（用于显示）
 * @param balanceValue 原始声像值（-1.0 ~ 1.0）
 * @return 映射后的声像值
 * 
 * 使用 gamma 校正（gamma = 0.62）扩展小幅度偏移，
 * 使细微的声像变化更容易被观察到。
 */
float MainComponent::mapBalanceForDisplay(float balanceValue) const
{
    const float x = juce::jlimit(-1.0f, 1.0f, balanceValue);
    const float magnitude = std::abs(x);

    // Gamma < 1 expands small offsets so subtle pans are easier to read.
    constexpr float gamma = 0.62f;
    const float curved = std::pow(magnitude, gamma);
    return x >= 0.0f ? curved : -curved;
}

/**
 * @brief 将声像值映射到 X 坐标
 * @param balanceValue 声像值（-1.0 ~ 1.0）
 * @param track 轨道矩形
 * @return 对应的 X 坐标（轨道上的位置）
 * 
 * 先将声像值进行非线性映射（mapBalanceForDisplay），
 * 然后映射到轨道的 X 坐标。
 */
float MainComponent::mapBalanceToX(float balanceValue, const juce::Rectangle<float>& track) const
{
    const float curved = mapBalanceForDisplay(balanceValue);
    const float center = std::floor(track.getCentreX()) + 0.5f;
    return center + curved * (track.getWidth() * 0.5f);
}

/**
 * @brief 打开设置面板弹窗
 * 
 * 功能：
 * 1. 如果设置面板已打开，将其置于前台并获取焦点
 * 2. 否则，创建新的 MainComponentSettingsPanel
 * 3. 使用 DialogWindow::LaunchOptions 配置弹窗属性
 * 4. 启动异步弹窗（非模态）
 * 5. 设置弹窗属性：置顶、可调整大小、居中显示
 */
void MainComponent::openSettingsPopup()
{
    if (settingsDialog != nullptr)
    {
        settingsDialog->toFront(true);
        settingsDialog->grabKeyboardFocus();
        return;
    }

    auto panel = std::make_unique<MainComponentSettingsPanel>(*this);
    panel->setSize(640, 560);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "SeePosition Settings";
    options.dialogBackgroundColour = juce::Colour::fromRGB(24, 26, 31);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.content.setOwned(panel.release());

    auto* window = options.launchAsync();
    settingsDialog = window;

    if (window != nullptr)
    {
        window->setAlwaysOnTop(true);
        window->setResizeLimits(560, 420, 1400, 1200);
        window->centreAroundComponent(this, 640, 560);
    }
}

/**
 * @brief 判断鼠标是否悬停在设置按钮上
 * @return 是否悬停在设置按钮上
 */
bool MainComponent::isOverSettingsButton() const
{
    const auto mouse = getMouseXYRelative();
    return settingsButtonBounds.contains((float)mouse.x, (float)mouse.y);
}

/**
 * @brief 判断鼠标是否悬停在版本链接上
 * @return 是否悬停在版本链接上
 * 
 * 注意：仅在 isHovering == true 时有效
 */
bool MainComponent::isOverVersionLink() const
{
    if (!isHovering)
        return false;
    const auto mouse = getMouseXYRelative();
    return versionLinkBounds.contains((float)mouse.x, (float)mouse.y);
}

/**
 * @brief 获取当前视觉调优参数
 * @return VisualTuning 结构体（包含所有视觉显示相关参数）
 */
MainComponent::VisualTuning MainComponent::getVisualTuning() const noexcept
{
    return visualTuning;
}

/**
 * @brief 设置视觉调优参数
 * @param tuning 新的视觉调优参数
 * 
 * 功能：
 * 1. 将所有参数限制在合理范围内
 * 2. 更新 visualTuning 成员变量
 * 3. 标记设置为"脏"（需要保存）
 */
void MainComponent::setVisualTuning(const VisualTuning& tuning) noexcept
{
    visualTuning.balanceDeadzone = juce::jlimit(0.0f, 0.30f, tuning.balanceDeadzone);
    visualTuning.snapNearZeroEpsilon = juce::jlimit(0.00001f, 0.05f, tuning.snapNearZeroEpsilon);
    visualTuning.centerBiasLearningRate = juce::jlimit(0.0f, 0.50f, tuning.centerBiasLearningRate);
    visualTuning.centerBiasMonoGate = juce::jlimit(0.0f, 1.0f, tuning.centerBiasMonoGate);
    visualTuning.levelGateLow = juce::jlimit(0.0f, 1.0f, tuning.levelGateLow);
    visualTuning.levelGateHigh = juce::jlimit(0.0f, 1.2f, tuning.levelGateHigh);
    visualTuning.positionWeightMin = juce::jlimit(0.0f, 1.0f, tuning.positionWeightMin);
    visualTuning.dotAlphaMin = juce::jlimit(0.0f, 1.0f, tuning.dotAlphaMin);
    visualTuning.dotAlphaPower = juce::jlimit(0.05f, 6.0f, tuning.dotAlphaPower);
    visualTuning.mainColourMapStart = juce::jlimit(0.0f, 1.0f, tuning.mainColourMapStart);
    visualTuning.mainColourMapSpan = juce::jlimit(0.001f, 2.0f, tuning.mainColourMapSpan);
    visualTuning.mainColourMapExponent = juce::jlimit(0.05f, 5.0f, tuning.mainColourMapExponent);
    visualTuning.historyRadiusMin = juce::jlimit(0.5f, 30.0f, tuning.historyRadiusMin);
    visualTuning.historyRadiusMax = juce::jlimit(0.5f, 40.0f, tuning.historyRadiusMax);
    visualTuning.historyFadeExponent = juce::jlimit(0.05f, 8.0f, tuning.historyFadeExponent);
    visualTuning.historyLifeMinSeconds = juce::jlimit(0.05f, 10.0f, tuning.historyLifeMinSeconds);
    visualTuning.historyLifeMaxSeconds = juce::jlimit(0.05f, 10.0f, tuning.historyLifeMaxSeconds);
    markSettingsDirty();
}

/**
 * @brief 重置所有参数为默认值
 * 
 * 功能：
 * 1. 重置视觉调优参数为默认值
 * 2. 重置音频调优参数为默认值
 * 3. 显示左右音量条（hideSideMeters = false）
 * 4. 重置中心偏置（centerBias = 0.0f）
 * 5. 标记设置为"脏"并立即保存
 * 6. 触发重绘
 */
void MainComponent::resetParametersToDefaults()
{
    setVisualTuning(VisualTuning {});
    audioCapture.setTuning(AudioCaptureService::Tuning {});
    hideSideMeters = false;
    centerBias = 0.0f;
    markSettingsDirty();
    saveSettings();
    repaint();
}

/**
 * @brief 标记设置为"脏"（需要保存）
 * 
 * 设置 settingsDirty = true，定时器回调会定期检查并保存。
 */
void MainComponent::markSettingsDirty() noexcept
{
    settingsDirty = true;
}

/**
 * @brief 查询是否隐藏左右音量条
 * @return 是否隐藏
 */
bool MainComponent::areSideMetersHidden() const noexcept
{
    return hideSideMeters;
}

/**
 * @brief 设置是否隐藏左右音量条
 * @param shouldHide 是否隐藏
 */
void MainComponent::setSideMetersHidden(bool shouldHide)
{
    if (hideSideMeters == shouldHide)
        return;

    hideSideMeters = shouldHide;
    markSettingsDirty();
    repaint();
}

/**
 * @brief 导出当前参数预设到文件
 * 
 * 功能：
 * 1. 打开文件选择器（保存模式）
 * 2. 默认文件名为 "SeePositionPreset.settings"
 * 3. 用户选择文件后，将当前设置保存为 XML 格式
 * 4. 文件扩展名强制为 .settings
 */
void MainComponent::exportPresetToFile()
{
    presetFileChooser = std::make_unique<juce::FileChooser>(u8"保存预设文件",
                                                             getSettingsFile().getSiblingFile("SeePositionPreset.settings"),
                                                             "*.settings");

    presetFileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                       | juce::FileBrowserComponent::canSelectFiles
                                       | juce::FileBrowserComponent::warnAboutOverwriting,
                                   [this](const juce::FileChooser& chooser)
                                   {
                                       auto file = chooser.getResult();
                                       if (file == juce::File())
                                       {
                                           presetFileChooser.reset();
                                           return;
                                       }

                                       if (!file.hasFileExtension("settings"))
                                           file = file.withFileExtension(".settings");

                                       file.getParentDirectory().createDirectory();
                                       const auto tree = createSettingsTree();
                                       if (auto xml = tree.createXml())
                                           xml->writeTo(file, {});

                                       presetFileChooser.reset();
                                   });
}

/**
 * @brief 从文件导入参数预设
 * @param onFinished 导入完成后的回调函数（bool 参数表示是否成功应用）
 * 
 * 功能：
 * 1. 打开文件选择器（打开模式）
 * 2. 用户选择 .settings 文件后，解析 XML
 * 3. 验证文件格式（必须是 SeePositionSettings 根节点）
 * 4. 应用设置到当前参数
 * 5. 保存设置并触发重绘
 * 6. 调用 onFinished 回调通知调用者
 */
void MainComponent::importPresetFromFile(std::function<void(bool)> onFinished)
{
    presetFileChooser = std::make_unique<juce::FileChooser>(u8"读取预设文件",
                                                             getSettingsFile().getParentDirectory(),
                                                             "*.settings");

    presetFileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                       | juce::FileBrowserComponent::canSelectFiles,
                                   [this, onFinished = std::move(onFinished)](const juce::FileChooser& chooser)
                                   {
                                       const auto file = chooser.getResult();
                                       if (file == juce::File() || !file.existsAsFile())
                                       {
                                           if (onFinished)
                                               onFinished(false);
                                           presetFileChooser.reset();
                                           return;
                                       }

                                       std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
                                       if (xml == nullptr)
                                       {
                                           if (onFinished)
                                               onFinished(false);
                                           presetFileChooser.reset();
                                           return;
                                       }

                                       const auto tree = juce::ValueTree::fromXml(*xml);
                                       if (!tree.isValid() || tree.getType().toString() != settingsRootId)
                                       {
                                           if (onFinished)
                                               onFinished(false);
                                           presetFileChooser.reset();
                                           return;
                                       }

                                       applySettingsTree(tree);
                                       markSettingsDirty();
                                       saveSettings();
                                       repaint();
                                       if (onFinished)
                                           onFinished(true);
                                       presetFileChooser.reset();
                                   });
}

/**
 * @brief 获取设置文件的路径
 * @return 设置文件的 File 对象
 * 
 * 设置文件位于：
 * - Windows: %APPDATA%\\SeePosition\\SeePosition.settings
 * - macOS: ~/Library/Application Support/SeePosition/SeePosition.settings
 * - Linux: ~/.config/SeePosition/SeePosition.settings
 */
juce::File MainComponent::getSettingsFile() const
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("SeePosition");
    return dir.getChildFile("SeePosition.settings");
}

/**
 * @brief 创建设置树的 XML 表示
 * @return juce::ValueTree 对象（包含当前所有设置）
 * 
 * 设置树结构：
 * - Root: "SeePositionSettings"
 *   - Child: "Visual" (视觉调优参数)
 *   - Child: "Audio" (音频调优参数)
 */
juce::ValueTree MainComponent::createSettingsTree() const
{
    juce::ValueTree root(settingsRootId);
    root.setProperty("version", 1, nullptr);
    root.setProperty("hideSideMeters", hideSideMeters, nullptr);

    const auto v = visualTuning;
    juce::ValueTree visual(visualNodeId);
    visual.setProperty("balanceDeadzone", v.balanceDeadzone, nullptr);
    visual.setProperty("snapNearZeroEpsilon", v.snapNearZeroEpsilon, nullptr);
    visual.setProperty("centerBiasLearningRate", v.centerBiasLearningRate, nullptr);
    visual.setProperty("centerBiasMonoGate", v.centerBiasMonoGate, nullptr);
    visual.setProperty("levelGateLow", v.levelGateLow, nullptr);
    visual.setProperty("levelGateHigh", v.levelGateHigh, nullptr);
    visual.setProperty("positionWeightMin", v.positionWeightMin, nullptr);
    visual.setProperty("dotAlphaMin", v.dotAlphaMin, nullptr);
    visual.setProperty("dotAlphaPower", v.dotAlphaPower, nullptr);
    visual.setProperty("mainColourMapStart", v.mainColourMapStart, nullptr);
    visual.setProperty("mainColourMapSpan", v.mainColourMapSpan, nullptr);
    visual.setProperty("mainColourMapExponent", v.mainColourMapExponent, nullptr);
    visual.setProperty("historyRadiusMin", v.historyRadiusMin, nullptr);
    visual.setProperty("historyRadiusMax", v.historyRadiusMax, nullptr);
    visual.setProperty("historyFadeExponent", v.historyFadeExponent, nullptr);
    visual.setProperty("historyLifeMinSeconds", v.historyLifeMinSeconds, nullptr);
    visual.setProperty("historyLifeMaxSeconds", v.historyLifeMaxSeconds, nullptr);
    root.appendChild(visual, nullptr);

    const auto a = audioCapture.getTuning();
    juce::ValueTree audio(audioNodeId);
    audio.setProperty("loudEnoughDb", a.loudEnoughDb, nullptr);
    audio.setProperty("onsetContrastStart", a.onsetContrastStart, nullptr);
    audio.setProperty("onsetRatioStart", a.onsetRatioStart, nullptr);
    audio.setProperty("onsetContrastSettle", a.onsetContrastSettle, nullptr);
    audio.setProperty("onsetRatioSettle", a.onsetRatioSettle, nullptr);
    audio.setProperty("cooldownBaseSeconds", a.cooldownBaseSeconds, nullptr);
    audio.setProperty("cooldownByLevelSeconds", a.cooldownByLevelSeconds, nullptr);
    audio.setProperty("scoreEnergyFluxWeight", a.scoreEnergyFluxWeight, nullptr);
    audio.setProperty("scoreHighFluxWeight", a.scoreHighFluxWeight, nullptr);
    audio.setProperty("scoreContrastWeight", a.scoreContrastWeight, nullptr);
    audio.setProperty("scoreAdaptTauSeconds", a.scoreAdaptTauSeconds, nullptr);
    audio.setProperty("scoreHighThresholdK", a.scoreHighThresholdK, nullptr);
    audio.setProperty("scoreLowThresholdK", a.scoreLowThresholdK, nullptr);
    audio.setProperty("forcedRearmSeconds", a.forcedRearmSeconds, nullptr);
    audio.setProperty("burstIoiBlend", a.burstIoiBlend, nullptr);
    audio.setProperty("burstDecayPerSecond", a.burstDecayPerSecond, nullptr);
    audio.setProperty("burstThresholdDropScale", a.burstThresholdDropScale, nullptr);
    audio.setProperty("burstCooldownReduction", a.burstCooldownReduction, nullptr);
    audio.setProperty("burstMinCooldownScale", a.burstMinCooldownScale, nullptr);
    audio.setProperty("transientHighpassHz", a.transientHighpassHz, nullptr);
    audio.setProperty("sustainedRelaxStartSeconds", a.sustainedRelaxStartSeconds, nullptr);
    audio.setProperty("sustainedRelaxFullSeconds", a.sustainedRelaxFullSeconds, nullptr);
    audio.setProperty("sustainedThresholdDropK", a.sustainedThresholdDropK, nullptr);
    audio.setProperty("sustainedCooldownReduction", a.sustainedCooldownReduction, nullptr);
    audio.setProperty("sustainedGateDbOffset", a.sustainedGateDbOffset, nullptr);
    audio.setProperty("fastTauSeconds", a.fastTauSeconds, nullptr);
    audio.setProperty("slowTauSeconds", a.slowTauSeconds, nullptr);
    audio.setProperty("lowBandCutoffHz", a.lowBandCutoffHz, nullptr);
    audio.setProperty("midBandCutoffHz", a.midBandCutoffHz, nullptr);
    audio.setProperty("onsetLockBaseSeconds", a.onsetLockBaseSeconds, nullptr);
    audio.setProperty("onsetLockByLevelSeconds", a.onsetLockByLevelSeconds, nullptr);
    audio.setProperty("onsetLockBandVoteMix", a.onsetLockBandVoteMix, nullptr);
    audio.setProperty("bandMaskTauSeconds", a.bandMaskTauSeconds, nullptr);
    audio.setProperty("bandMaskStrength", a.bandMaskStrength, nullptr);
    audio.setProperty("bandVoteLowWeight", a.bandVoteLowWeight, nullptr);
    audio.setProperty("bandVoteMidWeight", a.bandVoteMidWeight, nullptr);
    audio.setProperty("bandVoteHighWeight", a.bandVoteHighWeight, nullptr);
    audio.setProperty("streamMatchDistance", a.streamMatchDistance, nullptr);
    audio.setProperty("streamUpdateBlend", a.streamUpdateBlend, nullptr);
    audio.setProperty("streamHoldSeconds", a.streamHoldSeconds, nullptr);
    audio.setProperty("gunshotVeryLoudDb", a.gunshotVeryLoudDb, nullptr);
    audio.setProperty("gunshotLoudDb", a.gunshotLoudDb, nullptr);
    audio.setProperty("gunshotBrightMin", a.gunshotBrightMin, nullptr);
    audio.setProperty("gunshotVeryBrightMin", a.gunshotVeryBrightMin, nullptr);
    audio.setProperty("gunshotOnsetMin", a.gunshotOnsetMin, nullptr);
    audio.setProperty("reloadDbMin", a.reloadDbMin, nullptr);
    audio.setProperty("reloadHighRatioMin", a.reloadHighRatioMin, nullptr);
    root.appendChild(audio, nullptr);

    return root;
}

/**
 * @brief 从设置树应用设置
 * @param tree 包含设置的 ValueTree 对象
 * 
 * 功能：
 * 1. 读取 "hideSideMeters" 属性
 * 2. 读取 "Visual" 节点的所有视觉参数
 * 3. 读取 "Audio" 节点的所有音频参数
 * 4. 应用参数到当前状态
 */
void MainComponent::applySettingsTree(const juce::ValueTree& tree)
{
    if (!tree.isValid())
        return;

    hideSideMeters = (bool)tree.getProperty("hideSideMeters", hideSideMeters);

    auto visual = VisualTuning {};
    auto audio = AudioCaptureService::Tuning {};

    const auto visualNode = tree.getChildWithName(visualNodeId);
    if (visualNode.isValid())
    {
        visual.balanceDeadzone = (float)visualNode.getProperty("balanceDeadzone", visual.balanceDeadzone);
        visual.snapNearZeroEpsilon = (float)visualNode.getProperty("snapNearZeroEpsilon", visual.snapNearZeroEpsilon);
        visual.centerBiasLearningRate = (float)visualNode.getProperty("centerBiasLearningRate", visual.centerBiasLearningRate);
        visual.centerBiasMonoGate = (float)visualNode.getProperty("centerBiasMonoGate", visual.centerBiasMonoGate);
        visual.levelGateLow = (float)visualNode.getProperty("levelGateLow", visual.levelGateLow);
        visual.levelGateHigh = (float)visualNode.getProperty("levelGateHigh", visual.levelGateHigh);
        visual.positionWeightMin = (float)visualNode.getProperty("positionWeightMin", visual.positionWeightMin);
        visual.dotAlphaMin = (float)visualNode.getProperty("dotAlphaMin", visual.dotAlphaMin);
        visual.dotAlphaPower = (float)visualNode.getProperty("dotAlphaPower", visual.dotAlphaPower);
        visual.mainColourMapStart = (float)visualNode.getProperty("mainColourMapStart", visual.mainColourMapStart);
        visual.mainColourMapSpan = (float)visualNode.getProperty("mainColourMapSpan", visual.mainColourMapSpan);
        visual.mainColourMapExponent = (float)visualNode.getProperty("mainColourMapExponent", visual.mainColourMapExponent);
        visual.historyRadiusMin = (float)visualNode.getProperty("historyRadiusMin", visual.historyRadiusMin);
        visual.historyRadiusMax = (float)visualNode.getProperty("historyRadiusMax", visual.historyRadiusMax);
        visual.historyFadeExponent = (float)visualNode.getProperty("historyFadeExponent", visual.historyFadeExponent);
        visual.historyLifeMinSeconds = (float)visualNode.getProperty("historyLifeMinSeconds", visual.historyLifeMinSeconds);
        visual.historyLifeMaxSeconds = (float)visualNode.getProperty("historyLifeMaxSeconds", visual.historyLifeMaxSeconds);
    }

    const auto audioNode = tree.getChildWithName(audioNodeId);
    if (audioNode.isValid())
    {
        audio.loudEnoughDb = (float)audioNode.getProperty("loudEnoughDb", audio.loudEnoughDb);
        audio.onsetContrastStart = (float)audioNode.getProperty("onsetContrastStart", audio.onsetContrastStart);
        audio.onsetRatioStart = (float)audioNode.getProperty("onsetRatioStart", audio.onsetRatioStart);
        audio.onsetContrastSettle = (float)audioNode.getProperty("onsetContrastSettle", audio.onsetContrastSettle);
        audio.onsetRatioSettle = (float)audioNode.getProperty("onsetRatioSettle", audio.onsetRatioSettle);
        audio.cooldownBaseSeconds = (float)audioNode.getProperty("cooldownBaseSeconds", audio.cooldownBaseSeconds);
        audio.cooldownByLevelSeconds = (float)audioNode.getProperty("cooldownByLevelSeconds", audio.cooldownByLevelSeconds);
        audio.scoreEnergyFluxWeight = (float)audioNode.getProperty("scoreEnergyFluxWeight", audio.scoreEnergyFluxWeight);
        audio.scoreHighFluxWeight = (float)audioNode.getProperty("scoreHighFluxWeight", audio.scoreHighFluxWeight);
        audio.scoreContrastWeight = (float)audioNode.getProperty("scoreContrastWeight", audio.scoreContrastWeight);
        audio.scoreAdaptTauSeconds = (float)audioNode.getProperty("scoreAdaptTauSeconds", audio.scoreAdaptTauSeconds);
        audio.scoreHighThresholdK = (float)audioNode.getProperty("scoreHighThresholdK", audio.scoreHighThresholdK);
        audio.scoreLowThresholdK = (float)audioNode.getProperty("scoreLowThresholdK", audio.scoreLowThresholdK);
        audio.forcedRearmSeconds = (float)audioNode.getProperty("forcedRearmSeconds", audio.forcedRearmSeconds);
        audio.burstIoiBlend = (float)audioNode.getProperty("burstIoiBlend", audio.burstIoiBlend);
        audio.burstDecayPerSecond = (float)audioNode.getProperty("burstDecayPerSecond", audio.burstDecayPerSecond);
        audio.burstThresholdDropScale = (float)audioNode.getProperty("burstThresholdDropScale", audio.burstThresholdDropScale);
        audio.burstCooldownReduction = (float)audioNode.getProperty("burstCooldownReduction", audio.burstCooldownReduction);
        audio.burstMinCooldownScale = (float)audioNode.getProperty("burstMinCooldownScale", audio.burstMinCooldownScale);
        audio.transientHighpassHz = (float)audioNode.getProperty("transientHighpassHz", audio.transientHighpassHz);
        audio.sustainedRelaxStartSeconds = (float)audioNode.getProperty("sustainedRelaxStartSeconds", audio.sustainedRelaxStartSeconds);
        audio.sustainedRelaxFullSeconds = (float)audioNode.getProperty("sustainedRelaxFullSeconds", audio.sustainedRelaxFullSeconds);
        audio.sustainedThresholdDropK = (float)audioNode.getProperty("sustainedThresholdDropK", audio.sustainedThresholdDropK);
        audio.sustainedCooldownReduction = (float)audioNode.getProperty("sustainedCooldownReduction", audio.sustainedCooldownReduction);
        audio.sustainedGateDbOffset = (float)audioNode.getProperty("sustainedGateDbOffset", audio.sustainedGateDbOffset);
        audio.fastTauSeconds = (float)audioNode.getProperty("fastTauSeconds", audio.fastTauSeconds);
        audio.slowTauSeconds = (float)audioNode.getProperty("slowTauSeconds", audio.slowTauSeconds);
        audio.lowBandCutoffHz = (float)audioNode.getProperty("lowBandCutoffHz", audio.lowBandCutoffHz);
        audio.midBandCutoffHz = (float)audioNode.getProperty("midBandCutoffHz", audio.midBandCutoffHz);
        audio.onsetLockBaseSeconds = (float)audioNode.getProperty("onsetLockBaseSeconds", audio.onsetLockBaseSeconds);
        audio.onsetLockByLevelSeconds = (float)audioNode.getProperty("onsetLockByLevelSeconds", audio.onsetLockByLevelSeconds);
        audio.onsetLockBandVoteMix = (float)audioNode.getProperty("onsetLockBandVoteMix", audio.onsetLockBandVoteMix);
        audio.bandMaskTauSeconds = (float)audioNode.getProperty("bandMaskTauSeconds", audio.bandMaskTauSeconds);
        audio.bandMaskStrength = (float)audioNode.getProperty("bandMaskStrength", audio.bandMaskStrength);
        audio.bandVoteLowWeight = (float)audioNode.getProperty("bandVoteLowWeight", audio.bandVoteLowWeight);
        audio.bandVoteMidWeight = (float)audioNode.getProperty("bandVoteMidWeight", audio.bandVoteMidWeight);
        audio.bandVoteHighWeight = (float)audioNode.getProperty("bandVoteHighWeight", audio.bandVoteHighWeight);
        audio.streamMatchDistance = (float)audioNode.getProperty("streamMatchDistance", audio.streamMatchDistance);
        audio.streamUpdateBlend = (float)audioNode.getProperty("streamUpdateBlend", audio.streamUpdateBlend);
        audio.streamHoldSeconds = (float)audioNode.getProperty("streamHoldSeconds", audio.streamHoldSeconds);
        audio.gunshotVeryLoudDb = (float)audioNode.getProperty("gunshotVeryLoudDb", audio.gunshotVeryLoudDb);
        audio.gunshotLoudDb = (float)audioNode.getProperty("gunshotLoudDb", audio.gunshotLoudDb);
        audio.gunshotBrightMin = (float)audioNode.getProperty("gunshotBrightMin", audio.gunshotBrightMin);
        audio.gunshotVeryBrightMin = (float)audioNode.getProperty("gunshotVeryBrightMin", audio.gunshotVeryBrightMin);
        audio.gunshotOnsetMin = (float)audioNode.getProperty("gunshotOnsetMin", audio.gunshotOnsetMin);
        audio.reloadDbMin = (float)audioNode.getProperty("reloadDbMin", audio.reloadDbMin);
        audio.reloadHighRatioMin = (float)audioNode.getProperty("reloadHighRatioMin", audio.reloadHighRatioMin);
    }

    setVisualTuning(visual);
    audioCapture.setTuning(audio);
    settingsDirty = false;
}

/**
 * @brief 从文件加载设置
 * 
 * 功能：
 * 1. 获取设置文件路径
 * 2. 如果文件存在，解析 XML
 * 3. 验证文件格式
 * 4. 调用 applySettingsTree() 应用设置
 */
void MainComponent::loadSettings()
{
    const auto file = getSettingsFile();
    if (!file.existsAsFile())
        return;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid() || tree.getType().toString() != settingsRootId)
        return;

    applySettingsTree(tree);
}

/**
 * @brief 保存设置到文件
 * 
 * 功能：
 * 1. 获取设置文件路径
 * 2. 确保目录存在
 * 3. 调用 createSettingsTree() 创建设置树
 * 4. 将设置树写入 XML 文件
 * 5. 标记 settingsDirty = false
 * 6. 更新 lastSettingsSaveTimeMs
 */
void MainComponent::saveSettings()
{
    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();

    const auto tree = createSettingsTree();
    if (auto xml = tree.createXml())
    {
        xml->writeTo(file, {});
        settingsDirty = false;
        lastSettingsSaveTimeMs = juce::Time::getMillisecondCounterHiRes();
    }
}