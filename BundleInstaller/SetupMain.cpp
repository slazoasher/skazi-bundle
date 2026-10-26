// SKAZI Bundle Setup: one .exe that installs every SKAZI plugin.
// The plugins ride on the end of the exe as a zip (see build_bundle.ps1):
//   [setup.exe][payload.zip][zip size: int64][magic "SKZBNDL1"]
// The zip holds VST3/<Name>.vst3/... and Apps/<Name>.exe.
// Installs to Common Files\VST3\SKAZI and Program Files\SKAZI, removes older single-plugin
// installs of the same plugins, registers with Add/Remove Programs and doubles as the
// uninstaller ("/uninstall"). "/silent" runs without a window.
#include <juce_gui_extra/juce_gui_extra.h>
#include "MetalLookAndFeel.h"
#if SHAK_HAS_LOGO
 #include "BinaryData.h"
#endif

using namespace juce;

namespace
{
    const String kVersion = "1.1";
    const String kUninstallKey = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SKAZI Bundle\\";
    const char kMagic[8] = { 'S', 'K', 'Z', 'B', 'N', 'D', 'L', '1' };

    File programFiles() { return File::getSpecialLocation (File::globalApplicationsDirectory); }
    File vst3Root()     { return File (SystemStats::getEnvironmentVariable ("CommonProgramFiles", "C:\\Program Files\\Common Files")).getChildFile ("VST3"); }
    File vst3Dir()      { return vst3Root().getChildFile ("SKAZI"); }
    File appDir()       { return programFiles().getChildFile ("SKAZI"); }
    File startMenu()    { return File (SystemStats::getEnvironmentVariable ("ProgramData", "C:\\ProgramData")).getChildFile ("Microsoft\\Windows\\Start Menu\\Programs"); }
    File startDir()     { return startMenu().getChildFile ("SKAZI"); }
    File desktop()      { return File::getSpecialLocation (File::userDesktopDirectory); }
    File uninstaller()  { return appDir().getChildFile ("Uninstall SKAZI Bundle.exe"); }
    File installedList(){ return appDir().getChildFile ("installed.txt"); }

    String describe (const String& name)
    {
        if (name == "SKAZI")          return "Synth";
        if (name == "SHAKSUKA")       return "Random chop & glitch FX";
        if (name == "PITA PUMP")      return "Sidechain LFO tool";
        if (name == "The FilterFish") return "Filter FX";
        if (name == "SUBICH")         return "Bass + sub bass synth";
        if (name == "BAMBA")          return "Riser & FX maker";
        if (name == "TAHINI")         return "Space delay + reverb";
        if (name == "ZHUG")           return "Saturator + widener";
        if (name == "BULLY")          return "Wavetable synth: warps, mod matrix, FX";
        if (name == "KICKASS")        return "Kick drum synth + Splice kick browser";
        if (name == "SALAT")          return "Glitch multi-FX + MAKE A SALAT randomizer";
        if (name == "SKAZI 303")      return "TBON 303 acid bass line synth + sequencer";
        if (name == "MIDI GOD")       return "MIDI pattern generator + audio to MIDI";
        if (name == "KUBE")           return "Psytrance wavetable synth";
        return "Plugin";
    }

    // The zip appended to this exe
    struct Payload
    {
        Payload()
        {
            const auto self = File::getSpecialLocation (File::currentExecutableFile);
            FileInputStream in (self);
            const auto size = in.getTotalLength();
            if (! in.openedOk() || size < 16) return;
            char tail[16];
            in.setPosition (size - 16);
            in.read (tail, 16);
            if (std::memcmp (tail + 8, kMagic, 8) != 0) return;
            int64 zipLen = 0;
            std::memcpy (&zipLen, tail, 8);
            if (zipLen <= 0 || zipLen > size - 16) return;
            exeLen = size - 16 - zipLen;
            zip = std::make_unique<ZipFile> (new SubregionStream (new FileInputStream (self), exeLen, zipLen, true), true);

            for (int i = 0; i < zip->getNumEntries(); ++i)
            {
                const auto n = entryName (i);
                if (n.startsWith ("VST3/") && n.contains (".vst3/"))
                    products.addIfNotAlreadyThere (n.fromFirstOccurrenceOf ("VST3/", false, false).upToFirstOccurrenceOf (".vst3/", false, false));
            }
        }

        String entryName (int i) const { return zip->getEntry (i)->filename.replaceCharacter ('\\', '/'); }
        bool hasApp (const String& product) const
        {
            for (int i = 0; i < zip->getNumEntries(); ++i)
                if (entryName (i) == "Apps/" + product + ".exe") return true;
            return false;
        }

