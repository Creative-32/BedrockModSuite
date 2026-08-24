#include "pch.h"

#include "XRayBlockListScreen.h"
#include "XRayScreen.h"

#include "client/Latite.h"

#include "client/event/Eventing.h"
#include "client/event/events/CharEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/KeyUpdateEvent.h"
#include "client/event/events/RenderOverlayEvent.h"

#include "client/feature/nexus/NexusConfig.h"
#include "client/feature/nexus/ui/NexusControls.h"
#include "client/feature/nexus/xray/XRayBlockCatalog.h"
#include "client/feature/nexus/xray/XRayTargets.h"

#include "client/screen/ScreenManager.h"

#include "util/DrawContext.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

    //
    // ============================================================
    // TEXT
    // ============================================================
    //

    std::wstring lowerText(std::wstring value) {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });

        return value;
    }

    std::wstring normalizeSearchText(std::wstring value) {
        value = lowerText(std::move(value));

        for (wchar_t& ch : value) {
            if (ch == L'_' || ch == L'-' || ch == L'.' || ch == L':') {
                ch = L' ';
            }
        }

        std::wstring result;

        result.reserve(value.size());

        bool lastWasSpace = true;

        for (wchar_t ch : value) {
            bool space = std::iswspace(ch) != 0;

            if (space) {
                if (!lastWasSpace) {
                    result.push_back(L' ');
                }

                lastWasSpace = true;
            }

            else {
                result.push_back(ch);
                lastWasSpace = false;
            }
        }

        while (!result.empty() && result.back() == L' ') {
            result.pop_back();
        }

        return result;
    }

    std::string_view blockPath(std::string_view id) {
        std::size_t colon = id.find(':');

        if (colon == std::string_view::npos) {
            return id;
        }

        if (colon + 1 >= id.size()) {
            return {};
        }

        return id.substr(colon + 1);
    }

    //
    // ============================================================
    // TECHNICAL BLOCK FILTER
    // ============================================================
    //

    bool isTechnicalBlockId(std::string_view id) {
        std::string_view path = blockPath(id);

        if (path.empty()) {
            return true;
        }

        //
        // Air.
        //
        if (path == "air" || path == "cave_air" || path == "void_air") {
            return true;
        }

        //
        // Invisible Light blocks.
        //
        // light_block
        // light_block_0
        // ...
        // light_block_15
        //
        if (path == "light_block" || path.starts_with("light_block_")) {
            return true;
        }

        //
        // Internal double slabs.
        //
        if (path.find("double_slab") != std::string_view::npos) {
            return true;
        }

        if (path.starts_with("double_") && path.find("slab") != std::string_view::npos) {
            return true;
        }

        //
        // Internal sign forms.
        //
        if (path.ends_with("_standing_sign") || path.ends_with("_wall_sign") || path.ends_with("_wall_hanging_sign")) {
            return true;
        }

        //
        // Banner internals.
        //
        if (path.ends_with("_standing_banner") || path.ends_with("_wall_banner")) {
            return true;
        }

        //
        // Fluid-state duplicates.
        //
        if (path == "flowing_water" || path == "flowing_lava") {
            return true;
        }

        //
        // State duplicates.
        //
        if (path == "lit_redstone_ore" || path == "lit_furnace") {
            return true;
        }

        //
        // Engine / collision helpers.
        //
        if (path == "moving_block" || path == "piston_arm_collision" || path == "sticky_piston_arm_collision" ||
            path == "reserved6" || path == "info_update" || path == "info_update2" || path == "unknown") {
            return true;
        }

        //
        // Education / chemistry blocks create a huge amount of noise.
        //
        if (path.starts_with("element_") || path == "compound_creator" || path == "material_reducer" ||
            path == "lab_table" || path == "allow" || path == "deny" || path == "border_block") {
            return true;
        }

        return false;
    }

    //
    // ============================================================
    // AUTO ORE
    // ============================================================
    //

    bool isAutoOreId(std::string_view id) {
        std::string_view path = blockPath(id);

        if (path == "ancient_debris") {
            return true;
        }

        std::size_t start = 0;

        while (start < path.size()) {
            std::size_t end = path.find('_', start);

            std::string_view token =
                end == std::string_view::npos ? path.substr(start) : path.substr(start, end - start);

            if (token == "ore") {
                return true;
            }

            if (end == std::string_view::npos) {
                break;
            }

            start = end + 1;
        }

        return false;
    }

    //
    // ============================================================
    // DYE COLORS
    // ============================================================
    //

    constexpr std::array<std::string_view, 16> DyeColors { { "white", "light_gray", "gray", "black", "brown", "red",
                                                             "orange", "yellow", "lime", "green", "cyan", "light_blue",
                                                             "blue", "purple", "magenta", "pink" } };

    int colorPriority(std::string_view path) {
        for (std::size_t index = 0; index < DyeColors.size(); ++index) {
            std::string prefix = std::string(DyeColors[index]) + "_";

            if (path.starts_with(prefix)) {
                return static_cast<int>(index);
            }
        }

        return 100;
    }

    //
    // ============================================================
    // GROUP DEFINITIONS
    // ============================================================
    //

    enum class GroupKind {
        Ores,
        ResourceBlocks,
        Storage,

        Stone,
        Ground,

        Wood,

        Plants,
        Farming,

        Nether,
        End,
        Ocean,

        Amethyst,
        Sculk,
        Copper,

        Wool,
        Concrete,
        Terracotta,
        Glass,

        Lighting,
        Redstone,
        Transport,
        Utility,
        DoorsBarriers,
        Special,

        Other
    };

    struct GroupDefinition {
        std::string_view key;
        std::wstring_view name;
        GroupKind kind;
    };

    const std::vector<GroupDefinition> GroupDefinitions {
        //
        // Most useful for X-Ray first.
        //
        { "ores", L"Ores", GroupKind::Ores },
        { "resources", L"Resource Blocks", GroupKind::ResourceBlocks },
        { "storage", L"Storage", GroupKind::Storage },

        //
        // Main terrain.
        //
        { "stone", L"Stone & Masonry", GroupKind::Stone },
        { "ground", L"Ground & Terrain", GroupKind::Ground },

        //
        // Wood families.
        //
        { "oak", L"Oak", GroupKind::Wood },
        { "spruce", L"Spruce", GroupKind::Wood },
        { "birch", L"Birch", GroupKind::Wood },
        { "jungle", L"Jungle", GroupKind::Wood },
        { "acacia", L"Acacia", GroupKind::Wood },
        { "dark_oak", L"Dark Oak", GroupKind::Wood },
        { "mangrove", L"Mangrove", GroupKind::Wood },
        { "cherry", L"Cherry", GroupKind::Wood },
        { "pale_oak", L"Pale Oak", GroupKind::Wood },
        { "bamboo", L"Bamboo", GroupKind::Wood },

        //
        // Overworld vegetation.
        //
        { "plants", L"Plants & Flowers", GroupKind::Plants },
        { "farming", L"Farming & Crops", GroupKind::Farming },

        //
        // Dimension/environment families.
        //
        { "nether", L"Nether", GroupKind::Nether },
        { "end", L"End", GroupKind::End },
        { "ocean", L"Ocean", GroupKind::Ocean },

        //
        // Special natural families.
        //
        { "amethyst", L"Amethyst", GroupKind::Amethyst },
        { "deep_dark", L"Deep Dark", GroupKind::Sculk },
        { "copper", L"Copper", GroupKind::Copper },

        //
        // Colored/building families.
        //
        { "wool", L"Wool", GroupKind::Wool },
        { "concrete", L"Concrete", GroupKind::Concrete },
        { "terracotta", L"Terracotta", GroupKind::Terracotta },
        { "glass", L"Glass", GroupKind::Glass },

        //
        // Functional groups.
        //
        { "lighting", L"Lighting", GroupKind::Lighting },
        { "redstone", L"Redstone", GroupKind::Redstone },
        { "transport", L"Rails & Transport", GroupKind::Transport },
        { "utility", L"Workstations", GroupKind::Utility },
        { "doors", L"Doors & Barriers", GroupKind::DoorsBarriers },
        { "special", L"Special Blocks", GroupKind::Special },

        //
        // Diagnostic fallback.
        //
        { "other", L"Other Blocks", GroupKind::Other }
    };

    //
    // ============================================================
    // WOOD MEMBERSHIP
    // ============================================================
    //

    bool belongsToWoodFamily(std::string_view id, std::string_view family) {
        std::string_view path = blockPath(id);

        if (path == family) {
            return true;
        }

        std::string prefix = std::string(family) + "_";

        if (path.starts_with(prefix)) {
            return true;
        }

        std::string strippedPrefix = "stripped_" + std::string(family) + "_";

        return path.starts_with(strippedPrefix);
    }

    //
    // ============================================================
    // RESOURCE BLOCKS
    // ============================================================
    //

    bool isResourceBlock(std::string_view path) {
        return path == "diamond_block" || path == "emerald_block" ||

               path == "gold_block" || path == "raw_gold_block" ||

               path == "iron_block" || path == "raw_iron_block" ||

               path == "lapis_block" || path == "redstone_block" || path == "coal_block" ||

               path == "netherite_block";
    }

    //
    // ============================================================
    // GROUP MEMBERSHIP
    // ============================================================
    //

    bool belongsToGroup(std::string_view id, const GroupDefinition& group) {
        std::string_view path = blockPath(id);

        switch (group.kind) {
            //
            // ========================================================
            // ORES
            // ========================================================
            //

        case GroupKind::Ores:
            //
            // Ore IDs such as:
            //
            // diamond_ore
            // deepslate_diamond_ore
            // nether_gold_ore
            // quartz_ore
            // addon ruby_ore
            //
            // Ancient Debris behaves like an ore for X-Ray purposes
            // despite not containing "_ore".
            //
            return isAutoOreId(id) || path == "ancient_debris";

            //
            // ========================================================
            // RESOURCE BLOCKS
            // ========================================================
            //

        case GroupKind::ResourceBlocks:
            return isResourceBlock(path);

            //
            // ========================================================
            // STORAGE
            // ========================================================
            //

        case GroupKind::Storage:
            return path == "chest" || path == "trapped_chest" || path == "ender_chest" || path == "barrel" ||

                   path == "shulker_box" || path.ends_with("_shulker_box");

            //
            // ========================================================
            // STONE & MASONRY
            // ========================================================
            //

        case GroupKind::Stone:
            return path == "stone" || path.starts_with("stone_") ||

                   path == "cobblestone" || path.starts_with("cobblestone_") ||

                   path == "smooth_stone" || path.starts_with("smooth_stone_") ||

                   path.find("stone_brick") != std::string_view::npos ||

                   path == "deepslate" || path.starts_with("deepslate_") ||

                   path == "cobbled_deepslate" || path.starts_with("cobbled_deepslate_") ||

                   path.find("deepslate_brick") != std::string_view::npos ||

                   path.find("deepslate_tile") != std::string_view::npos ||

                   path == "tuff" || path.starts_with("tuff_") ||

                   path.find("tuff_brick") != std::string_view::npos ||

                   path.find("granite") != std::string_view::npos ||

                   path.find("diorite") != std::string_view::npos ||

                   path.find("andesite") != std::string_view::npos ||

                   path.find("sandstone") != std::string_view::npos ||

                   path == "brick_block" || path == "bricks" || path.starts_with("brick_") ||

                   path.find("prismarine") != std::string_view::npos ||

                   //
                   // Quartz Ore already gets captured by Ores because
                   // Ores appears before Stone.
                   //
                   path.find("quartz") != std::string_view::npos ||

                   path == "calcite" || path == "dripstone_block" || path == "pointed_dripstone" ||

                   path == "bedrock" ||

                   path.starts_with("infested_");

            //
            // ========================================================
            // GROUND & TERRAIN
            // ========================================================
            //

        case GroupKind::Ground:
            return path == "dirt" || path == "coarse_dirt" || path == "rooted_dirt" || path == "grass_block" ||
                   path == "dirt_path" ||

                   path == "podzol" || path == "mycelium" ||

                   path == "sand" || path == "red_sand" || path == "gravel" || path == "clay" ||

                   path == "mud" || path == "packed_mud" || path == "muddy_mangrove_roots" ||

                   path == "snow" || path == "snow_layer" ||

                   path == "ice" || path == "packed_ice" || path == "blue_ice" || path == "frosted_ice";

            //
            // ========================================================
            // WOOD FAMILY
            // ========================================================
            //

        case GroupKind::Wood:
            return belongsToWoodFamily(id, group.key);

            //
            // ========================================================
            // PLANTS & FLOWERS
            // ========================================================
            //

        case GroupKind::Plants:
            return
                //
                // Grass / bushes.
                //
                path == "short_grass" || path == "tallgrass" || path == "fern" || path == "large_fern" ||
                path == "deadbush" || path == "bush" || path == "firefly_bush" ||

                //
                // Flowers.
                //
                path == "dandelion" || path == "poppy" || path == "blue_orchid" || path == "allium" ||
                path == "azure_bluet" || path == "oxeye_daisy" || path == "cornflower" ||
                path == "lily_of_the_valley" || path == "wither_rose" ||

                path.find("tulip") != std::string_view::npos ||

                path == "sunflower" || path == "lilac" || path == "rose_bush" || path == "peony" ||

                path == "pink_petals" || path == "pitcher_plant" || path == "torchflower" ||

                path.find("eyeblossom") != std::string_view::npos ||

                //
                // Moss / lichen.
                //
                path == "moss_block" || path == "moss_carpet" || path == "glow_lichen" || path == "pale_hanging_moss" ||
                path == "hanging_roots" || path == "leaf_litter" ||

                //
                // Overworld vines.
                //
                path == "vine" || path == "vines" ||

                path.find("cave_vines") != std::string_view::npos ||

                //
                // Lush cave plants.
                //
                path.find("dripleaf") != std::string_view::npos ||

                path.find("azalea") != std::string_view::npos ||

                path == "spore_blossom" ||

                //
                // Mushrooms.
                //
                path == "brown_mushroom" || path == "red_mushroom" ||

                path == "lily_pad";

            //
            // ========================================================
            // FARMING & CROPS
            // ========================================================
            //

        case GroupKind::Farming:
            return path == "wheat" || path == "carrots" || path == "potatoes" || path == "beetroot" ||

                   path == "melon" || path == "melon_stem" ||

                   path == "pumpkin" || path == "pumpkin_stem" || path == "carved_pumpkin" || path == "lit_pumpkin" ||

                   path == "cocoa" ||

                   path == "sugar_cane" || path == "cactus" ||

                   path == "sweet_berry_bush" ||

                   path == "farmland" || path == "composter" ||

                   path == "hay_block";

            //
            // ========================================================
            // NETHER
            // ========================================================
            //

        case GroupKind::Nether:
            return path == "netherrack" ||

                   path == "soul_sand" || path == "soul_soil" ||

                   path == "magma" || path == "magma_block" ||

                   path == "glowstone" ||

                   path == "nether_wart" || path == "nether_wart_block" ||

                   path == "crimson_nylium" || path == "warped_nylium" ||

                   path == "crimson_fungus" || path == "warped_fungus" ||

                   path == "crimson_roots" || path == "warped_roots" ||

                   path == "nether_sprouts" ||

                   path.starts_with("crimson_") || path.starts_with("warped_") ||

                   path.find("blackstone") != std::string_view::npos ||

                   path.find("basalt") != std::string_view::npos ||

                   path.find("nether_brick") != std::string_view::npos ||

                   path == "crying_obsidian" || path == "obsidian" ||

                   path == "respawn_anchor" ||

                   path == "soul_fire" || path == "soul_torch" || path == "soul_lantern" || path == "soul_campfire" ||

                   path.find("weeping_vines") != std::string_view::npos ||

                   path.find("twisting_vines") != std::string_view::npos;

            //
            // ========================================================
            // END
            // ========================================================
            //

        case GroupKind::End:
            return path == "end_stone" ||

                   path.find("end_stone_brick") != std::string_view::npos ||

                   path.find("purpur") != std::string_view::npos ||

                   path == "chorus_plant" || path == "chorus_flower" ||

                   path == "end_rod" ||

                   path == "end_portal" || path == "end_portal_frame" || path == "end_gateway" ||

                   path == "dragon_egg" || path == "dragon_head";

            //
            // ========================================================
            // OCEAN
            // ========================================================
            //

        case GroupKind::Ocean:
            return path == "water" || path == "bubble_column" ||

                   path.find("kelp") != std::string_view::npos ||

                   path == "seagrass" ||

                   path.find("coral") != std::string_view::npos ||

                   path == "sponge" || path == "wet_sponge" ||

                   path == "sea_pickle" || path == "sea_lantern" ||

                   path == "conduit" ||

                   path == "turtle_egg" ||

                   path == "frog_spawn" || path == "frogspawn";

            //
            // ========================================================
            // AMETHYST
            // ========================================================
            //

        case GroupKind::Amethyst:
            return path == "amethyst_block" || path == "block_of_amethyst" || path == "budding_amethyst" ||

                   path == "small_amethyst_bud" || path == "medium_amethyst_bud" || path == "large_amethyst_bud" ||

                   path == "amethyst_cluster";

            //
            // ========================================================
            // DEEP DARK
            // ========================================================
            //

        case GroupKind::Sculk:
            return path.starts_with("sculk") ||

                   path.find("_sculk") != std::string_view::npos ||

                   path == "reinforced_deepslate";

            //
            // ========================================================
            // COPPER
            // ========================================================
            //

        case GroupKind::Copper:
            //
            // Copper Ore is already captured by Ores.
            //
            // Everything else copper-related lives here:
            //
            // Copper Block
            // Raw Copper Block
            // Cut Copper
            // Waxed / Exposed / Weathered / Oxidized
            // Doors
            // Trapdoors
            // Grates
            // Bulbs
            // Lightning Rod
            //
            return path.find("copper") != std::string_view::npos ||

                   path == "lightning_rod";

            //
            // ========================================================
            // WOOL
            // ========================================================
            //

        case GroupKind::Wool:
            return path == "wool" || path.ends_with("_wool") ||

                   path == "carpet" || path.ends_with("_carpet");

            //
            // ========================================================
            // CONCRETE
            // ========================================================
            //

        case GroupKind::Concrete:
            return path.find("concrete") != std::string_view::npos;

            //
            // ========================================================
            // TERRACOTTA
            // ========================================================
            //

        case GroupKind::Terracotta:
            return path.find("terracotta") != std::string_view::npos;

            //
            // ========================================================
            // GLASS
            // ========================================================
            //

        case GroupKind::Glass:
            return path.find("glass") != std::string_view::npos;

            //
            // ========================================================
            // LIGHTING
            // ========================================================
            //

        case GroupKind::Lighting:
            return path == "torch" || path == "redstone_torch" ||

                   path == "lantern" || path == "campfire" ||

                   path == "jack_o_lantern" ||

                   path.find("froglight") != std::string_view::npos ||

                   path == "candle" || path.ends_with("_candle") ||

                   path.find("candle_cake") != std::string_view::npos;

            //
            // ========================================================
            // REDSTONE
            // ========================================================
            //

        case GroupKind::Redstone:
            return path == "redstone_wire" ||

                   path == "observer" ||

                   path == "piston" || path == "sticky_piston" ||

                   path == "dispenser" || path == "dropper" || path == "hopper" ||

                   path == "crafter" ||

                   path == "lever" ||

                   path.ends_with("_button") || path.ends_with("_pressure_plate") ||

                   path.find("repeater") != std::string_view::npos ||

                   path.find("comparator") != std::string_view::npos ||

                   path.find("daylight_detector") != std::string_view::npos ||

                   path == "target" ||

                   path == "tnt" || path == "note_block";

            //
            // ========================================================
            // RAILS & TRANSPORT
            // ========================================================
            //

        case GroupKind::Transport:
            return path == "rail" || path.ends_with("_rail") ||

                   path.find("minecart") != std::string_view::npos;

            //
            // ========================================================
            // WORKSTATIONS
            // ========================================================
            //

        case GroupKind::Utility:
            return path == "crafting_table" ||

                   path == "furnace" || path == "blast_furnace" || path == "smoker" ||

                   path == "brewing_stand" ||

                   path == "anvil" || path == "chipped_anvil" || path == "damaged_anvil" ||

                   path == "grindstone" || path == "stonecutter" ||

                   path == "smithing_table" || path == "fletching_table" || path == "cartography_table" ||

                   path == "loom" || path == "lectern" ||

                   path == "enchanting_table" ||

                   path == "beacon" ||

                   path == "cauldron" || path.find("_cauldron") != std::string_view::npos ||

                   path == "scaffolding" ||

                   path == "lodestone" ||

                   path == "bookshelf" || path == "chiseled_bookshelf";

            //
            // ========================================================
            // DOORS & BARRIERS
            // ========================================================
            //

        case GroupKind::DoorsBarriers:
            return
                //
                // Wood doors stay inside their own wood families.
                // Copper doors stay inside Copper.
                //
                path == "iron_door" || path == "iron_trapdoor" ||

                path == "iron_bars" || path == "chain" ||

                //
                // Generic/legacy versions, if shown.
                //
                path == "wooden_door" || path == "trapdoor" || path == "fence" || path == "fence_gate";

            //
            // ========================================================
            // SPECIAL BLOCKS
            // ========================================================
            //

        case GroupKind::Special:
            return
                //
                // Mob / Trial Chamber blocks.
                //
                path == "mob_spawner" || path == "spawner" || path == "trial_spawner" || path == "vault" ||

                //
                // Archaeology.
                //
                path == "suspicious_sand" || path == "suspicious_gravel" || path == "decorated_pot" ||

                //
                // Decorative/special objects.
                //
                path == "flower_pot" ||

                path.find("banner") != std::string_view::npos ||

                path.find("skull") != std::string_view::npos ||

                path.find("_head") != std::string_view::npos ||

                path == "bed" || path.ends_with("_bed") ||

                path.find("cake") != std::string_view::npos ||

                path == "bell" ||

                path == "cobweb" ||

                path == "honey_block" || path == "honeycomb_block" ||

                path == "bone_block" ||

                path.find("resin") != std::string_view::npos ||

                path == "heavy_core";

            //
            // ========================================================
            // OTHER
            // ========================================================
            //

        case GroupKind::Other:
            return true;
        }

        return false;
    }

} // namespace

