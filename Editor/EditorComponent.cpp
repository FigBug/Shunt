#include "EditorComponent.h"
#include "../Source/game/MapsDir.h"

namespace
{
    struct ToolDef { EditorCanvas::Tool tool; const char* name; };
    const ToolDef kTools[] = {
        { EditorCanvas::Tool::select,   "Select (V)"   },
        { EditorCanvas::Tool::straight, "Straight (L)" },
        { EditorCanvas::Tool::curve,    "Curve (C)"    },
        { EditorCanvas::Tool::rect,     "Rect (R)"     },
        { EditorCanvas::Tool::spawn,    "Spawn (S)"    },
        { EditorCanvas::Tool::dropOff,  "Drop-off (D)" },
        { EditorCanvas::Tool::erase,    "Erase (E)"    },
    };
}

EditorComponent::EditorComponent()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "ShuntEditor";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);

    addAndMakeVisible (canvas);

    for (const auto& td : kTools)
    {
        auto* b = new juce::TextButton (td.name);
        b->setClickingTogglesState (false);
        b->setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        auto tool = td.tool;
        b->onClick = [this, tool] { selectTool (tool); };
        addAndMakeVisible (b);
        toolButtons.add (b);
    }

    for (int i = 0; i < 4; ++i)
    {
        auto* b = new juce::TextButton (juce::String (i));
        b->setColour (juce::TextButton::buttonColourId, LevelDocument::slotColour (i));
        b->onClick = [this, i] { canvas.setDropColour (i); refreshToolButtons(); };
        addAndMakeVisible (b);
        colourButtons.add (b);
    }

    newButton.onClick    = [this] { newLevel(); };
    openButton.onClick   = [this] { openLevel(); };
    saveButton.onClick   = [this] { saveLevel (false); };
    saveAsButton.onClick = [this] { saveLevel (true); };
    frameButton.onClick  = [this] { canvas.frameAll(); };
    weldButton.onClick   = [this] { weldLevel(); };
    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton, &frameButton, &weldButton })
        addAndMakeVisible (b);

    carsCaption.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    carsCaption.setJustificationType (juce::Justification::centredRight);
    carsCaption.setTooltip ("Total cars, spread randomly across the spawn points");
    addAndMakeVisible (carsCaption);

    carsField.setEditable (true);
    carsField.setColour (juce::Label::backgroundColourId, juce::Colour::fromRGB (55, 58, 64));
    carsField.setColour (juce::Label::textColourId, juce::Colours::white);
    carsField.setJustificationType (juce::Justification::centred);
    carsField.setTooltip ("Total cars, spread randomly across the spawn points");
    carsField.onTextChange = [this]
    {
        doc.totalCars = juce::jmax (0, carsField.getText().getIntValue());
        refreshCarsField();
    };
    addAndMakeVisible (carsField);
    refreshCarsField();

    statusBar.setColour (juce::Label::backgroundColourId, juce::Colour::fromRGB (24, 26, 30));
    statusBar.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    statusBar.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (statusBar);

    canvas.onStatusChanged = [this] { statusBar.setText (canvas.statusText, juce::dontSendNotification); };
    canvas.onToolChanged   = [this] { refreshToolButtons(); };

    selectTool (EditorCanvas::Tool::select);
    setSize (1400, 800);
}

EditorComponent::~EditorComponent() = default;

void EditorComponent::selectTool (EditorCanvas::Tool t)
{
    canvas.setTool (t);
    refreshToolButtons();

    if (isShowing() || isOnDesktop())
        canvas.grabKeyboardFocus();
}

