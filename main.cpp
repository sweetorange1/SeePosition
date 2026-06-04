#include <JuceHeader.h>
#include "Source/MainComponent.h"

namespace
{
juce::String pickSystemUIFont()
{
    const auto installed = juce::Font::findAllTypefaceNames();

    static constexpr const char* candidates[] = {
        "Microsoft YaHei UI",
        "Microsoft YaHei",
        "SimHei",
        "Noto Sans CJK SC",
        "PingFang SC"
    };

    for (const auto* name : candidates)
    {
        if (installed.contains(name))
            return name;
    }

    return {};
}
}

class SeePositionApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "SeePosition"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        const auto fontName = pickSystemUIFont();
        if (fontName.isNotEmpty())
            juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName(fontName);

        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name,
                                   juce::Colours::transparentBlack,
                                   juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0);
            setResizable(false, false);
            setAlwaysOnTop(true);
            setDropShadowEnabled(false);

            constexpr int windowWidth = 760;
            constexpr int windowHeight = 90;

            setContentOwned(new MainComponent(), true);
            setSize(windowWidth, windowHeight);

            const auto& displays = juce::Desktop::getInstance().getDisplays();
            const auto* display = displays.getPrimaryDisplay();
            const auto userArea = display != nullptr ? display->userArea : juce::Rectangle<int>(0, 0, 1280, 720);

            const int centerX = userArea.getCentreX();
            const int quarterY = userArea.getY() + userArea.getHeight() / 4;
            const int x = centerX - windowWidth / 2;
            const int y = quarterY - windowHeight / 2;

            setTopLeftPosition(x, y);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(SeePositionApplication)