//
// ============================================================
// GROUP ITEM ORDER
// ============================================================
//

int groupPriority(std::string_view path, const GroupDefinition& group) {
    switch (group.kind) {
        //
        // ========================================================
        // ORES
        // ========================================================
        //

    case GroupKind::Ores:
        if (path == "diamond_ore") return 0;

        if (path == "deepslate_diamond_ore") return 1;

        if (path == "ancient_debris") return 2;

        if (path == "emerald_ore") return 3;

        if (path == "deepslate_emerald_ore") return 4;

        if (path == "gold_ore") return 5;

        if (path == "deepslate_gold_ore") return 6;

        if (path == "nether_gold_ore") return 7;

        if (path == "iron_ore") return 8;

        if (path == "deepslate_iron_ore") return 9;

        if (path == "copper_ore") return 10;

        if (path == "deepslate_copper_ore") return 11;

        if (path == "redstone_ore") return 12;

        if (path == "deepslate_redstone_ore") return 13;

        if (path == "lapis_ore") return 14;

        if (path == "deepslate_lapis_ore") return 15;

        if (path == "coal_ore") return 16;

        if (path == "deepslate_coal_ore") return 17;

        if (path == "quartz_ore" || path == "nether_quartz_ore") return 18;

        return 100;

        //
        // ========================================================
        // RESOURCE BLOCKS
        // ========================================================
        //

    case GroupKind::ResourceBlocks:
        if (path == "netherite_block") return 0;

        if (path == "diamond_block") return 1;

        if (path == "emerald_block") return 2;

        if (path == "gold_block") return 3;

        if (path == "raw_gold_block") return 4;

        if (path == "iron_block") return 5;

        if (path == "raw_iron_block") return 6;

        if (path == "redstone_block") return 7;

        if (path == "lapis_block") return 8;

        if (path == "coal_block") return 9;

        return 100;

        //
        // ========================================================
        // STORAGE
        // ========================================================
        //

    case GroupKind::Storage:
        if (path == "chest") return 0;

        if (path == "trapped_chest") return 1;

        if (path == "ender_chest") return 2;

        if (path == "barrel") return 3;

        if (path == "shulker_box") return 10;

        if (path.ends_with("_shulker_box")) return 11 + colorPriority(path);

        return 100;

        //
        // ========================================================
        // STONE
        // ========================================================
        //

    case GroupKind::Stone:
        if (path == "stone") return 0;

        if (path == "deepslate") return 1;

        if (path == "cobblestone") return 2;

        if (path == "cobbled_deepslate") return 3;

        if (path == "tuff") return 4;

        if (path == "calcite") return 5;

        if (path == "dripstone_block") return 6;

        if (path == "pointed_dripstone") return 7;

        if (path == "granite") return 10;

        if (path == "diorite") return 11;

        if (path == "andesite") return 12;

        if (path == "polished_granite") return 13;

        if (path == "polished_diorite") return 14;

        if (path == "polished_andesite") return 15;

        if (path.find("stairs") != std::string_view::npos) return 50;

        if (path.find("slab") != std::string_view::npos) return 51;

        if (path.find("wall") != std::string_view::npos) return 52;

        return 100;

        //
        // ========================================================
        // GROUND
        // ========================================================
        //

    case GroupKind::Ground:
        if (path == "grass_block") return 0;

        if (path == "dirt") return 1;

        if (path == "coarse_dirt") return 2;

        if (path == "rooted_dirt") return 3;

        if (path == "dirt_path") return 4;

        if (path == "podzol") return 5;

        if (path == "mycelium") return 6;

        if (path == "sand") return 10;

        if (path == "red_sand") return 11;

        if (path == "gravel") return 12;

        if (path == "clay") return 13;

        if (path == "mud") return 20;

        if (path == "packed_mud") return 21;

        if (path == "snow") return 30;

        if (path == "ice") return 40;

        if (path == "packed_ice") return 41;

        if (path == "blue_ice") return 42;

        return 100;

        //
        // ========================================================
        // WOOD
        // ========================================================
        //

    case GroupKind::Wood: {
        std::string family(group.key);

        if (path == family + "_log") return 0;

        if (path == family + "_wood") return 1;

        if (path == "stripped_" + family + "_log") return 2;

        if (path == "stripped_" + family + "_wood") return 3;

        if (path == family + "_block") return 0;

        if (path == "stripped_" + family + "_block") return 2;

        if (path == family + "_planks") return 4;

        if (path.find("mosaic") != std::string_view::npos) return 5;

        if (path == family + "_leaves") return 6;

        if (path == family + "_sapling") return 7;

        if (path == family + "_stairs") return 10;

        if (path == family + "_slab") return 11;

        if (path == family + "_fence") return 12;

        if (path == family + "_fence_gate") return 13;

        if (path == family + "_door") return 14;

        if (path == family + "_trapdoor") return 15;

        if (path == family + "_sign") return 16;

        if (path == family + "_hanging_sign") return 17;

        if (path == family + "_shelf") return 18;

        if (path == family + "_button") return 30;

        if (path == family + "_pressure_plate") return 31;

        return 100;
    }

        //
        // ========================================================
        // PLANTS
        // ========================================================
        //

    case GroupKind::Plants:
        if (path == "dandelion") return 0;

        if (path == "poppy") return 1;

        if (path == "blue_orchid") return 2;

        if (path == "allium") return 3;

        if (path.find("tulip") != std::string_view::npos) return 4;

        if (path == "sunflower") return 10;

        if (path == "short_grass" || path == "tallgrass") return 20;

        if (path == "fern") return 21;

        if (path == "vine" || path == "vines") return 30;

        if (path.find("cave_vines") != std::string_view::npos) return 31;

        if (path == "moss_block") return 40;

        if (path == "moss_carpet") return 41;

        if (path.find("azalea") != std::string_view::npos) return 50;

        return 100;

        //
        // ========================================================
        // FARMING
        // ========================================================
        //

    case GroupKind::Farming:
        if (path == "wheat") return 0;

        if (path == "carrots") return 1;

        if (path == "potatoes") return 2;

        if (path == "beetroot") return 3;

        if (path == "melon") return 10;

        if (path == "pumpkin") return 11;

        if (path == "cocoa") return 20;

        if (path == "sugar_cane") return 21;

        if (path == "cactus") return 22;

        if (path == "farmland") return 30;

        return 100;

        //
        // ========================================================
        // NETHER
        // ========================================================
        //

    case GroupKind::Nether:
        if (path == "netherrack") return 0;

        if (path == "soul_sand") return 1;

        if (path == "soul_soil") return 2;

        if (path == "magma" || path == "magma_block") return 3;

        if (path == "glowstone") return 4;

        if (path.find("basalt") != std::string_view::npos) return 10;

        if (path.find("blackstone") != std::string_view::npos) return 11;

        if (path.find("nether_brick") != std::string_view::npos) return 12;

        if (path.starts_with("crimson_")) return 20;

        if (path.starts_with("warped_")) return 21;

        if (path == "nether_wart") return 30;

        return 100;

        //
        // ========================================================
        // END
        // ========================================================
        //

    case GroupKind::End:
        if (path == "end_stone") return 0;

        if (path.find("end_stone_brick") != std::string_view::npos) return 1;

        if (path.find("purpur") != std::string_view::npos) return 10;

        if (path == "chorus_plant") return 20;

        if (path == "chorus_flower") return 21;

        if (path == "end_rod") return 30;

        return 100;

        //
        // ========================================================
        // OCEAN
        // ========================================================
        //

    case GroupKind::Ocean:
        if (path == "water") return 0;

        if (path.find("kelp") != std::string_view::npos) return 10;

        if (path == "seagrass") return 11;

        if (path.find("coral") != std::string_view::npos) return 20;

        if (path == "sponge") return 30;

        if (path == "wet_sponge") return 31;

        if (path == "conduit") return 40;

        return 100;

        //
        // ========================================================
        // AMETHYST
        // ========================================================
        //

    case GroupKind::Amethyst:
        if (path == "amethyst_block" || path == "block_of_amethyst") return 0;

        if (path == "budding_amethyst") return 1;

        if (path == "amethyst_cluster") return 2;

        if (path == "large_amethyst_bud") return 3;

        if (path == "medium_amethyst_bud") return 4;

        if (path == "small_amethyst_bud") return 5;

        return 100;

        //
        // ========================================================
        // DEEP DARK
        // ========================================================
        //

    case GroupKind::Sculk:
        if (path == "sculk") return 0;

        if (path == "sculk_sensor") return 1;

        if (path == "calibrated_sculk_sensor") return 2;

        if (path == "sculk_shrieker") return 3;

        if (path == "sculk_catalyst") return 4;

        if (path == "sculk_vein") return 5;

        if (path == "reinforced_deepslate") return 10;

        return 100;

        //
        // ========================================================
        // COPPER
        // ========================================================
        //

    case GroupKind::Copper:
        if (path == "copper_block") return 0;

        if (path == "raw_copper_block") return 1;

        if (path.find("cut_copper") != std::string_view::npos) return 10;

        if (path.find("chiseled") != std::string_view::npos) return 20;

        if (path.find("stairs") != std::string_view::npos) return 30;

        if (path.find("slab") != std::string_view::npos) return 31;

        if (path.find("grate") != std::string_view::npos) return 40;

        if (path.find("bulb") != std::string_view::npos) return 41;

        if (path.find("door") != std::string_view::npos && path.find("trapdoor") == std::string_view::npos) return 50;

        if (path.find("trapdoor") != std::string_view::npos) return 51;

        if (path == "lightning_rod") return 60;

        return 100;

        //
        // ========================================================
        // COLOR FAMILIES
        // ========================================================
        //

    case GroupKind::Wool:
        if (path == "wool" || path.ends_with("_wool")) return colorPriority(path);

        return 100 + colorPriority(path);

    case GroupKind::Concrete:
        if (path.find("concrete_powder") == std::string_view::npos) return colorPriority(path);

        return 100 + colorPriority(path);

    case GroupKind::Terracotta:
        if (path == "terracotta") return 0;

        if (path.find("glazed_terracotta") != std::string_view::npos) return 100 + colorPriority(path);

        return 10 + colorPriority(path);

    case GroupKind::Glass:
        if (path == "glass") return 0;

        if (path == "glass_pane") return 1;

        if (path.find("pane") == std::string_view::npos) return 10 + colorPriority(path);

        return 100 + colorPriority(path);

        //
        // ========================================================
        // LIGHTING
        // ========================================================
        //

    case GroupKind::Lighting:
        if (path == "torch") return 0;

        if (path == "lantern") return 1;

        if (path == "campfire") return 2;

        if (path == "jack_o_lantern") return 3;

        if (path.find("froglight") != std::string_view::npos) return 4;

        if (path == "candle") return 10;

        if (path.ends_with("_candle")) return 11 + colorPriority(path);

        return 100;

        //
        // ========================================================
        // REDSTONE
        // ========================================================
        //

    case GroupKind::Redstone:
        if (path == "redstone_wire") return 0;

        if (path == "observer") return 1;

        if (path == "piston") return 2;

        if (path == "sticky_piston") return 3;

        if (path == "hopper") return 4;

        if (path == "dispenser") return 5;

        if (path == "dropper") return 6;

        if (path == "crafter") return 7;

        if (path.find("repeater") != std::string_view::npos) return 10;

        if (path.find("comparator") != std::string_view::npos) return 11;

        if (path == "lever") return 12;

        if (path == "target") return 13;

        if (path.ends_with("_button")) return 50;

        if (path.ends_with("_pressure_plate")) return 51;

        return 100;

        //
        // ========================================================
        // TRANSPORT
        // ========================================================
        //

    case GroupKind::Transport:
        if (path == "rail") return 0;

        if (path == "powered_rail") return 1;

        if (path == "detector_rail") return 2;

        if (path == "activator_rail") return 3;

        return 100;

        //
        // ========================================================
        // WORKSTATIONS
        // ========================================================
        //

    case GroupKind::Utility:
        if (path == "crafting_table") return 0;

        if (path == "furnace") return 1;

        if (path == "blast_furnace") return 2;

        if (path == "smoker") return 3;

        if (path == "enchanting_table") return 4;

        if (path == "brewing_stand") return 5;

        if (path == "anvil") return 6;

        if (path == "grindstone") return 7;

        if (path == "smithing_table") return 8;

        if (path == "stonecutter") return 9;

        if (path == "cartography_table") return 10;

        if (path == "fletching_table") return 11;

        if (path == "loom") return 12;

        if (path == "lectern") return 13;

        return 100;

        //
        // ========================================================
        // DOORS & BARRIERS
        // ========================================================
        //

    case GroupKind::DoorsBarriers:
        if (path == "iron_door") return 0;

        if (path == "iron_trapdoor") return 1;

        if (path == "iron_bars") return 2;

        if (path == "chain") return 3;

        return 100;

        //
        // ========================================================
        // SPECIAL
        // ========================================================
        //

    case GroupKind::Special:
        if (path == "mob_spawner" || path == "spawner") return 0;

        if (path == "trial_spawner") return 1;

        if (path == "vault") return 2;

        if (path == "suspicious_sand") return 10;

        if (path == "suspicious_gravel") return 11;

        if (path == "decorated_pot") return 12;

        if (path == "bell") return 20;

        if (path == "flower_pot") return 21;

        if (path.find("banner") != std::string_view::npos) return 30;

        if (path.find("skull") != std::string_view::npos || path.find("_head") != std::string_view::npos) return 31;

        return 100;

        //
        // ========================================================
        // OTHER
        // ========================================================
        //

    case GroupKind::Other:
        return 100;
    }

    return 100;
}

