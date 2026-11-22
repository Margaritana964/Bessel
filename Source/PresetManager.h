#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <set>
#include <vector>

// 插件版本：工厂预设的版本戳用它判断要不要重建。
// 在 CMakeLists.txt 里由 BESSEL_VERSION 覆盖。
#ifndef BESSEL_VERSION
 #define BESSEL_VERSION "0.2.0"
#endif

namespace sfx
{

//==============================================================================
// 预设管理（v2）
//
// 目录结构（分开是必须的）：
//
//   %APPDATA%\Bessel\Presets\
//     Factory\  ← 随插件版本重写，用户不该往里存东西
//     User\     ← 只增不删，插件永远不碰
//
// 为什么要分开：
//   · 混在一起的话，用户删掉一个工厂预设就再也拿不回来了
//   · 插件升级想更新工厂预设，会覆盖用户手里的同名文件
//
// 文件格式（v2）在根节点上带版本号：
//
//   <PARAMETERS besselFormat="2" pluginVersion="0.2.0" presetName="…">
//     <PARAM id="carrier" value="220"/>
//     …
//   </PARAMETERS>
//
// 加载时会统计「文件里缺的参数」和「插件不认识的参数」：
// 老预设（v1，没有版本号）加载进来不会报错，但可能悄悄少几个参数，
// 所以要把这件事告诉用户，而不是静默吞掉。
//==============================================================================
class PresetManager
{
public:
    static constexpr int formatVersion = 2;
    static constexpr const char* extension = ".bessel";

    struct Entry
    {
        juce::File file;
        bool factory = false;
    };

    struct LoadResult
    {
        bool ok          = false;
        int  missing     = 0;      // 插件有、文件没有的参数
        int  unknown     = 0;      // 文件有、插件没有的参数
        int  fromFormat  = 0;      // 文件里的 besselFormat（0 = 老格式）
        juce::String message;      // 给界面显示的一句话
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& stateToUse)
        : apvts (stateToUse)
    {
        migrateLegacyFlatPresets();
        rescan();
    }

    /** v1 的预设是平铺在 Presets\ 根目录的，v2 只扫 Factory\ 和 User\。
     *  这里把老文件搬进 User\，避免升级之后"预设全没了"。 */
    static void migrateLegacyFlatPresets()
    {
        const auto root = getRootDirectory();

        std::vector<juce::File> legacy;

        for (const auto& entry : juce::RangedDirectoryIterator (root, false,
                                                                juce::String ("*") + extension,
                                                                juce::File::findFiles))
            legacy.push_back (entry.getFile());

        if (legacy.empty())
            return;

        const auto userDir = getUserDirectory();

        for (const auto& file : legacy)
        {
            auto target = userDir.getChildFile (file.getFileName());
            int counter = 2;

            while (target.existsAsFile())
                target = userDir.getChildFile (file.getFileNameWithoutExtension()
                                                 + " " + juce::String (counter++) + extension);

            file.moveFileTo (target);
        }
    }

    //==========================================================================
    static juce::File getRootDirectory()
    {
        // 环境变量覆盖：便携安装 / CI / 自动化测试
        const auto override = juce::SystemStats::getEnvironmentVariable ("BESSEL_PRESET_DIR", {});

        auto root = override.isNotEmpty()
                      ? juce::File (override)
                      : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("Bessel")
                          .getChildFile ("Presets");

        if (! root.isDirectory())
            root.createDirectory();

        return root;
    }

    static juce::File getFactoryDirectory() { return ensureSub ("Factory"); }
    static juce::File getUserDirectory()    { return ensureSub ("User"); }

    /** 工厂预设要不要重建：版本戳不匹配，或者目录里一个都没有。 */
    static bool factoryNeedsRebuild()
    {
        const auto dir = getFactoryDirectory();

        if (! dir.getChildFile (".version").loadFileAsString().trim().equalsIgnoreCase (BESSEL_VERSION))
            return true;

        for (const auto& entry : juce::RangedDirectoryIterator (dir, false,
                                                                juce::String ("*") + extension,
                                                                juce::File::findFiles))
        {
            juce::ignoreUnused (entry);
            return false;      // 有一个就够了
        }

        return true;
    }

    static void writeVersionStamp()
    {
        getFactoryDirectory().getChildFile (".version")
            .replaceWithText (juce::String (BESSEL_VERSION));
    }