        std::unique_ptr<ZipFile> zip;
        StringArray products;
        int64 exeLen = 0;
    };

    bool extract (ZipFile& zip, int index, const File& dest)
    {
        dest.getParentDirectory().createDirectory();
        dest.deleteFile();
        std::unique_ptr<InputStream> in (zip.createStreamForEntry (index));
        FileOutputStream out (dest);
        if (in == nullptr || ! out.openedOk()) return false;
        out.writeFromInputStream (*in, -1);
        out.flush();
        return out.getStatus().wasOk();
    }

    void deleteIfEmpty (const File& dir)
    {
        if (dir.isDirectory() && dir.findChildFiles (File::findFilesAndDirectories, false).isEmpty())
            dir.deleteRecursively();
    }

    // Earlier single-plugin installs of the same plugins, so nothing shows up twice in the DAW
    void removeLegacy (const StringArray& names)
    {
        for (auto& n : names)
            vst3Root().getChildFile (n + ".vst3").deleteRecursively();

        if (names.contains ("SKAZI"))
        {
            // SKAZI 1.2 came with an Inno Setup installer; its uninstaller relaunches itself, so wait for the key to go
            const String inno = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{3F9B7C21-5D4E-4A8B-B1C6-5CA21F0E0001}_is1";
            const auto cmd = WindowsRegistry::getValue (inno + "\\UninstallString");
            if (cmd.isNotEmpty())
            {
                ChildProcess p;
                if (p.start (cmd + " /VERYSILENT /SUPPRESSMSGBOXES /NORESTART", 0))
                {
                    p.waitForProcessToFinish (60000);
                    for (int i = 0; i < 600 && WindowsRegistry::keyExists (inno); ++i)
                        Thread::sleep (100);
                }
            }
            startMenu().getChildFile ("SKAZI\\SKAZI.lnk").deleteFile();
            File ("C:\\Users\\Public\\Desktop\\SKAZI.lnk").deleteFile();
        }
        if (names.contains ("SHAKSUKA"))
        {
            WindowsRegistry::deleteKey ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SHAKSUKA");
            programFiles().getChildFile ("SHAKSUKA").deleteRecursively();
            startMenu().getChildFile ("SHAKSUKA.lnk").deleteFile();
            desktop().getChildFile ("SHAKSUKA.lnk").deleteFile();
        }
        if (names.contains ("The FilterFish"))
        {
            WindowsRegistry::deleteKey ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TheFilterFish");
            programFiles().getChildFile ("Asher Swissa\\The FilterFish").deleteRecursively();
            deleteIfEmpty (programFiles().getChildFile ("Asher Swissa"));
            startMenu().getChildFile ("The FilterFish.lnk").deleteFile();
        }
        if (names.contains ("KICKASS"))
        {
            // Early KICKASS builds had their own Inno Setup installer and went straight into VST3\.
            const String inno = "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{A19DAE55-F5FD-4FA7-8E34-A7A0AF4477AD}_is1";
            const auto cmd = WindowsRegistry::getValue (inno + "\\UninstallString");
            if (cmd.isNotEmpty())
            {
                ChildProcess p;
                if (p.start (cmd + " /VERYSILENT /SUPPRESSMSGBOXES /NORESTART", 0))
                {
                    p.waitForProcessToFinish (60000);
                    for (int i = 0; i < 600 && WindowsRegistry::keyExists (inno); ++i)
                        Thread::sleep (100);
                }
            }
            vst3Root().getChildFile ("KICKASS.vst3").deleteRecursively();
            programFiles().getChildFile ("SKAZI\\KICKASS").deleteRecursively();
            programFiles().getChildFile ("Asher Swissa\\KICKASS").deleteRecursively();
            deleteIfEmpty (programFiles().getChildFile ("Asher Swissa"));
            startMenu().getChildFile ("KICKASS\\KICKASS.lnk").deleteFile();
            startMenu().getChildFile ("KICKASS\\Uninstall KICKASS.lnk").deleteFile();
            deleteIfEmpty (startMenu().getChildFile ("KICKASS"));
        }
    }
}

