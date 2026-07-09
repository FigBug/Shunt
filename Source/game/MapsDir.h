#pragma once

#include <juce_core/juce_core.h>

namespace game
{

// The shared, world-writable folder that holds map files. The installer creates
// and populates it; the game scans it on launch and the editor reads/writes it.
// Resolves to the system-wide app-data location when that's usable, falling back
// to a per-user folder for dev builds / platforms without an installer. Both the
// game and the editor call this so they always agree on the location.
//
//   macOS   : /Library/Application Support/Shunt/Maps   (user: ~/Library/...)
//   Windows : C:/ProgramData/Shunt/Maps                 (user: AppData/Roaming)
//   Linux   : ~/.config/Shunt/Maps
inline juce::File mapsDirectory()
{
    using F = juce::File;

   #if JUCE_DEBUG
    // Dev builds (game and editor) read and write the repository's Maps/ folder
    // directly, via this header's path — so edits show up in source control.
    // __FILE__ is <repo>/Source/game/MapsDir.h.
    auto repoMaps = F (__FILE__).getParentDirectory()   // Source/game
                        .getParentDirectory()           // Source
                        .getParentDirectory()           // <repo>
                        .getChildFile ("Maps");
    repoMaps.createDirectory();
    return repoMaps;
   #else
    auto sub = [] (F base) -> F
    {
       #if JUCE_MAC
        return base.getChildFile ("Application Support").getChildFile ("Shunt").getChildFile ("Maps");
       #else
        return base.getChildFile ("Shunt").getChildFile ("Maps");
       #endif
    };

    #if JUCE_LINUX
     auto dir = sub (F::getSpecialLocation (F::userApplicationDataDirectory));
     dir.createDirectory();
     return dir;
    #else
     auto shared = sub (F::getSpecialLocation (F::commonApplicationDataDirectory));
     if (shared.isDirectory() || shared.createDirectory())
         return shared;

     // No access to the shared location (unprivileged dev run) — use the user's.
     auto user = sub (F::getSpecialLocation (F::userApplicationDataDirectory));
     user.createDirectory();
     return user;
    #endif
   #endif
}

} // namespace game
