#include "pch.h"

#include "XRayBlockCatalog.h"

#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/level/block/BlockLegacy.h"

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace Nexus {

    namespace {

        using TranslationMap = std::unordered_map<std::string, std::wstring>;

        //
        // ============================================================
        // DEBUG OUTPUT
        // ============================================================
        //

        void debugLog(const std::string& message) {
            std::string line = "[Nexus XRayBlockCatalog] " + message + "\n";

            OutputDebugStringA(line.c_str());
        }

        //
        // ============================================================
        // STRING HELPERS
        // ============================================================
        //

        std::string trim(std::string value) {
            auto isSpace = [](unsigned char ch) {
                return std::isspace(ch) != 0;
            };

            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
                value.erase(value.begin());
            }

            while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
                value.pop_back();
            }

            return value;
        }

        std::wstring utf8ToWide(std::string_view text) {
            if (text.empty()) {
                return {};
            }

            int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

            if (size <= 0) {
                return {};
            }

            std::wstring result(static_cast<std::size_t>(size), L'\0');

            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);

            return result;
        }

        //
        // ============================================================
        // MINECRAFT DIRECTORY
        // ============================================================
        //

        std::filesystem::path getMinecraftExecutableDirectory() {
            std::array<wchar_t, 32768> buffer {};

            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

            if (length == 0 || length >= buffer.size()) {
                return {};
            }

            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }

        //
        // ============================================================
        // VANILLA RESOURCE PACK DIRECTORIES
        // ============================================================
        //

        std::vector<std::filesystem::path> findVanillaPackDirectories() {
            std::vector<std::filesystem::path> result;

            const auto gameDirectory = getMinecraftExecutableDirectory();

            if (gameDirectory.empty()) {
                debugLog("Could not determine Minecraft executable directory.");

                return result;
            }

            debugLog("Minecraft directory: " + gameDirectory.string());

            //
            // Different Bedrock builds/install layouts have used
            // slightly different resource-pack roots.
            //
            const std::array roots { gameDirectory / "data" / "resource_packs",

                                     gameDirectory / "resource_packs" };

            std::error_code error;

            for (const auto& root : roots) {
                if (!std::filesystem::exists(root, error) || error) {
                    error.clear();
                    continue;
                }

                debugLog("Found resource pack root: " + root.string());

                for (std::filesystem::directory_iterator iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, error);
                     iterator != std::filesystem::directory_iterator(); iterator.increment(error)) {
                    if (error) {
                        error.clear();
                        continue;
                    }

                    if (!iterator->is_directory(error)) {
                        error.clear();
                        continue;
                    }

                    std::string folderName = iterator->path().filename().string();

                    //
                    // Bedrock sometimes layers vanilla data into
                    // folders such as:
                    //
                    // vanilla
                    // vanilla_1.14
                    // vanilla_1.16
                    // etc.
                    //
                    // Loading all vanilla* packs gives us a union of
                    // the available block definitions.
                    //
                    if (folderName == "vanilla" || folderName.starts_with("vanilla_")) {
                        result.push_back(iterator->path());
                    }
                }
            }

            //
            // Remove duplicates.
            //
            std::sort(result.begin(), result.end());

            result.erase(std::unique(result.begin(), result.end()), result.end());

            //
            // Make sure the main "vanilla" pack comes first.
            //
            std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
                bool aBase = a.filename() == "vanilla";

                bool bBase = b.filename() == "vanilla";

                if (aBase != bBase) {
                    return aBase;
                }

                return a.filename().string() < b.filename().string();
            });

            return result;
        }

        //
        // ============================================================
        // LANGUAGE FILE
        // ============================================================
        //

        void loadLanguageFile(const std::filesystem::path& file, TranslationMap& translations) {
            std::ifstream input(file, std::ios::binary);

            if (!input) {
                return;
            }

            std::string line;

            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                //
                // UTF-8 BOM.
                //
                if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
                    static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
                    line.erase(0, 3);
                }

                if (line.empty()) {
                    continue;
                }

                if (line.starts_with("#")) {
                    continue;
                }

                std::size_t equals = line.find('=');

                if (equals == std::string::npos) {
                    continue;
                }

                std::string key = trim(line.substr(0, equals));

                std::string value = line.substr(equals + 1);

                //
                // Bedrock .lang files can put comments/metadata after
                // a tab. We only want the visible translated string.
                //
                std::size_t tab = value.find('\t');

                if (tab != std::string::npos) {
                    value.resize(tab);
                }

                value = trim(std::move(value));

                if (key.empty() || value.empty()) {
                    continue;
                }

                std::wstring wide = utf8ToWide(value);

                if (!wide.empty()) {
                    translations[key] = std::move(wide);
                }
            }

            debugLog("Loaded language file: " + file.string());
        }

        //
        // ============================================================
        // TRANSLATION LOOKUP
        // ============================================================
        //

        std::wstring findBlockTranslation(std::string_view namespacedId, const TranslationMap& translations) {
            std::string_view identifier = namespacedId;

            std::size_t colon = namespacedId.find(':');

            if (colon != std::string_view::npos && colon + 1 < namespacedId.size()) {
                identifier = namespacedId.substr(colon + 1);
            }

            //
            // Standard Bedrock block translation:
            //
            // minecraft:diamond_ore
            // ->
            // tile.diamond_ore.name
            //
            std::string tileKey = "tile." + std::string(identifier) + ".name";

            if (auto found = translations.find(tileKey); found != translations.end()) {
                return found->second;
            }

            //
            // A few pieces of content may expose their visible name
            // through item localization instead.
            //
            std::string itemKey = "item." + std::string(identifier) + ".name";

            if (auto found = translations.find(itemKey); found != translations.end()) {
                return found->second;
            }

            return {};
        }

        //
        // ============================================================
        // JSON FILE READER
        // ============================================================
        //

        std::string readWholeFile(const std::filesystem::path& file) {
            std::ifstream input(file, std::ios::binary);

            if (!input) {
                return {};
            }

            std::ostringstream stream;

            stream << input.rdbuf();

            return stream.str();
        }

        //
        // ============================================================
        // BLOCKS.JSON
        // ============================================================
        //

        std::size_t loadBlocksJson(const std::filesystem::path& file, const TranslationMap& translations) {
            std::string contents = readWholeFile(file);

            if (contents.empty()) {
                return 0;
            }

            //
            // ignore_comments = true
            //
            // This makes us tolerant of JSONC-style comments if a
            // particular Bedrock resource file contains them.
            //
            nlohmann::json document = nlohmann::json::parse(contents, nullptr, false, true);

            if (document.is_discarded() || !document.is_object()) {
                debugLog("Failed to parse: " + file.string());

                return 0;
            }

            std::size_t before = XRayBlockCatalog::size();

            for (auto iterator = document.begin(); iterator != document.end(); ++iterator) {
                std::string rawId = iterator.key();

                //
                // Metadata field, not a block.
                //
                if (rawId == "format_version") {
                    continue;
                }

                //
                // Block entries are objects.
                //
                if (!iterator.value().is_object()) {
                    continue;
                }

                if (rawId.empty()) {
                    continue;
                }

                std::string namespacedId;

                if (rawId.find(':') == std::string::npos) {
                    namespacedId = "minecraft:" + rawId;
                } else {
                    namespacedId = rawId;
                }

                std::wstring displayName = findBlockTranslation(namespacedId, translations);

                if (displayName.empty()) {
                    XRayBlockCatalog::observeId(namespacedId);

                } else {
                    XRayBlockCatalog::observeId(namespacedId, displayName);
                }
            }

            std::size_t added = XRayBlockCatalog::size() - before;

            debugLog("Loaded blocks.json: " + file.string() + " | new blocks: " + std::to_string(added));

            return added;
        }

    } // namespace

    //
    // ================================================================
    // INITIALIZATION
    // ================================================================
    //

    void XRayBlockCatalog::initialize() {
        if (initialized) {
            return;
        }

        initialized = true;

        vanillaResourcesLoaded = loadInstalledVanillaResources();

        debugLog("Catalog initialization finished. Total IDs: " + std::to_string(size()));
    }

    //
    // ================================================================
    // INSTALLED VANILLA RESOURCE LOADER
    // ================================================================
    //

    bool XRayBlockCatalog::loadInstalledVanillaResources() {
        auto packDirectories = findVanillaPackDirectories();

        if (packDirectories.empty()) {
            debugLog("No vanilla resource-pack directories found. "
                     "Runtime scanner fallback will remain active.");

            return false;
        }

        TranslationMap translations;

        //
        // First collect translations from every vanilla layer.
        //
        for (const auto& pack : packDirectories) {
            loadLanguageFile(pack / "texts" / "en_US.lang", translations);
        }

        std::size_t before = size();

        //
        // Then collect all block IDs.
        //
        for (const auto& pack : packDirectories) {
            loadBlocksJson(pack / "blocks.json", translations);
        }

        std::size_t added = size() - before;

        debugLog("Vanilla resource loading complete. Added " + std::to_string(added) + " block IDs.");

        return added > 0;
    }

    //
    // ================================================================
    // RUNTIME OBSERVATION
    // ================================================================
    //

    void XRayBlockCatalog::observe(SDK::Block* block) {
        if (!block || !block->legacyBlock) {
            return;
        }

        std::string id = block->legacyBlock->namespacedId.getString();

        if (id.empty()) {
            return;
        }

        observeId(id);
    }

    //
    // ================================================================
    // ADD ID
    // ================================================================
    //

    void XRayBlockCatalog::observeId(std::string_view namespacedId) {
        observeId(namespacedId, {});
    }

    void XRayBlockCatalog::observeId(std::string_view namespacedId, std::wstring_view displayName) {
        if (namespacedId.empty()) {
            return;
        }

        //
        // Require namespaced runtime-style IDs.
        //
        // This still allows addon namespaces:
        //
        // minecraft:stone
        // nexus:ruby_ore
        // somepack:machine
        //
        if (namespacedId.find(':') == std::string_view::npos) {
            return;
        }

        std::string id(namespacedId);

        auto [iterator, inserted] = idSet.insert(id);

        if (inserted) {
            ids.push_back(id);
            dirty = true;
        }

        //
        // Allow later sources to improve an existing entry's name.
        //
        if (!displayName.empty()) {
            std::wstring name(displayName);

            auto found = displayNameOverrides.find(id);

            if (found == displayNameOverrides.end() || found->second != name) {
                displayNameOverrides[id] = std::move(name);

                dirty = true;
            }
        }
    }

    //
    // ================================================================
    // GET ENTRIES
    // ================================================================
    //

    const std::vector<XRayCatalogEntry>& XRayBlockCatalog::getEntries() {
        if (!initialized) {
            initialize();
        }

        if (dirty) {
            rebuild();
        }

        return entries;
    }

    //
    // ================================================================
    // CLEAR
    // ================================================================
    //

    void XRayBlockCatalog::clear() {
        idSet.clear();
        ids.clear();

        displayNameOverrides.clear();

        entries.clear();

        initialized = false;
        vanillaResourcesLoaded = false;

        dirty = true;
    }

    //
    // ================================================================
    // REBUILD DISPLAY LIST
    // ================================================================
    //

    void XRayBlockCatalog::rebuild() {
        entries.clear();

        //
        // Keep the stored ID vector deterministic.
        //
        std::sort(ids.begin(), ids.end());

        entries.reserve(ids.size());

        for (const auto& id : ids) {
            std::wstring displayName;

            if (auto found = displayNameOverrides.find(id); found != displayNameOverrides.end()) {
                displayName = found->second;
            }

            if (displayName.empty()) {
                displayName = makeDisplayName(id);
            }

            entries.push_back({ id, std::move(displayName) });
        }

        dirty = false;
    }

    //
    // ================================================================
    // FALLBACK DISPLAY NAME
    // ================================================================
    //

    std::wstring XRayBlockCatalog::makeDisplayName(std::string_view namespacedId) {
        std::string_view identifier = namespacedId;

        std::size_t colon = namespacedId.find(':');

        if (colon != std::string_view::npos && colon + 1 < namespacedId.size()) {
            identifier = namespacedId.substr(colon + 1);
        }

        std::wstring result;

        result.reserve(identifier.size());

        bool newWord = true;

        for (char raw : identifier) {
            unsigned char value = static_cast<unsigned char>(raw);

            if (raw == '_' || raw == '-' || raw == '.') {
                if (!result.empty() && result.back() != L' ') {
                    result.push_back(L' ');
                }

                newWord = true;

                continue;
            }

            if (newWord) {
                result.push_back(static_cast<wchar_t>(std::toupper(value)));

                newWord = false;

            } else {
                result.push_back(static_cast<wchar_t>(std::tolower(value)));
            }
        }

        if (result.empty()) {
            return L"Unknown Block";
        }

        return result;
    }

} // namespace Nexus
