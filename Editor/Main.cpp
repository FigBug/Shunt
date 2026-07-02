#include <JuceHeader.h>
#include "EditorComponent.h"

class ShuntEditorApplication : public juce::JUCEApplication
{
public:
    ShuntEditorApplication() = default;

    const juce::String getApplicationName() override    { return "Shunt Level Editor"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        auto args = juce::JUCEApplication::getCommandLineParameterArray();

        // Headless batch convert: --convert <in.json> <out.json>
        // Loads a level and re-serialises it — handy for scripting / CI and for
        // verifying round-trips.
        if (args.size() == 3 && args[0] == "--convert")
        {
            LevelDocument doc;
            if (doc.loadFromString (juce::File (args[1]).loadFileAsString()))
                juce::File (args[2]).replaceWithText (doc.toJsonString());
            setApplicationReturnValue (0);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // Open a level passed on the command line (or via "open with").
        for (const auto& a : args)
        {
            juce::File f (a);
            if (f.existsAsFile() && f.hasFileExtension ("json"))
            {
                if (auto* ec = dynamic_cast<EditorComponent*> (mainWindow->getContentComponent()))
                    ec->loadFile (f);
                break;
            }
        }

        juce::ignoreUnused (commandLine);
    }

    void shutdown() override               { mainWindow = nullptr; }
    void systemRequestedQuit() override    { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new EditorComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ShuntEditorApplication)