void EditorComponent::refreshToolButtons()
{
    auto active = canvas.getTool();
    for (int i = 0; i < toolButtons.size(); ++i)
    {
        bool on = (kTools[i].tool == active);
        toolButtons[i]->setColour (juce::TextButton::buttonColourId,
            on ? juce::Colour::fromRGB (70, 120, 200) : juce::Colour::fromRGB (55, 58, 64));
    }
    for (int i = 0; i < colourButtons.size(); ++i)
    {
        bool on = (i == canvas.getDropColour());
        colourButtons[i]->setColour (juce::TextButton::textColourOffId,
            on ? juce::Colours::black : juce::Colours::black.withAlpha (0.35f));
        colourButtons[i]->setButtonText (on ? "*" + juce::String (i) : juce::String (i));
    }
}

// ---- file I/O --------------------------------------------------------------

void EditorComponent::newLevel()
{
    doc.clear();
    currentFile = juce::File();
    refreshCarsField();
    canvas.frameAll();
    canvas.repaint();
}

bool EditorComponent::loadFile (const juce::File& file)
{
    if (! file.existsAsFile() || ! doc.loadFromString (file.loadFileAsString()))
        return false;
    currentFile = file;
    rememberFile (file);
    refreshCarsField();
    canvas.frameAll();
    canvas.repaint();
    return true;
}

void EditorComponent::rememberFile (const juce::File& file)
{
    if (settings != nullptr && file != juce::File())
    {
        settings->setValue ("lastFile", file.getFullPathName());
        settings->saveIfNeeded();
    }
}

bool EditorComponent::openLastFile()
{
    if (settings == nullptr) return false;
    juce::File f (settings->getValue ("lastFile"));
    return f.existsAsFile() && loadFile (f);
}

void EditorComponent::openLevel()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a Shunt level",
                    currentFile != juce::File() ? currentFile : game::mapsDirectory(), "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File()) return;
        if (doc.loadFromString (file.loadFileAsString()))
        {
            currentFile = file;
            rememberFile (file);
            refreshCarsField();
            canvas.frameAll();
            canvas.repaint();
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                "Open failed", "Could not parse " + file.getFileName());
        }
    });
}

void EditorComponent::saveLevel (bool forceChooser)
{
    auto write = [this] (juce::File file)
    {
        if (! file.hasFileExtension ("json"))
            file = file.withFileExtension ("json");
        if (file.replaceWithText (doc.toJsonString()))
        {
            currentFile = file;
            rememberFile (file);
        }
    };

    if (! forceChooser && currentFile != juce::File())
    {
        write (currentFile);
        return;
    }

    chooser = std::make_unique<juce::FileChooser> ("Save Shunt level",
                    currentFile != juce::File() ? currentFile
                                                : game::mapsDirectory().getChildFile ("level.json"),
                    "*.json");
    auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
               | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync (flags, [write] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File())
            write (file);
    });
}

void EditorComponent::refreshCarsField()
{
    carsField.setText (juce::String (doc.totalCars), juce::dontSendNotification);
}

void EditorComponent::weldLevel()
{
    int n = canvas.weldLooseEnds();
    statusBar.setText (n > 0 ? "Welded " + juce::String (n) + " loose connection(s)"
                             : "Nothing to weld — no loose ends found",
                       juce::dontSendNotification);
    canvas.grabKeyboardFocus();
}

// ---- layout ----------------------------------------------------------------

void EditorComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (40, 43, 48));
}

void EditorComponent::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (40).reduced (4);
    auto place = [&top] (juce::Button& b, int w) { b.setBounds (top.removeFromLeft (w).reduced (1)); };

    for (auto* b : { &newButton, &openButton, &saveButton, &saveAsButton })
        place (*b, 66);
    top.removeFromLeft (12);
    for (auto* b : toolButtons)
        place (*b, 92);
    top.removeFromLeft (12);
    for (auto* b : colourButtons)
        place (*b, 34);
    top.removeFromLeft (12);
    place (frameButton, 90);
    place (weldButton, 60);
    top.removeFromLeft (12);
    carsCaption.setBounds (top.removeFromLeft (44));
    carsField.setBounds (top.removeFromLeft (46).reduced (1));

    statusBar.setBounds (area.removeFromBottom (24));
    canvas.setBounds (area);
}