//
// ====================================================================
// CONSTRUCTOR
// ====================================================================
//

XRayBlockListScreen::XRayBlockListScreen() {
    Eventing::get().listen<RenderOverlayEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onRender, 1, true);

    Eventing::get().listen<ClickEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onClick, 1);

    Eventing::get().listen<CharEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onChar, 2);

    Eventing::get().listen<KeyUpdateEvent>(this, (EventListenerFunc)&XRayBlockListScreen::onKey, 2);
}

void XRayBlockListScreen::onEnable(bool) {
    scroll = 0.0f;
    lerpScroll = 0.0f;
    scrollMax = 0.0f;

    selectedOnly = false;
    groupedMode = true;
    showTechnicalBlocks = false;

    layoutDropdownOpen = false;

    scrollbarDragging = false;
    scrollbarDragOffset = 0.0f;

    expandedFamilies.clear();

    Nexus::NexusConfig::load();

    Nexus::NexusConfig::blockListColumns = std::clamp(Nexus::NexusConfig::blockListColumns, 1, 3);

    searchBox.reset();
    searchBox.setSelected(false);

    resetInputState();
}

void XRayBlockListScreen::onDisable() {
    Nexus::NexusConfig::save();

    layoutDropdownOpen = false;

    scrollbarDragging = false;

    searchBox.setSelected(false);

    resetInputState();
}

