#include "Log.h"

#include <mutex>

#ifndef PITCHLANE_BUILD_ID
 #define PITCHLANE_BUILD_ID "local"
#endif
#ifndef PITCHLANE_VERSION
 #define PITCHLANE_VERSION "0.0.0"
#endif

namespace pitchlane::log {

namespace {
std::mutex& mutex()
{
    static std::mutex m;
    return m;
}
constexpr juce::int64 kMaxBytes = 1024 * 1024;
} // namespace

juce::File file()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("PITCHLANE_LOG_FILE", {});
    if (env.isNotEmpty()) return juce::File(env);
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/Logs/PitchLane/pitchlane.log");
#else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("PitchLane/pitchlane.log");
#endif
}

juce::String versionString()
{
    return juce::String(PITCHLANE_VERSION) + " (" + PITCHLANE_BUILD_ID + ")";
}

void write(const juce::String& category, const juce::String& message)
{
    const auto now = juce::Time::getCurrentTime();
    const auto line = now.formatted("%Y-%m-%d %H:%M:%S") + "." + juce::String(now.getMilliseconds()).paddedLeft('0', 3)
                    + " [" + category + "] " + message + "\n";
    const std::lock_guard<std::mutex> lock(mutex());
    const auto f = file();
    if (!f.getParentDirectory().createDirectory()) return;
    if (f.getSize() > kMaxBytes)
    {
        const auto old = f.getSiblingFile(f.getFileNameWithoutExtension() + ".old" + f.getFileExtension());
        old.deleteFile();
        f.moveFileTo(old);
    }
    f.appendText(line, false, false, "\n");
#if JUCE_DEBUG
    DBG("PitchLane " << line.trimEnd());
#endif
}

} // namespace pitchlane::log