class SetupComponent : public Component
{
public:
    explicit SetupComponent (bool startUninstall)
    {
        setLookAndFeel (&lnf);
#if SHAK_HAS_LOGO
        logo = ImageCache::getFromMemory (BinaryData::skazi_logo_png, BinaryData::skazi_logo_pngSize);
#endif
        for (int i = 0; i < payload.products.size(); ++i)
        {
            auto* t = toggles.add (new ToggleButton());
            t->setColour (ToggleButton::tickColourId, skin::red);
            t->setToggleState (true, dontSendNotification);
            addAndMakeVisible (t);
        }
        for (auto* t : { &optApps, &optDesktop })
        {
            t->setColour (ToggleButton::tickColourId, skin::red);
            addAndMakeVisible (t);
        }
        optApps.setToggleState (true, dontSendNotification);

        installBtn.setToggleState (true, dontSendNotification); // drawn lit red
        installBtn.onClick = [this]
        {
            setStatus ("Installing...", skin::textDim);
            MessageManager::callAsync ([this] { install(); });
        };
        uninstallBtn.onClick = [this] { uninstall(); };
        closeBtn.onClick = [] { JUCEApplication::quit(); };
        for (auto* b : { &installBtn, &uninstallBtn, &closeBtn })
            addAndMakeVisible (b);

        setSize (660, 140 + 44 * jmax (1, payload.products.size()) + 210);
        refreshState();
        if (startUninstall)
            MessageManager::callAsync ([this] { uninstall(); });
    }

    ~SetupComponent() override { setLookAndFeel (nullptr); }

    void paint (Graphics& g) override
    {
        g.drawImageAt (faceplate, 0, 0);
        skin::drawLogoBadge (g, { 30.0f, 24.0f, 150.0f, 46.0f }, logo);
        const auto tf = skin::titleFont (34.0f);
        const float w1 = GlyphArrangement::getStringWidth (tf, "SKAZI");
        skin::engrave (g, "SKAZI", { 196, 20, (int) w1 + 4, 38 }, tf, Colour (0xff26282c), Justification::left);
        skin::engrave (g, "BUNDLE", { 202 + (int) w1, 20, 160, 38 }, tf, skin::red, Justification::left);
        skin::engrave (g, String::fromUTF8 ("SETUP  \xc2\xb7  VERSION ") + kVersion + String::fromUTF8 ("  \xc2\xb7  BY ASHER SWISSA"),
                       { 197, 56, 360, 14 }, skin::font (10.5f), Colour (0xff3b3e43), Justification::left);

        skin::drawInsetPanel (g, panel.toFloat());
        g.setColour (skin::white);
        g.setFont (skin::font (13.0f));
        g.drawText ("PLUGINS", panel.getX() + 16, panel.getY() + 12, 200, 20, Justification::left);
        g.setColour (skin::textDim);
        g.setFont (skin::font (11.0f, false));
        g.drawText ("Installed to " + vst3Dir().getFullPathName() + "  (shows as SKAZI in your DAW)",
                    panel.getX() + 90, panel.getY() + 14, panel.getWidth() - 100, 16, Justification::left);

        for (int i = 0; i < toggles.size(); ++i)
        {
            const auto& name = payload.products[i];
            auto* t = toggles[i];
            g.setColour (skin::white);
            g.setFont (skin::font (14.0f));
            g.drawText (name, t->getRight() + 10, t->getY() - 3, 300, 18, Justification::left);
            g.setColour (skin::textDim);
            g.setFont (skin::font (11.0f, false));
            g.drawText (describe (name) + (payload.hasApp (name) ? String::fromUTF8 ("  \xc2\xb7  VST3 + standalone app") : "  \xc2\xb7  VST3"),
                        t->getRight() + 10, t->getY() + 15, 400, 16, Justification::left);
        }

        g.setColour (skin::line);
        g.drawHorizontalLine (optApps.getY() - 12, (float) panel.getX() + 16, (float) panel.getRight() - 16);
        g.setColour (skin::white);
        g.setFont (skin::font (12.5f));
        g.drawText ("Standalone apps + Start Menu folder", optApps.getRight() + 10, optApps.getY() + 1, 400, 18, Justification::left);
        g.drawText ("Desktop shortcuts", optDesktop.getRight() + 10, optDesktop.getY() + 1, 400, 18, Justification::left);

        g.setFont (skin::font (12.0f));
        g.setColour (statusColour);
        g.drawFittedText (status, panel.getX() + 16, panel.getBottom() - 44, panel.getWidth() - 32, 36, Justification::centredLeft, 2);

        Random r (5);
        for (auto pt : { Point<float> (15, 15), Point<float> ((float) getWidth() - 15, 15),
                         Point<float> (15, (float) getHeight() - 15), Point<float> ((float) getWidth() - 15, (float) getHeight() - 15) })
            skin::drawScrew (g, pt, 7.0f, r.nextFloat() * MathConstants<float>::pi);
    }