//
// ====================================================================
// WHEEL
// ====================================================================
//

void XRayBlockListScreen::onClick(Event& event) {
    if (!isActive()) {
        return;
    }

    auto& clickEvent = reinterpret_cast<ClickEvent&>(event);

    if (clickEvent.getClickType() != ClickEvent::ClickType::Wheel) {
        return;
    }

    auto client = SDK::ClientInstance::get();

    if (!client) {
        return;
    }

    auto& cursorPos = client->cursorPos;

    bool overList =
        cursorPos.x >= listLeft && cursorPos.x <= listRight && cursorPos.y >= listTop && cursorPos.y <= listBottom;

    if (!overList) {
        return;
    }

    scroll = std::clamp(scroll - static_cast<float>(clickEvent.getWheelDelta()) / 3.0f, 0.0f, scrollMax);

    clickEvent.setCancelled(true);
}

//
// ====================================================================
// TEXT INPUT
// ====================================================================
//

void XRayBlockListScreen::onChar(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& charEvent = reinterpret_cast<CharEvent&>(event);

    if (!charEvent.isChar()) {
        return;
    }

    std::wstring oldText = searchBox.getText();

    searchBox.onChar(charEvent.getChar());

    if (oldText != searchBox.getText()) {
        scroll = 0.0f;
        lerpScroll = 0.0f;
    }

    charEvent.setCancelled(true);
}

