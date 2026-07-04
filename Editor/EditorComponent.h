#pragma once

#include <JuceHeader.h>
#include "LevelDocument.h"
#include "EditorCanvas.h"

// ============================================================================
// EditorComponent — top-level: toolbar + canvas + status bar, plus file I/O.
// ============================================================================

class EditorComponent : public juce::Component
{
public:
    EditorComponent();
    ~EditorComponent() override;

    bool loadFile (const juce::File&);   // load a level (used for CLI / drag-open)
    bool openLastFile();                 // reopen the last-edited level, if any

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void selectTool (EditorCanvas::Tool);
    void refreshToolButtons();

    void newLevel();
    void openLevel();
    void saveLevel (bool forceChooser);
    void weldLevel();
    void refreshCarsField();
    void rememberFile (const juce::File&);   // persist as the last-edited level

    LevelDocument doc;
    EditorCanvas  canvas { doc };

    juce::OwnedArray<juce::TextButton> toolButtons;   // parallels the Tool enum
    juce::OwnedArray<juce::TextButton> colourButtons; // drop-off / spawn colour 0..3
    juce::TextButton newButton  { "New" };
    juce::TextButton openButton { "Open" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton saveAsButton { "Save As" };
    juce::TextButton frameButton { "Frame (F)" };
    juce::TextButton weldButton { "Weld" };
    juce::Label carsCaption { {}, "Cars:" };
    juce::Label carsField;              // editable total-car count

    juce::Label statusBar;

    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::PropertiesFile> settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorComponent)
};