    void resized() override
    {
        faceplate = skin::renderBrushedMetal (getWidth(), getHeight());
        panel = { 30, 92, getWidth() - 60, getHeight() - 92 - 70 };
        int y = panel.getY() + 48;
        for (auto* t : toggles)
        {
            t->setBounds (panel.getX() + 20, y, 36, 20);
            y += 44;
        }
        optApps.setBounds (panel.getX() + 20, y + 14, 36, 20);
        optDesktop.setBounds (panel.getX() + 20, y + 44, 36, 20);
        const int by = getHeight() - 56;
        uninstallBtn.setBounds (30, by, 110, 34);
        closeBtn.setBounds (getWidth() - 250, by, 90, 34);
        installBtn.setBounds (getWidth() - 150, by, 120, 34);
    }

    bool install()
    {
        if (payload.zip == nullptr)
            return fail ("This setup has no plugins inside it. Build it with build_bundle.ps1.");

        StringArray chosen;
        for (int i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
                chosen.add (payload.products[i]);
        if (chosen.isEmpty())
            return fail ("Tick at least one plugin.");

        removeLegacy (chosen);

        bool ok = true;
        for (auto& n : chosen)
            vst3Dir().getChildFile (n + ".vst3").deleteRecursively();

        auto& zip = *payload.zip;
        for (int i = 0; i < zip.getNumEntries() && ok; ++i)
        {
            const auto e = payload.entryName (i);
            if (e.endsWithChar ('/')) continue;
            for (auto& n : chosen)
            {
                if (e.startsWith ("VST3/" + n + ".vst3/"))
                    ok = extract (zip, i, vst3Dir().getChildFile (e.fromFirstOccurrenceOf ("VST3/", false, false)));
                else if (optApps.getToggleState() && e == "Apps/" + n + ".exe")
                    ok = extract (zip, i, appDir().getChildFile (n + ".exe"));
            }
        }
        if (! ok)
            return fail ("Could not write the files. Close your DAW (it may be using a SKAZI plugin) and try again.");

        // shortcuts
        for (auto& n : chosen)
        {
            const auto exe = appDir().getChildFile (n + ".exe");
            if (! optApps.getToggleState() || ! exe.existsAsFile()) continue;
            startDir().createDirectory();
            exe.createShortcut (n + " by SKAZI", startDir().getChildFile (n + ".lnk"));
            if (optDesktop.getToggleState())
                exe.createShortcut (n + " by SKAZI", desktop().getChildFile (n + ".lnk"));
        }

        // remember what is installed, keep a payload-free copy of this exe as the uninstaller
        StringArray all = StringArray::fromLines (installedList().loadFileAsString());
        all.addArray (chosen);
        all.removeEmptyStrings();
        all.removeDuplicates (true);
        appDir().createDirectory();
        installedList().replaceWithText (all.joinIntoString ("\n"));

        const auto self = File::getSpecialLocation (File::currentExecutableFile);
        if (self != uninstaller())
        {
            FileInputStream in (self);
            uninstaller().deleteFile();
            FileOutputStream out (uninstaller());
            ok = in.openedOk() && out.openedOk() && out.writeFromInputStream (in, payload.exeLen > 0 ? payload.exeLen : -1) > 0;
        }
        if (! ok)
            return fail ("Could not create the uninstaller in " + appDir().getFullPathName());

        WindowsRegistry::setValue (kUninstallKey + "DisplayName", "SKAZI Bundle");
        WindowsRegistry::setValue (kUninstallKey + "DisplayVersion", kVersion);
        WindowsRegistry::setValue (kUninstallKey + "Publisher", "SKAZI");
        WindowsRegistry::setValue (kUninstallKey + "InstallLocation", appDir().getFullPathName());
        WindowsRegistry::setValue (kUninstallKey + "DisplayIcon", uninstaller().getFullPathName());
        WindowsRegistry::setValue (kUninstallKey + "UninstallString", "\"" + uninstaller().getFullPathName() + "\" /uninstall");
        WindowsRegistry::setValue (kUninstallKey + "NoModify", (uint32) 1);
        WindowsRegistry::setValue (kUninstallKey + "NoRepair", (uint32) 1);

        setStatus (String::fromUTF8 ("Installed \xe2\x9c\x93  ") + chosen.joinIntoString (", ")
                       + ". Open your DAW and rescan plugins: they are all under SKAZI.", Colour (0xff22e3a5));
        refreshState();
        return true;
    }

    bool uninstall()
    {
        StringArray names = StringArray::fromLines (installedList().loadFileAsString());
        names.addArray (payload.products);
        names.removeEmptyStrings();
        names.removeDuplicates (true);

        bool ok = true;
        for (auto& n : names)
        {
            const auto v = vst3Dir().getChildFile (n + ".vst3");
            ok = (! v.exists() || v.deleteRecursively()) && ok;
            startDir().getChildFile (n + ".lnk").deleteFile();
            desktop().getChildFile (n + ".lnk").deleteFile();
        }
        deleteIfEmpty (vst3Dir());
        deleteIfEmpty (startDir());
        WindowsRegistry::deleteKey (kUninstallKey.dropLastCharacters (1));

        const auto self = File::getSpecialLocation (File::currentExecutableFile);
        if (appDir().exists())
        {
            if (self.isAChildOf (appDir()))
            {
                for (auto& f : appDir().findChildFiles (File::findFilesAndDirectories, false))
                    if (f != self)
                        ok = f.deleteRecursively() && ok;
                selfDeleteOnExit = true;
            }
            else
            {
                ok = appDir().deleteRecursively() && ok;
            }
        }

        if (ok) setStatus (String::fromUTF8 ("SKAZI Bundle was removed \xe2\x9c\x93"), Colour (0xff22e3a5));
        else    fail ("Some files are in use. Close your DAW and uninstall again.");
        refreshState();
        return ok;
    }

    bool selfDeleteOnExit = false;

private:
    bool fail (const String& message)
    {
        setStatus (message, skin::red2);
        return false;
    }

    void setStatus (const String& s, Colour c)
    {
        status = s;
        statusColour = c;
        repaint();
    }

    void refreshState()
    {
        const bool installed = installedList().existsAsFile();
        uninstallBtn.setEnabled (installed);
        installBtn.setButtonText (installed ? "REINSTALL" : "INSTALL");
        installBtn.setEnabled (payload.zip != nullptr);
        if (status.isEmpty())
            setStatus (payload.zip == nullptr ? "Uninstaller: click UNINSTALL to remove the SKAZI Bundle."
                       : installed ? "The SKAZI Bundle is already installed. Reinstall to update it, or uninstall to remove it."
                                   : "Close your DAW, pick your plugins and click INSTALL. Older separate installs of them are replaced.",
                       skin::textDim);
    }

    Payload payload;
    skin::MetalLookAndFeel lnf;
    Image faceplate, logo;
    Rectangle<int> panel;
    OwnedArray<ToggleButton> toggles;
    ToggleButton optApps, optDesktop;
    TextButton installBtn { "INSTALL" }, uninstallBtn { "UNINSTALL" }, closeBtn { "CLOSE" };
    String status;
    Colour statusColour = skin::textDim;
};

class SetupApp : public JUCEApplication
{
public:
    const String getApplicationName() override    { return "SKAZI Bundle Setup"; }
    const String getApplicationVersion() override { return kVersion; }
    bool moreThanOneInstanceAllowed() override    { return false; }