    //==========================================================================
    void rescan()
    {
        entries.clear();

        const auto collect = [this] (const juce::File& dir, bool factory)
        {
            if (! dir.isDirectory())
                return;

            std::vector<juce::File> found;

            for (const auto& entry : juce::RangedDirectoryIterator (dir, false,
                                                                    juce::String ("*") + extension,
                                                                    juce::File::findFiles))
                found.push_back (entry.getFile());

            std::sort (found.begin(), found.end(),
                       [] (const juce::File& a, const juce::File& b)
                       { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

            for (const auto& file : found)
                entries.push_back ({ file, factory });
        };

        collect (getFactoryDirectory(), true);
        collect (getUserDirectory(),    false);

        if (currentIndex >= (int) entries.size())
            currentIndex = (int) entries.size() - 1;
    }

    int size() const noexcept { return (int) entries.size(); }

    bool isFactory (int index) const noexcept
    {
        return index >= 0 && index < (int) entries.size() && entries[(size_t) index].factory;
    }

    juce::String getName (int index) const
    {
        if (index < 0 || index >= (int) entries.size())
            return {};

        return entries[(size_t) index].file.getFileNameWithoutExtension();
    }

    juce::String getCurrentName() const
    {
        const auto name = getName (currentIndex);
        return name.isNotEmpty() ? name : juce::String ("Init");
    }

    int getCurrentIndex() const noexcept { return currentIndex; }

    //==========================================================================
    LoadResult load (int index)
    {
        LoadResult result;

        if (index < 0 || index >= (int) entries.size())
            return result;

        auto xml = juce::XmlDocument::parse (entries[(size_t) index].file);

        if (xml == nullptr)
        {
            result.message = "preset: unreadable file";
            postMessage (result.message);
            return result;
        }

        const auto tree = juce::ValueTree::fromXml (*xml);

        if (! tree.isValid())
        {
            result.message = "preset: bad format";
            postMessage (result.message);
            return result;
        }

        // --- 版本与参数完整性 ------------------------------------------------
        result.fromFormat = (int) xml->getIntAttribute ("besselFormat", 0);

        std::set<juce::String> pluginIds;

        for (auto* parameter : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                pluginIds.insert (ranged->getParameterID());

        std::set<juce::String> fileIds;

        for (auto* element : xml->getChildWithTagNameIterator ("PARAM"))
            fileIds.insert (element->getStringAttribute ("id"));

        for (const auto& id : pluginIds)
            if (fileIds.count (id) == 0)
                ++result.missing;

        for (const auto& id : fileIds)
            if (pluginIds.count (id) == 0)
                ++result.unknown;

        // 不要直接用 replaceState(tree)：
        // 那样只有文件里**存在**的参数会被替换，缺的参数会保留上一个预设的值。
        // 正确的语义是"先全部回到默认值，再套用文件里的值" ——
        // 这样"不带 comb 的预设"里 comb 相关参数天然是初始值。
        auto merged = apvts.copyState();

        for (int i = 0; i < merged.getNumChildren(); ++i)
        {
            auto child = merged.getChild (i);
            const auto id = child.getProperty ("id").toString();

            if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
                child.setProperty ("value", parameter->convertFrom0to1 (parameter->getDefaultValue()),
                                   nullptr);
        }

        for (auto* element : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = element->getStringAttribute ("id");
            const auto value = element->getDoubleAttribute ("value", 0.0);

            for (int i = 0; i < merged.getNumChildren(); ++i)
            {
                auto child = merged.getChild (i);

                if (child.getProperty ("id").toString() == id)
                {
                    child.setProperty ("value", value, nullptr);
                    break;
                }
            }
        }

        apvts.replaceState (merged);
        currentIndex = index;
        result.ok = true;

        // --- 给用户的一句话 --------------------------------------------------
        juce::StringArray notes;

        if (result.fromFormat < formatVersion)
            notes.add ("old format");

        if (result.missing > 0)
            notes.add (juce::String (result.missing) + " param(s) missing");

        if (result.unknown > 0)
            notes.add (juce::String (result.unknown) + " unknown");

        result.message = notes.isEmpty() ? juce::String()
                                         : ("preset: " + notes.joinIntoString (", "));

        postMessage (result.message.isEmpty() ? (juce::String ("loaded ") + getCurrentName())
                                              : result.message);

        return result;
    }

    /** 存成文件。toFactory 只是给内部/测试用，界面上永远存进 User。 */
    juce::String save (const juce::String& requestedName, bool toFactory = false)
    {
        auto name = requestedName.trim();

        if (name.isEmpty())
            name = juce::String ("User ") + juce::String (size() + 1);

        const auto dir = toFactory ? getFactoryDirectory() : getUserDirectory();
        auto file = dir.getChildFile (juce::File::createLegalFileName (name) + extension);

        auto xml = apvts.copyState().createXml();

        if (xml == nullptr)
            return {};

        xml->setAttribute ("besselFormat", formatVersion);
        xml->setAttribute ("pluginVersion", juce::String (BESSEL_VERSION));
        xml->setAttribute ("presetName", name);

        // 不用 XmlElement::writeTo(File)：它内部走 TemporaryFile + 重命名，
        // 在某些受限环境里重命名会被拒。直接写流更可靠。
        //
        // ⚠️ FileOutputStream **不会截断已有文件**，它从末尾开始写。
        // 不先删掉的话，每次 SAVE 都在文件尾部再追加一份完整状态，
        // 加载时只读最旧的那份 —— 表现就是"改了参数再存不生效"。
        file.deleteFile();

        {
            juce::FileOutputStream out (file);

            if (! out.openedOk())
            {
                postMessage ("preset: save failed (permission?)");
                return {};
            }

            xml->writeTo (out);
            out.flush();

            if (out.getStatus().failed())
            {
                postMessage ("preset: save failed (write error)");
                return {};
            }
        }

        rescan();
        currentIndex = -1;

        for (int i = 0; i < size(); ++i)
            if (getName (i).equalsIgnoreCase (name) && isFactory (i) == toFactory)
                currentIndex = i;

        postMessage (juce::String ("saved ") + name);

        return name;
    }

    /** 当前选中的是不是"可以覆盖 / 重命名"的用户预设。工厂预设只读。 */
    bool canModifyCurrent() const noexcept
    {
        return currentIndex >= 0 && ! isFactory (currentIndex);
    }

    /** 用当前状态覆盖选中的用户预设（SAVE）。 */
    juce::String overwriteCurrent()
    {
        if (! canModifyCurrent())
            return {};

        return save (getCurrentName(), false);
    }

    /** 重命名选中的用户预设。目标已存在返回 false。 */
    bool renameCurrent (const juce::String& newName)
    {
        if (! canModifyCurrent())
            return false;

        const auto trimmed = newName.trim();

        if (trimmed.isEmpty())
            return false;

        const auto source = entries[(size_t) currentIndex].file;
        auto target = getUserDirectory().getChildFile (juce::File::createLegalFileName (trimmed) + extension);

        if (target == source)
            return true;

        if (target.existsAsFile())
        {
            postMessage ("rename: name already exists");
            return false;
        }

        if (! source.moveFileTo (target))
        {
            postMessage ("rename failed");
            return false;
        }

        rescan();

        for (int i = 0; i < size(); ++i)
            if (! isFactory (i) && getName (i).equalsIgnoreCase (trimmed))
                currentIndex = i;

        postMessage (juce::String ("renamed to ") + trimmed);
        return true;
    }

    /** 删除选中的用户预设：进回收站，可恢复。工厂预设不让删。 */
    bool deleteCurrent()
    {
        if (! canModifyCurrent())
            return false;

        const auto file = entries[(size_t) currentIndex].file;

        if (! file.moveToTrash())
        {
            postMessage ("delete failed");
            return false;
        }

        rescan();
        currentIndex = -1;
        postMessage ("deleted");
        return true;
    }

    /** 选中某个索引（列表点击时用，不加载）。 */
    void setCurrentIndex (int index)
    {
        if (index >= 0 && index < (int) entries.size())
            currentIndex = index;
    }

    /** 下一个 / 上一个（会绕回）。 */
    void step (int delta)
    {
        if (entries.empty())
            return;

        const auto count = (int) entries.size();
        const auto from  = (currentIndex < 0) ? (delta > 0 ? -1 : 0) : currentIndex;
        const auto next  = ((from + delta) % count + count) % count;

        load (next);
    }

    //==========================================================================
    juce::String getLastMessage() const { return lastMessage; }
    double getLastMessageTime() const   { return lastMessageTime; }

    juce::File getFile (int index) const
    {
        if (index < 0 || index >= (int) entries.size())
            return {};

        return entries[(size_t) index].file;
    }

private:
    void postMessage (const juce::String& text)
    {
        lastMessage = text;
        lastMessageTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    static juce::File ensureSub (const char* child)
    {
        auto dir = getRootDirectory().getChildFile (child);

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    int currentIndex = -1;

    juce::String lastMessage;
    double lastMessageTime = 0.0;

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};

} // namespace sfx