void XRayBlockListScreen::onKey(Event& event) {
    if (!isActive() || !searchBox.isSelected()) {
        return;
    }

    auto& keyEvent = reinterpret_cast<KeyUpdateEvent&>(event);

    if (!keyEvent.isDown()) {
        return;
    }

    int pressedKey = keyEvent.getKey();

    if (pressedKey == VK_ESCAPE) {
        searchBox.reset();
        searchBox.setSelected(false);

        scroll = 0.0f;
        lerpScroll = 0.0f;

        keyEvent.setCancelled(true);

        return;
    }

    if (pressedKey == VK_LEFT || pressedKey == VK_RIGHT) {
        searchBox.onKeyDown(pressedKey);
    }

    keyEvent.setCancelled(true);
}

//
// ====================================================================
// RENDER
// ====================================================================
//

void XRayBlockListScreen::onRender(Event&) {
    if (!isActive()) {
        return;
    }

    using namespace Nexus;

    auto client = SDK::ClientInstance::get();

    if (!client) {
        return;
    }

    D2DUtil dc;

    auto screenSize = Latite::getRenderer().getScreenSize();

    auto& cursorPos = client->cursorPos;

    cursor = Cursor::Arrow;

    float scale = std::clamp(screenSize.width / 1920.0f, 0.72f, 1.10f);

    float panelWidth = std::min(screenSize.width * 0.92f, 1180.0f * scale);

    float panelHeight = std::min(screenSize.height * 0.94f, 900.0f * scale);

    d2d::Rect panelRect = { (screenSize.width - panelWidth) * 0.5f, (screenSize.height - panelHeight) * 0.5f,
                            (screenSize.width + panelWidth) * 0.5f, (screenSize.height + panelHeight) * 0.5f };

    float padding = 26.0f * scale;

    dc.fillRoundedRectangle(panelRect, d2d::Color::RGB(0x0B, 0x0B, 0x0B).asAlpha(0.95f), 18.0f * scale);

    dc.drawRoundedRectangle(panelRect, d2d::Color::RGB(0x45, 0x45, 0x45).asAlpha(0.75f), 18.0f * scale, 1.5f * scale);

    //
    // ============================================================
    // TITLE
    // ============================================================
    //

    d2d::Rect titleRect = { panelRect.left + padding, panelRect.top + 10.0f * scale, panelRect.right - padding,
                            panelRect.top + 48.0f * scale };

    dc.drawText(titleRect, L"Block List", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryLight, 27.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect subtitleRect = { panelRect.left + padding, panelRect.top + 42.0f * scale, panelRect.right - padding,
                               panelRect.top + 66.0f * scale };

    dc.drawText(subtitleRect, L"Choose which blocks X-Ray should highlight", d2d::Color::RGB(0x9A, 0x9A, 0x9A),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // CATALOG
    // ============================================================
    //

    struct BlockEntry {
        std::wstring name;
        std::wstring ids;
        std::string namespacedId;
    };

    std::vector<BlockEntry> entries;

    const auto& catalog = XRayBlockCatalog::getEntries();

    entries.reserve(catalog.size());

    for (const auto& item : catalog) {
        entries.push_back({ item.displayName,

                            std::wstring(item.namespacedId.begin(), item.namespacedId.end()),

                            item.namespacedId });
    }

    //
    // Total selected count includes technical blocks too.
    //
    int selectedCount = 0;

    for (const auto& entry : entries) {
        if (XRayTargets::isSelected(entry.namespacedId)) {
            ++selectedCount;
        }
    }

    d2d::Rect selectedRect = { panelRect.right - padding - 180.0f * scale,

                               titleRect.top,

                               panelRect.right - padding,

                               titleRect.bottom };

    dc.drawText(selectedRect, std::to_wstring(selectedCount) + L" selected", d2d::Color::RGB(0x92, 0x92, 0x92),
                Renderer::FontSelection::PrimaryRegular, 12.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    //
    // ============================================================
    // SEARCH BOX
    // ============================================================
    //

    d2d::Rect searchRect = { panelRect.left + padding, panelRect.top + 78.0f * scale, panelRect.right - padding,
                             panelRect.top + 112.0f * scale };

    bool hasSearch = !searchBox.getText().empty();

    d2d::Rect searchTextRect = { searchRect.left + 11.0f * scale, searchRect.top + 4.0f * scale,
                                 searchRect.right - (hasSearch ? 36.0f * scale : 10.0f * scale),
                                 searchRect.bottom - 4.0f * scale };

    searchBox.setRect(searchTextRect);

    d2d::Rect clearRect = { searchRect.right - 29.0f * scale, searchRect.top + 5.0f * scale,
                            searchRect.right - 6.0f * scale, searchRect.bottom - 5.0f * scale };

    bool searchHovered = shouldSelect(searchRect, cursorPos);

    bool clearHovered = hasSearch && shouldSelect(clearRect, cursorPos);

    if (clearHovered) {
        cursor = Cursor::Hand;
    }

    else if (searchHovered) {
        cursor = Cursor::IBeam;
    }

    if (justClicked[0]) {
        if (clearHovered) {
            searchBox.reset();
            searchBox.setSelected(true);

            scroll = 0.0f;
            lerpScroll = 0.0f;

            playClickSound();
        }

        else {
            searchBox.setSelected(searchHovered);
        }
    }

    d2d::Color searchBackground = searchBox.isSelected() ? d2d::Color::RGB(0x24, 0x24, 0x24)
                                  : searchHovered        ? d2d::Color::RGB(0x20, 0x20, 0x20)
                                                         : d2d::Color::RGB(0x17, 0x17, 0x17);

    dc.fillRoundedRectangle(searchRect, searchBackground, 8.0f * scale);

    dc.drawRoundedRectangle(
        searchRect, searchBox.isSelected() ? d2d::Color::RGB(0x42, 0x78, 0xA8) : d2d::Color::RGB(0x48, 0x48, 0x48),
        8.0f * scale, searchBox.isSelected() ? 1.5f * scale : 1.0f * scale);

    searchBox.render(dc, 0.0f, d2d::Color::RGB(0, 0, 0).asAlpha(0.0f), d2d::Colors::WHITE,
                     DWRITE_TEXT_ALIGNMENT_LEADING);

    if (searchBox.getText().empty() && !searchBox.isSelected()) {
        dc.drawText(searchTextRect, L"Search blocks...", d2d::Color::RGB(0x82, 0x82, 0x82),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    if (hasSearch) {
        dc.fillRoundedRectangle(clearRect,
                                clearHovered ? d2d::Color::RGB(0x45, 0x45, 0x45) : d2d::Color::RGB(0x2A, 0x2A, 0x2A),
                                6.0f * scale);

        dc.drawText(clearRect, L"X", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    std::wstring rawSearch = lowerText(searchBox.getText());

    std::wstring normalizedSearch = normalizeSearchText(searchBox.getText());

    auto matchesSearch = [&](const BlockEntry& entry) -> bool {
        if (rawSearch.empty()) {
            return true;
        }

        std::wstring nameRaw = lowerText(entry.name);

        std::wstring idRaw = lowerText(entry.ids);

        std::wstring nameNormalized = normalizeSearchText(entry.name);

        std::wstring idNormalized = normalizeSearchText(entry.ids);

        return nameRaw.find(rawSearch) != std::wstring::npos ||

               idRaw.find(rawSearch) != std::wstring::npos ||

               (!normalizedSearch.empty() && (nameNormalized.find(normalizedSearch) != std::wstring::npos ||

                                              idNormalized.find(normalizedSearch) != std::wstring::npos));
    };

    //
    // ============================================================
    // AVAILABLE ENTRIES
    // ============================================================
    //

    std::vector<BlockEntry*> availableEntries;

    availableEntries.reserve(entries.size());

    for (auto& entry : entries) {
        if (!showTechnicalBlocks && isTechnicalBlockId(entry.namespacedId)) {
            continue;
        }

        availableEntries.push_back(&entry);
    }

    //
    // ============================================================
    // BATCH SCOPE
    // ============================================================
    //

    std::vector<BlockEntry*> batchEntries;

    for (BlockEntry* entry : availableEntries) {
        if (!entry) {
            continue;
        }

        if (matchesSearch(*entry)) {
            batchEntries.push_back(entry);
        }
    }

    //
    // ============================================================
    // TOP BUTTONS
    // ============================================================
    //

    float helperTop = searchRect.bottom + 8.0f * scale;

    float helperHeight = 26.0f * scale;

    float helperGap = 7.0f * scale;

    float helperWidth = 95.0f * scale;

    d2d::Rect allOnRect = { searchRect.left, helperTop, searchRect.left + helperWidth, helperTop + helperHeight };

    d2d::Rect allOffRect = { allOnRect.right + helperGap, helperTop, allOnRect.right + helperGap + helperWidth,
                             helperTop + helperHeight };

    d2d::Rect autoOresRect = { allOffRect.right + helperGap, helperTop, allOffRect.right + helperGap + 125.0f * scale,
                               helperTop + helperHeight };

    d2d::Rect selectedOnlyRect = { autoOresRect.right + helperGap, helperTop,
                                   autoOresRect.right + helperGap + 125.0f * scale, helperTop + helperHeight };

    d2d::Rect groupedRect = { selectedOnlyRect.right + helperGap, helperTop,
                              selectedOnlyRect.right + helperGap + 95.0f * scale, helperTop + helperHeight };

    d2d::Rect technicalRect = { groupedRect.right + helperGap, helperTop,
                                groupedRect.right + helperGap + 105.0f * scale, helperTop + helperHeight };

    float layoutWidth = 160.0f * scale;

    d2d::Rect layoutSelectorRect = { searchRect.right - layoutWidth, helperTop, searchRect.right,
                                     helperTop + helperHeight };

    auto drawButton = [&](const d2d::Rect& rect, const std::wstring& label, bool selected = false) -> bool {
        bool hovering = shouldSelect(rect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        d2d::Color background = selected   ? d2d::Color::RGB(0x34, 0x5E, 0x82)
                                : hovering ? d2d::Color::RGB(0x2D, 0x2D, 0x2D)
                                           : d2d::Color::RGB(0x1C, 0x1C, 0x1C);

        dc.fillRoundedRectangle(rect, background, 7.0f * scale);

        dc.drawRoundedRectangle(rect, selected ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                                7.0f * scale, 1.0f * scale);

        dc.drawText(rect, label, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        return hovering && justClicked[0];
    };

    if (drawButton(allOnRect, L"All On")) {
        bool changed = false;

        for (BlockEntry* entry : batchEntries) {
            changed |= XRayTargets::setSelected(entry->namespacedId, true);
        }

        if (changed) {
            NexusConfig::save();
        }

        playClickSound();
    }

    if (drawButton(allOffRect, L"All Off")) {
        bool changed = false;

        for (BlockEntry* entry : batchEntries) {
            changed |= XRayTargets::setSelected(entry->namespacedId, false);
        }

        if (changed) {
            NexusConfig::save();
        }

        playClickSound();
    }

    if (drawButton(autoOresRect, L"Auto Add Ores")) {
        bool changed = false;

        for (BlockEntry* entry : batchEntries) {
            if (!isAutoOreId(entry->namespacedId)) {
                continue;
            }

            changed |= XRayTargets::setSelected(entry->namespacedId, true);
        }

        if (changed) {
            NexusConfig::save();
        }

        playClickSound();
    }

    if (drawButton(selectedOnlyRect, L"Selected Only", selectedOnly)) {
        selectedOnly = !selectedOnly;

        scroll = 0.0f;
        lerpScroll = 0.0f;

        playClickSound();
    }

    if (drawButton(groupedRect, L"Grouped", groupedMode)) {
        groupedMode = !groupedMode;

        scroll = 0.0f;
        lerpScroll = 0.0f;

        playClickSound();
    }

    if (drawButton(technicalRect, L"Technical", showTechnicalBlocks)) {
        showTechnicalBlocks = !showTechnicalBlocks;

        scroll = 0.0f;
        lerpScroll = 0.0f;

        playClickSound();
    }

    //
    // ============================================================
    // LAYOUT DROPDOWN
    // ============================================================
    //

    int columnCount = std::clamp(NexusConfig::blockListColumns, 1, 3);

    std::wstring layoutLabel = columnCount == 1 ? L"List" : columnCount == 2 ? L"Grid" : L"Compact Grid";

    bool layoutSelectorHovered = shouldSelect(layoutSelectorRect, cursorPos);

    if (layoutSelectorHovered) {
        cursor = Cursor::Hand;
    }

    dc.fillRoundedRectangle(
        layoutSelectorRect,
        layoutSelectorHovered ? d2d::Color::RGB(0x2D, 0x2D, 0x2D) : d2d::Color::RGB(0x1C, 0x1C, 0x1C), 7.0f * scale);

    dc.drawRoundedRectangle(layoutSelectorRect,
                            layoutDropdownOpen ? d2d::Color::RGB(0x62, 0x92, 0xBC) : d2d::Color::RGB(0x48, 0x48, 0x48),
                            7.0f * scale, 1.0f * scale);

    d2d::Rect layoutTextRect = { layoutSelectorRect.left + 10.0f * scale, layoutSelectorRect.top,
                                 layoutSelectorRect.right - 28.0f * scale, layoutSelectorRect.bottom };

    dc.drawText(layoutTextRect, layoutLabel, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 11.0f * scale,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    d2d::Rect layoutArrowRect = { layoutSelectorRect.right - 28.0f * scale, layoutSelectorRect.top,
                                  layoutSelectorRect.right - 5.0f * scale, layoutSelectorRect.bottom };

    dc.drawText(layoutArrowRect, layoutDropdownOpen ? L"\u25B4" : L"\u25BE", d2d::Color::RGB(0xC8, 0xC8, 0xC8),
                Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    bool layoutConsumedClick = false;

    if (layoutSelectorHovered && justClicked[0]) {
        layoutDropdownOpen = !layoutDropdownOpen;

        layoutConsumedClick = true;

        playClickSound();
    }

    float layoutOptionHeight = 28.0f * scale;

    float layoutOptionGap = 3.0f * scale;

    float layoutMenuTop = layoutSelectorRect.bottom + 4.0f * scale;

    d2d::Rect layoutMenuRect = { layoutSelectorRect.left, layoutMenuTop, layoutSelectorRect.right,

                                 layoutMenuTop + layoutOptionHeight * 3.0f + layoutOptionGap * 2.0f };

    d2d::Rect layoutOptionRects[3];

    for (int i = 0; i < 3; ++i) {
        float top = layoutMenuTop + static_cast<float>(i) * (layoutOptionHeight + layoutOptionGap);

        layoutOptionRects[i] = { layoutMenuRect.left, top, layoutMenuRect.right, top + layoutOptionHeight };
    }

    if (layoutDropdownOpen) {
        for (int i = 0; i < 3; ++i) {
            bool hovering = shouldSelect(layoutOptionRects[i], cursorPos);

            if (hovering) {
                cursor = Cursor::Hand;
            }

            if (hovering && justClicked[0] && !layoutConsumedClick) {
                int newColumns = i + 1;

                NexusConfig::blockListColumns = newColumns;

                NexusConfig::save();

                columnCount = newColumns;

                scroll = 0.0f;
                lerpScroll = 0.0f;

                layoutDropdownOpen = false;
                layoutConsumedClick = true;

                playClickSound();

                break;
            }
        }
    }

    //
    // ============================================================
    // FLAT RESULTS
    // ============================================================
    //

    std::vector<BlockEntry*> visibleEntries;

    for (BlockEntry* entry : availableEntries) {
        if (!entry || !matchesSearch(*entry)) {
            continue;
        }

        bool selected = XRayTargets::isSelected(entry->namespacedId);

        if (selectedOnly && !selected) {
            continue;
        }

        visibleEntries.push_back(entry);
    }

    std::sort(visibleEntries.begin(), visibleEntries.end(), [](const BlockEntry* a, const BlockEntry* b) {
        return lowerText(a->name) < lowerText(b->name);
    });

    //
    // ============================================================
    // BUILD GROUPS
    // ============================================================
    //

    struct GroupView {
        const GroupDefinition* definition = nullptr;

        std::string key;
        std::wstring name;

        std::vector<BlockEntry*> allMembers;
        std::vector<BlockEntry*> visibleMembers;

        int selectedCount = 0;

        bool expanded = false;
        bool nameMatchesSearch = false;
    };

    std::vector<GroupView> groups;

    for (const auto& definition : GroupDefinitions) {
        groups.push_back(
            { &definition, std::string(definition.key), std::wstring(definition.name.begin(), definition.name.end()) });
    }

    //
    // First matching category wins.
    //
    for (BlockEntry* entry : availableEntries) {
        if (!entry) {
            continue;
        }

        for (auto& group : groups) {
            if (!group.definition) {
                continue;
            }

            if (!belongsToGroup(entry->namespacedId, *group.definition)) {
                continue;
            }

            group.allMembers.push_back(entry);

            break;
        }
    }

    for (auto& group : groups) {
        if (!group.definition) {
            continue;
        }

        std::sort(group.allMembers.begin(), group.allMembers.end(), [&](const BlockEntry* a, const BlockEntry* b) {
            int pa = groupPriority(blockPath(a->namespacedId), *group.definition);

            int pb = groupPriority(blockPath(b->namespacedId), *group.definition);

            if (pa != pb) {
                return pa < pb;
            }

            return lowerText(a->name) < lowerText(b->name);
        });

        if (!rawSearch.empty()) {
            std::wstring groupName = lowerText(group.name);

            std::wstring normalizedGroup = normalizeSearchText(group.name);

            group.nameMatchesSearch =
                groupName.find(rawSearch) != std::wstring::npos ||

                (!normalizedSearch.empty() && normalizedGroup.find(normalizedSearch) != std::wstring::npos);
        }

        for (BlockEntry* member : group.allMembers) {
            bool selected = XRayTargets::isSelected(member->namespacedId);

            if (selected) {
                ++group.selectedCount;
            }

            if (selectedOnly && !selected) {
                continue;
            }

            if (rawSearch.empty() || group.nameMatchesSearch || matchesSearch(*member)) {
                group.visibleMembers.push_back(member);
            }
        }

        bool searchOpen = !rawSearch.empty() && !group.visibleMembers.empty();

        group.expanded = searchOpen || expandedFamilies.contains(group.key);
    }

    //
    // ============================================================
    // LIST
    // ============================================================
    //

    d2d::Rect listRect = { panelRect.left + padding,

                           helperTop + helperHeight + 10.0f * scale,

                           panelRect.right - padding,

                           panelRect.bottom - 70.0f * scale };

    listLeft = listRect.left;
    listTop = listRect.top;
    listRight = listRect.right;
    listBottom = listRect.bottom;

    dc.fillRoundedRectangle(listRect, d2d::Color::RGB(0x09, 0x09, 0x09).asAlpha(0.55f), 10.0f * scale);

    dc.drawRoundedRectangle(listRect, d2d::Color::RGB(0x36, 0x36, 0x36), 10.0f * scale, 1.0f * scale);

    float innerPadding = 6.0f * scale;

    float rowHeight = 54.0f * scale;

    float rowGap = 6.0f * scale;

    float columnGap = 6.0f * scale;

    //
    // More room reserved for scrollbar interaction.
    //
    float scrollbarSpace = 22.0f * scale;

    float familyHeaderHeight = 42.0f * scale;

    float familyGap = 6.0f * scale;

    float sectionGap = 10.0f * scale;

    float childIndent = 16.0f * scale;

    float usableWidth = listRect.getWidth() - innerPadding * 2.0f - scrollbarSpace;

    float cardWidth = (usableWidth - columnGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount);

    float childUsableWidth = usableWidth - childIndent;

    float childCardWidth =
        (childUsableWidth - columnGap * static_cast<float>(columnCount - 1)) / static_cast<float>(columnCount);

    //
    // ============================================================
    // CONTENT HEIGHT
    // ============================================================
    //

    float contentHeight = innerPadding;

    bool hasContent = false;

    if (!groupedMode) {
        std::size_t rows = visibleEntries.empty()
                               ? 0
                               : (visibleEntries.size() + static_cast<std::size_t>(columnCount) - 1) /
                                     static_cast<std::size_t>(columnCount);

        if (rows > 0) {
            hasContent = true;

            contentHeight += static_cast<float>(rows) * rowHeight;

            if (rows > 1) {
                contentHeight += static_cast<float>(rows - 1) * rowGap;
            }
        }
    }

    else {
        for (const auto& group : groups) {
            if (group.visibleMembers.empty()) {
                continue;
            }

            hasContent = true;

            contentHeight += familyHeaderHeight;

            if (group.expanded) {
                contentHeight += familyGap;

                std::size_t rows = (group.visibleMembers.size() + static_cast<std::size_t>(columnCount) - 1) /
                                   static_cast<std::size_t>(columnCount);

                contentHeight += static_cast<float>(rows) * rowHeight;

                if (rows > 1) {
                    contentHeight += static_cast<float>(rows - 1) * rowGap;
                }
            }

            contentHeight += sectionGap;
        }
    }

    contentHeight += innerPadding;

    scrollMax = std::max(0.0f, contentHeight - listRect.getHeight());

    scroll = std::clamp(scroll, 0.0f, scrollMax);

    if (!scrollbarDragging) {
        lerpScroll = std::lerp(lerpScroll, scroll, std::clamp(Latite::getRenderer().getDeltaTime() / 5.0f, 0.0f, 1.0f));
    }

    lerpScroll = std::clamp(lerpScroll, 0.0f, scrollMax);

    bool cursorOverLayoutMenu = layoutDropdownOpen && shouldSelect(layoutMenuRect, cursorPos);

    //
    // ============================================================
    // BLOCK CARD
    // ============================================================
    //

    auto drawBlockCard = [&](BlockEntry* entry, const d2d::Rect& rect) {
        if (!entry) {
            return;
        }

        if (rect.bottom < listRect.top || rect.top > listRect.bottom) {
            return;
        }

        bool hovering = !cursorOverLayoutMenu && listRect.contains(cursorPos) && shouldSelect(rect, cursorPos);

        if (hovering) {
            cursor = Cursor::Hand;
        }

        dc.fillRoundedRectangle(rect, hovering ? d2d::Color::RGB(0x25, 0x25, 0x25) : d2d::Color::RGB(0x17, 0x17, 0x17),
                                9.0f * scale);

        dc.drawRoundedRectangle(rect, d2d::Color::RGB(0x48, 0x48, 0x48).asAlpha(0.70f), 9.0f * scale, 1.0f * scale);

        float switchWidth = 48.0f * scale;

        float switchHeight = 22.0f * scale;

        d2d::Rect switchRect = { rect.right - switchWidth - 10.0f * scale,

                                 rect.center().y - switchHeight * 0.5f,

                                 rect.right - 10.0f * scale,

                                 rect.center().y + switchHeight * 0.5f };

        bool switchHovered = listRect.contains(cursorPos) && shouldSelect(switchRect, cursorPos);

        if (switchHovered) {
            cursor = Cursor::Hand;
        }

        bool value = XRayTargets::isSelected(entry->namespacedId);

        if (Nexus::UI::drawSwitch(dc, switchRect, value, switchHovered, justClicked[0] && !layoutConsumedClick,
                                  scale)) {
            if (XRayTargets::setSelected(entry->namespacedId, value)) {
                NexusConfig::save();
            }

            playClickSound();
        }

        d2d::Rect nameRect = { rect.left + 12.0f * scale, rect.top + 4.0f * scale, switchRect.left - 10.0f * scale,
                               rect.top + 29.0f * scale };

        dc.drawText(nameRect, entry->name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 14.0f * scale,
                    DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        d2d::Rect idRect = { rect.left + 12.0f * scale, rect.top + 26.0f * scale, switchRect.left - 10.0f * scale,
                             rect.bottom - 4.0f * scale };

        dc.drawText(idRect, entry->ids, d2d::Color::RGB(0x91, 0x91, 0x91), Renderer::FontSelection::PrimaryRegular,
                    10.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    //
    // ============================================================
    // CLIP
    // ============================================================
    //

    dc.ctx->PushAxisAlignedClip(listRect.get(), D2D1_ANTIALIAS_MODE_ALIASED);

    if (!hasContent) {
        dc.drawText(listRect, L"No matching blocks", d2d::Color::RGB(0x78, 0x78, 0x78),
                    Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    //
    // ============================================================
    // FLAT VIEW
    // ============================================================
    //

    if (!groupedMode) {
        for (std::size_t i = 0; i < visibleEntries.size(); ++i) {
            int column = static_cast<int>(i % static_cast<std::size_t>(columnCount));

            int row = static_cast<int>(i / static_cast<std::size_t>(columnCount));

            float left = listRect.left + innerPadding + static_cast<float>(column) * (cardWidth + columnGap);

            float top = listRect.top + innerPadding + static_cast<float>(row) * (rowHeight + rowGap) - lerpScroll;

            drawBlockCard(visibleEntries[i], { left, top, left + cardWidth, top + rowHeight });
        }
    }

    //
    // ============================================================
    // GROUPED VIEW
    // ============================================================
    //

    else {
        float y = listRect.top + innerPadding - lerpScroll;

        for (auto& group : groups) {
            if (group.visibleMembers.empty()) {
                continue;
            }

            d2d::Rect groupRect = { listRect.left + innerPadding,

                                    y,

                                    listRect.right - innerPadding - scrollbarSpace,

                                    y + familyHeaderHeight };

            bool headerVisible = groupRect.bottom >= listRect.top && groupRect.top <= listRect.bottom;

            float switchWidth = 48.0f * scale;

            float switchHeight = 22.0f * scale;

            d2d::Rect switchRect = { groupRect.right - switchWidth - 10.0f * scale,

                                     groupRect.center().y - switchHeight * 0.5f,

                                     groupRect.right - 10.0f * scale,

                                     groupRect.center().y + switchHeight * 0.5f };

            d2d::Rect countRect = { switchRect.left - 105.0f * scale,

                                    groupRect.top,

                                    switchRect.left - 8.0f * scale,

                                    groupRect.bottom };

            d2d::Rect arrowRect = { groupRect.left + 4.0f * scale,

                                    groupRect.top,

                                    groupRect.left + 32.0f * scale,

                                    groupRect.bottom };

            d2d::Rect nameRect = { arrowRect.right, groupRect.top, countRect.left - 8.0f * scale, groupRect.bottom };

            bool hovered = headerVisible && listRect.contains(cursorPos) && shouldSelect(groupRect, cursorPos);

            bool switchHovered = headerVisible && listRect.contains(cursorPos) && shouldSelect(switchRect, cursorPos);

            bool bodyHovered = hovered && !switchHovered;

            if (hovered) {
                cursor = Cursor::Hand;
            }

            if (headerVisible) {
                dc.fillRoundedRectangle(groupRect,
                                        hovered ? d2d::Color::RGB(0x26, 0x26, 0x26) : d2d::Color::RGB(0x1B, 0x1B, 0x1B),
                                        9.0f * scale);

                dc.drawRoundedRectangle(
                    groupRect, group.expanded ? d2d::Color::RGB(0x4D, 0x72, 0x94) : d2d::Color::RGB(0x48, 0x48, 0x48),
                    9.0f * scale, group.expanded ? 1.5f * scale : 1.0f * scale);

                dc.drawText(arrowRect, group.expanded ? L"\u25BE" : L"\u25B8", d2d::Color::RGB(0xC0, 0xC0, 0xC0),
                            Renderer::FontSelection::PrimaryRegular, 13.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER,
                            DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                dc.drawText(nameRect, group.name, d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                            14.0f * scale, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                dc.drawText(
                    countRect, std::to_wstring(group.selectedCount) + L" / " + std::to_wstring(group.allMembers.size()),
                    group.selectedCount > 0 ? d2d::Color::RGB(0xB8, 0xC9, 0xD8) : d2d::Color::RGB(0x84, 0x84, 0x84),
                    Renderer::FontSelection::PrimaryRegular, 11.0f * scale, DWRITE_TEXT_ALIGNMENT_TRAILING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                bool allSelected =
                    !group.allMembers.empty() && group.selectedCount == static_cast<int>(group.allMembers.size());

                bool groupValue = allSelected;

                if (Nexus::UI::drawSwitch(dc, switchRect, groupValue, switchHovered,
                                          justClicked[0] && !layoutConsumedClick, scale)) {
                    bool changed = false;

                    for (BlockEntry* member : group.allMembers) {
                        changed |= XRayTargets::setSelected(member->namespacedId, groupValue);
                    }

                    if (changed) {
                        NexusConfig::save();
                    }

                    playClickSound();
                }

                //
                // Partial-selection marker.
                //
                if (group.selectedCount > 0 && group.selectedCount < static_cast<int>(group.allMembers.size())) {
                    d2d::Rect partial = { switchRect.center().x - 5.0f * scale,

                                          switchRect.center().y - 1.0f * scale,

                                          switchRect.center().x + 5.0f * scale,

                                          switchRect.center().y + 1.0f * scale };

                    dc.fillRoundedRectangle(partial, d2d::Color::RGB(0xD8, 0xD8, 0xD8), 1.0f * scale);
                }

                //
                // Search automatically controls expansion while active.
                //
                if (bodyHovered && justClicked[0] && rawSearch.empty()) {
                    if (expandedFamilies.contains(group.key)) {
                        expandedFamilies.erase(group.key);
                    }

                    else {
                        expandedFamilies.insert(group.key);
                    }

                    playClickSound();
                }
            }

            y += familyHeaderHeight;

            if (group.expanded) {
                y += familyGap;

                std::size_t count = group.visibleMembers.size();

                std::size_t rows =
                    (count + static_cast<std::size_t>(columnCount) - 1) / static_cast<std::size_t>(columnCount);

                for (std::size_t i = 0; i < count; ++i) {
                    int column = static_cast<int>(i % static_cast<std::size_t>(columnCount));

                    int row = static_cast<int>(i / static_cast<std::size_t>(columnCount));

                    float left = listRect.left + innerPadding + childIndent +
                                 static_cast<float>(column) * (childCardWidth + columnGap);

                    float top = y + static_cast<float>(row) * (rowHeight + rowGap);

                    drawBlockCard(group.visibleMembers[i], { left, top, left + childCardWidth, top + rowHeight });
                }

                if (rows > 0) {
                    y += static_cast<float>(rows) * rowHeight;

                    if (rows > 1) {
                        y += static_cast<float>(rows - 1) * rowGap;
                    }
                }
            }

            y += sectionGap;
        }
    }

    dc.ctx->PopAxisAlignedClip();

    //
    // ============================================================
    // DRAGGABLE SCROLLBAR
    // ============================================================
    //

    if (scrollMax > 0.0f) {
        float trackWidth = 7.0f * scale;

        d2d::Rect trackRect = { listRect.right - 13.0f * scale,

                                listRect.top + 7.0f * scale,

                                listRect.right - 6.0f * scale,

                                listRect.bottom - 7.0f * scale };

        float visibleRatio = listRect.getHeight() / std::max(contentHeight, 1.0f);

        float thumbHeight = std::max(34.0f * scale, trackRect.getHeight() * visibleRatio);

        thumbHeight = std::min(thumbHeight, trackRect.getHeight());

        float usableTrack = std::max(0.0f, trackRect.getHeight() - thumbHeight);

        float percent = scrollMax > 0.0f ? lerpScroll / scrollMax : 0.0f;

        percent = std::clamp(percent, 0.0f, 1.0f);

        float thumbTop = trackRect.top + usableTrack * percent;

        d2d::Rect thumbRect = { trackRect.left, thumbTop, trackRect.right, thumbTop + thumbHeight };

        //
        // Large invisible interaction region.
        //
        d2d::Rect scrollbarHitRect = { listRect.right - 24.0f * scale, listRect.top, listRect.right, listRect.bottom };

        d2d::Rect thumbHitRect = { scrollbarHitRect.left, thumbRect.top - 4.0f * scale, scrollbarHitRect.right,
                                   thumbRect.bottom + 4.0f * scale };

        bool overScrollbar = !layoutDropdownOpen && shouldSelect(scrollbarHitRect, cursorPos);

        bool overThumb = !layoutDropdownOpen && shouldSelect(thumbHitRect, cursorPos);

        if (overScrollbar || scrollbarDragging) {
            cursor = Cursor::Hand;
        }

        if (overScrollbar && justClicked[0]) {
            scrollbarDragging = true;

            if (overThumb) {
                scrollbarDragOffset = cursorPos.y - thumbTop;
            }

            else {
                scrollbarDragOffset = thumbHeight * 0.5f;
            }
        }

        if (scrollbarDragging) {
            if (!mouseButtons[0]) {
                scrollbarDragging = false;
            }

            else {
                float desiredTop = cursorPos.y - scrollbarDragOffset;

                float dragPercent = usableTrack > 0.0f ? (desiredTop - trackRect.top) / usableTrack : 0.0f;

                dragPercent = std::clamp(dragPercent, 0.0f, 1.0f);

                scroll = dragPercent * scrollMax;

                //
                // Direct drag should not lag.
                //
                lerpScroll = scroll;
            }
        }

        dc.fillRoundedRectangle(trackRect,
                                overScrollbar ? d2d::Color::RGB(0x38, 0x38, 0x38) : d2d::Color::RGB(0x2D, 0x2D, 0x2D),
                                trackWidth * 0.5f);

        dc.fillRoundedRectangle(thumbRect,
                                overThumb || scrollbarDragging ? d2d::Color::RGB(0x9A, 0x9A, 0x9A)
                                                               : d2d::Color::RGB(0x72, 0x72, 0x72),
                                trackWidth * 0.5f);
    }

    else {
        scrollbarDragging = false;
    }

    //
    // ============================================================
    // DROPDOWN OVERLAY
    // ============================================================
    //

    if (layoutDropdownOpen) {
        dc.fillRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x10, 0x10, 0x10).asAlpha(0.98f), 8.0f * scale);

        dc.drawRoundedRectangle(layoutMenuRect, d2d::Color::RGB(0x4F, 0x4F, 0x4F), 8.0f * scale, 1.0f * scale);

        const wchar_t* names[3] = { L"List", L"Grid", L"Compact Grid" };

        for (int i = 0; i < 3; ++i) {
            bool selected = columnCount == i + 1;

            bool hovering = shouldSelect(layoutOptionRects[i], cursorPos);

            dc.fillRoundedRectangle(layoutOptionRects[i],
                                    selected   ? d2d::Color::RGB(0x34, 0x5E, 0x82)
                                    : hovering ? d2d::Color::RGB(0x2B, 0x2B, 0x2B)
                                               : d2d::Color::RGB(0x18, 0x18, 0x18),
                                    6.0f * scale);

            dc.drawText(layoutOptionRects[i], names[i], d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular,
                        11.0f * scale, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    //
    // ============================================================
    // BACK
    // ============================================================
    //

    d2d::Rect backRect = { panelRect.left + padding,

                           panelRect.bottom - 54.0f * scale,

                           panelRect.left + padding + 130.0f * scale,

                           panelRect.bottom - 16.0f * scale };

    bool backHovered = !layoutDropdownOpen && shouldSelect(backRect, cursorPos);

    if (backHovered) {
        cursor = Cursor::Hand;
    }

    dc.fillRoundedRectangle(
        backRect, backHovered ? d2d::Color::RGB(0x35, 0x35, 0x35) : d2d::Color::RGB(0x20, 0x20, 0x20), 9.0f * scale);

    dc.drawRoundedRectangle(backRect, d2d::Color::RGB(0x55, 0x55, 0x55), 9.0f * scale, 1.0f * scale);

    dc.drawText(backRect, L"< Back", d2d::Colors::WHITE, Renderer::FontSelection::PrimaryRegular, 15.0f * scale,
                DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (backHovered && justClicked[0]) {
        playClickSound();

        Latite::getScreenManager().showScreen<XRayScreen>(true);

        return;
    }
}