    // "/silent" installs everything with the default options and no window ("/uninstall /silent" removes);
    // the exit code is 0 on success
    void initialise (const String& commandLine) override
    {
        const bool uninstallMode = commandLine.containsIgnoreCase ("/uninstall");
        if (commandLine.containsIgnoreCase ("/silent"))
        {
            silent = std::make_unique<SetupComponent> (false);
            content = silent.get();
            const bool ok = uninstallMode ? silent->uninstall() : silent->install();
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }
        content = new SetupComponent (uninstallMode);
        window = std::make_unique<Window> (content);
    }

    void shutdown() override
    {
        const bool selfDelete = content != nullptr && content->selfDeleteOnExit;
        window = nullptr;
        silent = nullptr;
        content = nullptr;
        if (selfDelete)
        {
            // a detached cmd waits for this process to exit, then removes the install folder
            ChildProcess cmd;
            cmd.start ("cmd.exe /c cd /d %TEMP% & ping 127.0.0.1 -n 3 > nul & rmdir /s /q \"" + appDir().getFullPathName() + "\"", 0);
        }
    }

private:
    struct Window : public DocumentWindow
    {
        explicit Window (Component* c)
            : DocumentWindow ("SKAZI Bundle Setup", Colours::black, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (c, true);
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::quit(); }
    };

    SetupComponent* content = nullptr; // owned by the window, or by `silent`
    std::unique_ptr<SetupComponent> silent;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (SetupApp)
