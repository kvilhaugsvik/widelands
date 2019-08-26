/*
 * Copyright (C) 2002-2019 by the Widelands Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "editor/editorinteractive.h"

#include <memory>
#include <string>
#include <vector>

#include <SDL_keycode.h>
#include <boost/format.hpp>

#include "base/i18n.h"
#include "base/scoped_timer.h"
#include "base/warning.h"
#include "build_info.h"
#include "editor/tools/decrease_height_tool.h"
#include "editor/tools/decrease_resources_tool.h"
#include "editor/tools/increase_height_tool.h"
#include "editor/tools/increase_resources_tool.h"
#include "editor/tools/noise_height_tool.h"
#include "editor/tools/place_critter_tool.h"
#include "editor/tools/place_immovable_tool.h"
#include "editor/tools/set_port_space_tool.h"
#include "editor/tools/set_terrain_tool.h"
#include "editor/ui_menus/help.h"
#include "editor/ui_menus/main_menu_load_map.h"
#include "editor/ui_menus/main_menu_map_options.h"
#include "editor/ui_menus/main_menu_new_map.h"
#include "editor/ui_menus/main_menu_random_map.h"
#include "editor/ui_menus/main_menu_save_map.h"
#include "editor/ui_menus/player_menu.h"
#include "editor/ui_menus/scenario_lua.h"
#include "editor/ui_menus/scenario_tool_field_owner_options_menu.h"
#include "editor/ui_menus/scenario_tool_infrastructure_options_menu.h"
#include "editor/ui_menus/tool_change_height_options_menu.h"
#include "editor/ui_menus/tool_change_resources_options_menu.h"
#include "editor/ui_menus/tool_noise_height_options_menu.h"
#include "editor/ui_menus/tool_place_critter_options_menu.h"
#include "editor/ui_menus/tool_place_immovable_options_menu.h"
#include "editor/ui_menus/tool_resize_options_menu.h"
#include "editor/ui_menus/tool_set_terrain_options_menu.h"
#include "editor/ui_menus/toolsize_menu.h"
#include "graphic/game_renderer.h"
#include "graphic/graphic.h"
#include "graphic/playercolor.h"
#include "io/filewrite.h"
#include "logic/map.h"
#include "logic/map_objects/tribes/tribes.h"
#include "logic/map_objects/world/resource_description.h"
#include "logic/map_objects/world/world.h"
#include "logic/maptriangleregion.h"
#include "logic/player.h"
#include "logic/playersmanager.h"
#include "map_io/map_loader.h"
#include "map_io/widelands_map_loader.h"
#include "scripting/lua_interface.h"
#include "scripting/lua_table.h"
#include "ui_basic/messagebox.h"
#include "ui_basic/progresswindow.h"
#include "wui/game_tips.h"
#include "wui/interactive_base.h"

namespace {
using Widelands::Building;

// Load all tribes from disk.
void load_all_tribes(Widelands::EditorGameBase* egbase, UI::ProgressWindow* loader_ui) {
	loader_ui->step(_("Loading tribes"));
	egbase->tribes();
}

}  // namespace

EditorInteractive::EditorInteractive(Widelands::EditorGameBase& e)
   : InteractiveBase(e, g_options.pull_section("global")),
     need_save_(false),
     realtime_(SDL_GetTicks()),
     is_painting_(false),
     mainmenu_(toolbar(),
               "dropdown_menu_main",
               0,
               0,
               34U,
               10,
               34U,
               /** TRANSLATORS: Title for the main menu button in the editor */
               as_tooltip_text_with_hotkey(_("Main Menu"), "h"),
               UI::DropdownType::kPictorialMenu,
               UI::PanelStyle::kWui,
               UI::ButtonStyle::kWuiPrimary),
     toolmenu_(toolbar(),
               "dropdown_menu_tools",
               0,
               0,
               34U,
               12,
               34U,
               /** TRANSLATORS: Title for the tool menu button in the editor */
               as_tooltip_text_with_hotkey(_("Map Tools"), "t"),
               UI::DropdownType::kPictorialMenu,
               UI::PanelStyle::kWui,
               UI::ButtonStyle::kWuiPrimary),
     scenario_toolmenu_(toolbar(),
               "dropdown_menu_scenario_tools",
               0,
               0,
               34U,
               12,
               34U,
               /** TRANSLATORS: Title for the tool menu button in the editor */
               as_tooltip_text_with_hotkey(_("Scenario Tools"), "s"),
               UI::DropdownType::kPictorialMenu,
               UI::PanelStyle::kWui,
               UI::ButtonStyle::kWuiPrimary),
     showhidemenu_(toolbar(),
                   "dropdown_menu_showhide",
                   0,
                   0,
                   34U,
                   10,
                   34U,
                   /** TRANSLATORS: Title for a menu button in the editor. This menu will show/hide
                      building spaces, animals, immovables, resources */
                   _("Show / Hide"),
                   UI::DropdownType::kPictorialMenu,
                   UI::PanelStyle::kWui,
                   UI::ButtonStyle::kWuiPrimary),
     undo_(nullptr),
     redo_(nullptr),
     tools_(new Tools(e.map())),
     history_(nullptr) // history needs the undo/redo buttons
{
	add_main_menu();
	add_tool_menu();
	add_scenario_tool_menu();

	add_toolbar_button(
	   "wui/editor/menus/toolsize", "toolsize", _("Tool size"), &menu_windows_.toolsize, true);
	menu_windows_.toolsize.open_window = [this] {
		new EditorToolsizeMenu(*this, menu_windows_.toolsize);
	};

	toolbar()->add_space(15);

	add_mapview_menu(MiniMapType::kStaticMap);
	add_showhide_menu();

	toolbar()->add_space(15);

	undo_ = add_toolbar_button("wui/editor/menus/undo", "undo", _("Undo"));
	redo_ = add_toolbar_button("wui/editor/menus/redo", "redo", _("Redo"));

	history_.reset(new EditorHistory(*undo_, *redo_));

	undo_->sigclicked.connect([this] { history_->undo_action(egbase().world()); });
	redo_->sigclicked.connect([this] { history_->redo_action(egbase().world()); });

	toolbar()->add_space(15);

	add_toolbar_button("ui_basic/menu_help", "help", _("Help"), &menu_windows_.help, true);
	menu_windows_.help.open_window = [this] {
		new EditorHelp(*this, menu_windows_.help, &egbase().lua());
	};

	finalize_toolbar();

#ifndef NDEBUG
	set_display_flag(InteractiveBase::dfDebug, true);
#else
	set_display_flag(InteractiveBase::dfDebug, false);
#endif

	map_view()->field_clicked.connect([this](const Widelands::NodeAndTriangle<>& node_and_triangle) {
		map_clicked(node_and_triangle, false);
	});

	player_notes_ = Notifications::subscribe<Widelands::NoteEditorPlayerEdited>(
		[this](const Widelands::NoteEditorPlayerEdited& n) {
			if (n.map == &egbase().map()) {
				update_players();
			}
		});

	update_players();
}

void EditorInteractive::add_main_menu() {
	mainmenu_.set_image(g_gr->images().get("images/wui/editor/menus/main_menu.png"));

	menu_windows_.newmap.open_window = [this] { new MainMenuNewMap(*this, menu_windows_.newmap); };
	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("New Map"), MainMenuEntry::kNewMap,
	              g_gr->images().get("images/wui/editor/menus/new_map.png"));

	menu_windows_.newrandommap.open_window = [this] {
		new MainMenuNewRandomMap(*this, menu_windows_.newrandommap);
	};
	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("New Random Map"), MainMenuEntry::kNewRandomMap,
	              g_gr->images().get("images/wui/editor/menus/new_random_map.png"));

	menu_windows_.loadmap.open_window = [this] {
		new MainMenuLoadMap(*this, menu_windows_.loadmap);
	};
	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("Load Map"), MainMenuEntry::kLoadMap,
	              g_gr->images().get("images/wui/editor/menus/load_map.png"), false, "",
	              pgettext("hotkey", "Ctrl+l"));

	menu_windows_.savemap.open_window = [this] {
		new MainMenuSaveMap(*this, menu_windows_.savemap, menu_windows_.mapoptions);
	};
	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("Save Map"), MainMenuEntry::kSaveMap,
	              g_gr->images().get("images/wui/editor/menus/save_map.png"), false, "",
	              pgettext("hotkey", "Ctrl+s"));

	menu_windows_.mapoptions.open_window = [this] {
		new MainMenuMapOptions(*this, menu_windows_.mapoptions);
	};
	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("Map Options"), MainMenuEntry::kMapOptions,
	              g_gr->images().get("images/wui/editor/menus/map_options.png"));

	/** TRANSLATORS: An entry in the editor's main menu */
	mainmenu_.add(_("Exit Editor"), MainMenuEntry::kExitEditor,
	              g_gr->images().get("images/wui/menus/exit.png"));
	mainmenu_.selected.connect([this] { main_menu_selected(mainmenu_.get_selected()); });
	toolbar()->add(&mainmenu_);
}

void EditorInteractive::main_menu_selected(MainMenuEntry entry) {
	switch (entry) {
	case MainMenuEntry::kNewMap: {
		menu_windows_.newmap.toggle();
	} break;
	case MainMenuEntry::kNewRandomMap: {
		menu_windows_.newrandommap.toggle();
	} break;
	case MainMenuEntry::kLoadMap: {
		menu_windows_.loadmap.toggle();
	} break;
	case MainMenuEntry::kSaveMap: {
		menu_windows_.savemap.toggle();
	} break;
	case MainMenuEntry::kMapOptions: {
		menu_windows_.mapoptions.toggle();
	} break;
	case MainMenuEntry::kExitEditor: {
		exit();
	}
	}
}

void EditorInteractive::add_tool_menu() {
	toolmenu_.set_image(g_gr->images().get("images/wui/editor/menus/tools.png"));

	tool_windows_.height.open_window = [this] {
		new EditorToolChangeHeightOptionsMenu(*this, tools()->increase_height, tool_windows_.height);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Change height"), ToolMenuEntry::kChangeHeight,
	              g_gr->images().get("images/wui/editor/tools/height.png"), false,
	              /** TRANSLATORS: Tooltip for the change height tool in the editor */
	              _("Change the terrain height"));

	tool_windows_.noiseheight.open_window = [this] {
		new EditorToolNoiseHeightOptionsMenu(*this, tools()->noise_height, tool_windows_.noiseheight);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Random height"), ToolMenuEntry::kRandomHeight,
	              g_gr->images().get("images/wui/editor/tools/noise_height.png"), false,
	              /** TRANSLATORS: Tooltip for the random height tool in the editor */
	              _("Set the terrain height to random values"));

	tool_windows_.terrain.open_window = [this] {
		new EditorToolSetTerrainOptionsMenu(*this, tools()->set_terrain, tool_windows_.terrain);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Terrain"), ToolMenuEntry::kTerrain,
	              g_gr->images().get("images/wui/editor/tools/terrain.png"), false,
	              /** TRANSLATORS: Tooltip for the terrain tool in the editor */
	              _("Change the map’s terrain"));

	tool_windows_.immovables.open_window = [this] {
		new EditorToolPlaceImmovableOptionsMenu(
		   *this, tools()->place_immovable, tool_windows_.immovables);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Immovables"), ToolMenuEntry::kImmovables,
	              g_gr->images().get("images/wui/editor/tools/immovables.png"), false,
	              /** TRANSLATORS: Tooltip for the immovables tool in the editor */
	              _("Add or remove immovables"));

	tool_windows_.critters.open_window = [this] {
		new EditorToolPlaceCritterOptionsMenu(*this, tools()->place_critter, tool_windows_.critters);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Animals"), ToolMenuEntry::kAnimals,
	              g_gr->images().get("images/wui/editor/tools/critters.png"), false,
	              /** TRANSLATORS: Tooltip for the animals tool in the editor */
	              _("Add or remove animals"));

	tool_windows_.resources.open_window = [this] {
		new EditorToolChangeResourcesOptionsMenu(
		   *this, tools()->increase_resources, tool_windows_.resources);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Resources"), ToolMenuEntry::kResources,
	              g_gr->images().get("images/wui/editor/tools/resources.png"), false,
	              /** TRANSLATORS: Tooltip for the resources tool in the editor */
	              _("Set or change resources"));

	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Port spaces"), ToolMenuEntry::kPortSpace,
	              g_gr->images().get("images/wui/editor/tools/port_spaces.png"), false,
	              /** TRANSLATORS: Tooltip for the port spaces tool in the editor */
	              _("Add or remove port spaces"));

	tool_windows_.players.open_window = [this] {
		new EditorPlayerMenu(*this, tools()->set_starting_pos, tool_windows_.players);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Players"), ToolMenuEntry::kPlayers,
	              g_gr->images().get("images/wui/editor/tools/players.png"), false,
	              /** TRANSLATORS: Tooltip for the map size tool in the editor */
	              _("Set number of players and their names, tribes and starting positions"), "p");

	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Map origin"), ToolMenuEntry::kMapOrigin,
	              g_gr->images().get("images/wui/editor/tools/map_origin.png"), false,
	              /** TRANSLATORS: Tooltip for the map origin tool in the editor */
	              _("Set the position that will have the coordinates (0, 0). This will be the "
	                "top-left corner of a generated minimap."));

	tool_windows_.resizemap.open_window = [this] {
		new EditorToolResizeOptionsMenu(*this, tools()->resize, tool_windows_.resizemap);
	};
	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Map size"), ToolMenuEntry::kMapSize,
	              g_gr->images().get("images/wui/editor/tools/resize_map.png"), false,
	              /** TRANSLATORS: Tooltip for the map size tool in the editor */
	              _("Change the map’s size"));

	/** TRANSLATORS: An entry in the editor's tool menu */
	toolmenu_.add(_("Information"), ToolMenuEntry::kFieldInfo,
	              g_gr->images().get("images/wui/editor/fsel_editor_info.png"), false,
	              /** TRANSLATORS: Tooltip for the map information tool in the editor */
	              _("Click on a field to show information about it"), "i");
	toolmenu_.selected.connect([this] { tool_menu_selected(toolmenu_.get_selected()); });
	toolbar()->add(&toolmenu_);
}

void EditorInteractive::add_scenario_tool_menu() {
	scenario_toolmenu_.set_image(g_gr->images().get("images/wui/editor/menus/scenario_tools.png"));

	scenario_tool_windows_.fieldowner.open_window = [this] {
		new ScenarioToolFieldOwnerOptionsMenu(*this, tools()->sc_owner, scenario_tool_windows_.fieldowner);
	};
	/** TRANSLATORS: An entry in the editor's scenario tool menu */
	scenario_toolmenu_.add(_("Ownership"), ScenarioToolMenuEntry::kFieldOwner,
	              g_gr->images().get("images/wui/editor/tools/sc_owner.png"), false,
	              /** TRANSLATORS: Tooltip for the field ownership scenario tool in the editor */
	              _("Set the initial ownership of fields"));

	scenario_tool_windows_.infrastructure.open_window = [this] {
		new ScenarioToolInfrastructureOptionsMenu(*this, tools()->sc_infra, scenario_tool_windows_.infrastructure);
	};
	/** TRANSLATORS: An entry in the editor's scenario tool menu */
	scenario_toolmenu_.add(_("Place Infrastructure"), ScenarioToolMenuEntry::kInfrastructure,
	              g_gr->images().get("images/wui/editor/tools/sc_infra.png"), false,
	              /** TRANSLATORS: Tooltip for the place infrastructure scenario tool in the editor */
	              _("Place buildings, flags and tribe immovables"));

	/** TRANSLATORS: An entry in the editor's scenario tool menu */
	scenario_toolmenu_.add(_("Infrastructure Settings"), ScenarioToolMenuEntry::kInfrastructureSettings,
	              g_gr->images().get("images/wui/editor/tools/sc_infra_settings.png"), false,
	              /** TRANSLATORS: Tooltip for the infrastructure settings scenario tool in the editor */
	              _("Create the initial settings for buildings and flags"),
	              _("Shift+i"));

	scenario_tool_windows_.lua.open_window = [this] {
		new ScenarioLuaOptionsMenu(*this, scenario_tool_windows_.lua);
	};
	/** TRANSLATORS: An entry in the editor's scenario tool menu */
	scenario_toolmenu_.add(_("Scripting"), ScenarioToolMenuEntry::kLua,
	              g_gr->images().get("images/wui/editor/menus/scripting.png"), false,
	              /** TRANSLATORS: Tooltip for the scenario scripting menu in the editor */
	              _("Edit the scenario storyline"));

	scenario_toolmenu_.selected.connect([this] { scenario_tool_menu_selected(scenario_toolmenu_.get_selected()); });
	toolbar()->add(&scenario_toolmenu_);
}

void EditorInteractive::tool_menu_selected(ToolMenuEntry entry) {
	switch (entry) {
	case ToolMenuEntry::kChangeHeight:
		tool_windows_.height.toggle();
		break;
	case ToolMenuEntry::kRandomHeight:
		tool_windows_.noiseheight.toggle();
		break;
	case ToolMenuEntry::kTerrain:
		tool_windows_.terrain.toggle();
		break;
	case ToolMenuEntry::kImmovables:
		tool_windows_.immovables.toggle();
		break;
	case ToolMenuEntry::kAnimals:
		tool_windows_.critters.toggle();
		break;
	case ToolMenuEntry::kResources:
		tool_windows_.resources.toggle();
		break;
	case ToolMenuEntry::kPortSpace:
		select_tool(tools()->set_port_space, EditorTool::First);
		break;
	case ToolMenuEntry::kPlayers:
		tool_windows_.players.toggle();
		break;
	case ToolMenuEntry::kMapOrigin:
		select_tool(tools()->set_origin, EditorTool::First);
		break;
	case ToolMenuEntry::kMapSize:
		tool_windows_.resizemap.toggle();
		break;
	case ToolMenuEntry::kFieldInfo:
		select_tool(tools()->info, EditorTool::First);
		break;
	}
	toolmenu_.toggle();
}

void EditorInteractive::scenario_tool_menu_selected(ScenarioToolMenuEntry entry) {
	switch (entry) {
	case ScenarioToolMenuEntry::kFieldOwner:
		scenario_tool_windows_.fieldowner.toggle();
		break;
	case ScenarioToolMenuEntry::kInfrastructure:
		scenario_tool_windows_.infrastructure.toggle();
		break;
	case ScenarioToolMenuEntry::kInfrastructureSettings:
		select_tool(tools()->sc_infra_settings, EditorTool::First);
		break;
	case ScenarioToolMenuEntry::kLua:
		scenario_tool_windows_.lua.toggle();
		break;
	}
	scenario_toolmenu_.toggle();
}

void EditorInteractive::add_showhide_menu() {
	showhidemenu_.set_image(g_gr->images().get("images/wui/menus/showhide.png"));
	toolbar()->add(&showhidemenu_);

	rebuild_showhide_menu();

	showhidemenu_.selected.connect([this] { showhide_menu_selected(showhidemenu_.get_selected()); });
}

void EditorInteractive::rebuild_showhide_menu() {
	showhidemenu_.clear();

	/** TRANSLATORS: An entry in the editor's show/hide menu to toggle whether building spaces are
	 * shown */
	showhidemenu_.add(buildhelp() ? _("Hide Building Spaces") : _("Show Building Spaces"),
	                  ShowHideEntry::kBuildingSpaces,
	                  g_gr->images().get("images/wui/menus/toggle_buildhelp.png"), false, "",
	                  pgettext("hotkey", "Space"));

	/** TRANSLATORS: An entry in the editor's show/hide menu to toggle whether the map grid is shown
	 */
	showhidemenu_.add(draw_grid_ ? _("Hide Grid") : _("Show Grid"), ShowHideEntry::kGrid,
	                  g_gr->images().get("images/wui/menus/menu_toggle_grid.png"), false, "", "G");

	/** TRANSLATORS: An entry in the editor's show/hide menu to toggle whether immovables (trees,
	 * rocks etc.) are shown */
	showhidemenu_.add(draw_immovables_ ? _("Hide Immovables") : _("Show Immovables"),
	                  ShowHideEntry::kImmovables,
	                  g_gr->images().get("images/wui/menus/toggle_immovables.png"));

	/** TRANSLATORS: An entry in the editor's show/hide menu to toggle whether animals are shown */
	showhidemenu_.add(draw_bobs_ ? _("Hide Animals") : _("Show Animals"), ShowHideEntry::kAnimals,
	                  g_gr->images().get("images/wui/menus/toggle_bobs.png"));

	/** TRANSLATORS: An entry in the editor's show/hide menu to toggle whether resources are shown */
	showhidemenu_.add(draw_resources_ ? _("Hide Resources") : _("Show Resources"),
	                  ShowHideEntry::kResources,
	                  g_gr->images().get("images/wui/menus/toggle_resources.png"));
}

void EditorInteractive::showhide_menu_selected(ShowHideEntry entry) {
	switch (entry) {
	case ShowHideEntry::kBuildingSpaces: {
		toggle_buildhelp();
	} break;
	case ShowHideEntry::kGrid: {
		toggle_grid();
	} break;
	case ShowHideEntry::kImmovables: {
		toggle_immovables();
	} break;
	case ShowHideEntry::kAnimals: {
		toggle_bobs();
	} break;
	case ShowHideEntry::kResources: {
		toggle_resources();
	} break;
	}
	rebuild_showhide_menu();
}

void EditorInteractive::load(const std::string& filename) {
	assert(filename.size());

	Widelands::Map* map = egbase().mutable_map();

	cleanup_for_load();

	std::unique_ptr<Widelands::MapLoader> ml(map->get_correct_loader(filename));
	if (!ml.get())
		throw WLWarning(
		   _("Unsupported Format"),
		   _("Widelands could not load the file \"%s\". The file format seems to be incompatible."),
		   filename.c_str());
	ml->preload_map(true);

	UI::ProgressWindow loader_ui("images/loadscreens/editor.jpg");
	std::vector<std::string> tipstext;
	tipstext.push_back("editor");

	GameTips editortips(loader_ui, tipstext);

	load_all_tribes(&egbase(), &loader_ui);

	// Create the players. TODO(SirVer): this must be managed better
	// TODO(GunChleoc): Ugly - we only need this for the test suite right now
	loader_ui.step(_("Creating players"));
	iterate_player_numbers(p, map->get_nrplayers()) {
		if (!map->get_scenario_player_tribe(p).empty()) {
			egbase().add_player(
			   p, 0, map->get_scenario_player_tribe(p), map->get_scenario_player_name(p));
		}
	}

	ml->load_map_complete(egbase(), Widelands::MapLoader::LoadType::kEditor);
	egbase().postload();
	egbase().load_graphics(loader_ui);
	map_changed(MapWas::kReplaced);
}

void EditorInteractive::cleanup_for_load() {
	// TODO(unknown): get rid of cleanup_for_load, it tends to be very messy
	// Instead, delete and re-create the egbase.
	egbase().cleanup_for_load();
	functions_.clear();
	variables_.clear();
}

/// Called just before the editor starts, after postload, init and gfxload.
void EditorInteractive::start() {
	// Run the editor initialization script, if any
	try {
		egbase().lua().run_script("map:scripting/editor_init.lua");
	} catch (LuaScriptNotExistingError&) {
		// do nothing.
	}
	map_changed(MapWas::kReplaced);
}

/**
 * Called every frame.
 *
 * Advance the timecounter and animate textures.
 */
void EditorInteractive::think() {
	InteractiveBase::think();

	uint32_t lasttime = realtime_;

	realtime_ = SDL_GetTicks();

	egbase().get_gametime_pointer() += realtime_ - lasttime;
}

void EditorInteractive::exit() {
	if (need_save_) {
		if (SDL_GetModState() & KMOD_CTRL) {
			end_modal<UI::Panel::Returncodes>(UI::Panel::Returncodes::kBack);
		} else {
			UI::WLMessageBox mmb(this, _("Unsaved Map"),
			                     _("The map has not been saved, do you really want to quit?"),
			                     UI::WLMessageBox::MBoxType::kOkCancel);
			if (mmb.run<UI::Panel::Returncodes>() == UI::Panel::Returncodes::kBack)
				return;
		}
	}
	end_modal<UI::Panel::Returncodes>(UI::Panel::Returncodes::kBack);
}

void EditorInteractive::map_clicked(const Widelands::NodeAndTriangle<>& node_and_triangle,
                                    const bool should_draw) {
	history_->do_action(tools_->current(), tools_->use_tool, *egbase().mutable_map(),
	                    egbase().world(), node_and_triangle, *this, should_draw);
	set_need_save(true);
}

bool EditorInteractive::handle_mouserelease(uint8_t btn, int32_t x, int32_t y) {
	if (btn == SDL_BUTTON_LEFT) {
		is_painting_ = false;
	}
	return InteractiveBase::handle_mouserelease(btn, x, y);
}

bool EditorInteractive::handle_mousepress(uint8_t btn, int32_t x, int32_t y) {
	if (btn == SDL_BUTTON_LEFT) {
		is_painting_ = true;
	}
	return InteractiveBase::handle_mousepress(btn, x, y);
}

void EditorInteractive::draw(RenderTarget& dst) {
	const auto& ebase = egbase();
	auto* fields_to_draw = map_view()->draw_terrain(ebase, Workareas(), draw_grid_, &dst);

	const float scale = 1.f / map_view()->view().zoom;
	const uint32_t gametime = ebase.get_gametime();

	// The map provides a mapping from player number to Coords, while we require
	// the inverse here. We construct this, but this is done on every frame and
	// therefore potentially expensive - though it never showed up in any of my
	// profiles. We could change the Map should this become a bottleneck, since
	// plrnum -> coords is needed less often.
	const auto& map = ebase.map();
	std::map<Widelands::Coords, int> starting_positions;
	for (int i = 1; i <= map.get_nrplayers(); ++i) {
		starting_positions[map.get_starting_pos(i)] = i;
	}

	// Figure out which fields are currently under the selection.
	std::set<Widelands::Coords> selected_nodes;
	std::set<Widelands::TCoords<>> selected_triangles;
	if (!get_sel_triangles()) {
		Widelands::MapRegion<> mr(map, Widelands::Area<>(get_sel_pos().node, get_sel_radius()));
		do {
			selected_nodes.emplace(mr.location());
		} while (mr.advance(map));
	} else {
		Widelands::MapTriangleRegion<> mr(
		   map, Widelands::Area<Widelands::TCoords<>>(get_sel_pos().triangle, get_sel_radius()));
		do {
			selected_triangles.emplace(mr.location());
		} while (mr.advance(map));
	}

	const auto& world = ebase.world();
	for (size_t idx = 0; idx < fields_to_draw->size(); ++idx) {
		const FieldsToDraw::Field& field = fields_to_draw->at(idx);

		draw_border_markers(field, scale, *fields_to_draw, &dst);

		if (draw_immovables_) {
			Widelands::BaseImmovable* const imm = field.fcoords.field->get_immovable();
			if (imm != nullptr && imm->get_positions(ebase).front() == field.fcoords) {
				imm->draw(
				   gametime, TextToDraw::kNone, field.rendertarget_pixel, field.fcoords, scale, &dst);
			}
		}

		if (draw_bobs_) {
			for (Widelands::Bob* bob = field.fcoords.field->get_first_bob(); bob;
			     bob = bob->get_next_bob()) {
				bob->draw(
				   ebase, TextToDraw::kNone, field.rendertarget_pixel, field.fcoords, scale, &dst);
			}
		}

		// Draw resource overlay.
		uint8_t const amount = field.fcoords.field->get_resources_amount();
		if (draw_resources_ && amount > 0) {
			const std::string& immname =
			   world.get_resource(field.fcoords.field->get_resources())->editor_image(amount);
			if (!immname.empty()) {
				const auto* pic = g_gr->images().get(immname);
				blit_field_overlay(
				   &dst, field, pic, Vector2i(pic->width() / 2, pic->height() / 2), scale);
			}
		}

		// Draw build help.
		if (buildhelp()) {
			const auto* overlay =
			   get_buildhelp_overlay(tools_->current().nodecaps_for_buildhelp(field.fcoords, ebase));
			if (overlay != nullptr) {
				blit_field_overlay(&dst, field, overlay->pic, overlay->hotspot, scale);
			}
		}

		// Draw the player starting position overlays.
		const auto it = starting_positions.find(field.fcoords);
		if (it != starting_positions.end()) {
			const Image* player_image =
			   playercolor_image(it->second - 1, "images/players/player_position.png");
			assert(player_image != nullptr);
			constexpr int kStartingPosHotspotY = 55;
			blit_field_overlay(&dst, field, player_image,
			                   Vector2i(player_image->width() / 2, kStartingPosHotspotY), scale);
		}

		// Draw selection markers on the field.
		if (selected_nodes.count(field.fcoords) > 0) {
			const Image* pic = get_sel_picture();
			blit_field_overlay(&dst, field, pic, Vector2i(pic->width() / 2, pic->height() / 2), scale);
		}

		// Draw selection markers on the triangles.
		if (field.all_neighbors_valid()) {
			const FieldsToDraw::Field& rn = fields_to_draw->at(field.rn_index);
			const FieldsToDraw::Field& brn = fields_to_draw->at(field.brn_index);
			const FieldsToDraw::Field& bln = fields_to_draw->at(field.bln_index);
			if (selected_triangles.count(
			       Widelands::TCoords<>(field.fcoords, Widelands::TriangleIndex::R))) {
				const Vector2i tripos(
				   (field.rendertarget_pixel.x + rn.rendertarget_pixel.x + brn.rendertarget_pixel.x) /
				      3,
				   (field.rendertarget_pixel.y + rn.rendertarget_pixel.y + brn.rendertarget_pixel.y) /
				      3);
				const Image* pic = get_sel_picture();
				blit_overlay(&dst, tripos, pic, Vector2i(pic->width() / 2, pic->height() / 2), scale);
			}
			if (selected_triangles.count(
			       Widelands::TCoords<>(field.fcoords, Widelands::TriangleIndex::D))) {
				const Vector2i tripos(
				   (field.rendertarget_pixel.x + bln.rendertarget_pixel.x + brn.rendertarget_pixel.x) /
				      3,
				   (field.rendertarget_pixel.y + bln.rendertarget_pixel.y + brn.rendertarget_pixel.y) /
				      3);
				const Image* pic = get_sel_picture();
				blit_overlay(&dst, tripos, pic, Vector2i(pic->width() / 2, pic->height() / 2), scale);
			}
		}
	}
}

/// Needed to get freehand painting tools (hold down mouse and move to edit).
void EditorInteractive::set_sel_pos(Widelands::NodeAndTriangle<> const sel) {
	bool const target_changed = tools_->current().operates_on_triangles() ?
	                               sel.triangle != get_sel_pos().triangle :
	                               sel.node != get_sel_pos().node;
	InteractiveBase::set_sel_pos(sel);
	if (target_changed && is_painting_) {
		map_clicked(sel, true);
	}
}

void EditorInteractive::set_sel_radius_and_update_menu(uint32_t const val) {
	if (tools_->current().has_size_one()) {
		set_sel_radius(0);
		return;
	}
	if (UI::UniqueWindow* const w = menu_windows_.toolsize.window) {
		dynamic_cast<EditorToolsizeMenu&>(*w).update(val);
	} else {
		set_sel_radius(val);
	}
}

void EditorInteractive::stop_painting() {
	is_painting_ = false;
}

bool EditorInteractive::player_hears_field(const Widelands::Coords&) const {
	return true;
}

void EditorInteractive::toggle_resources() {
	draw_resources_ = !draw_resources_;
}

void EditorInteractive::toggle_immovables() {
	draw_immovables_ = !draw_immovables_;
}

void EditorInteractive::toggle_bobs() {
	draw_bobs_ = !draw_bobs_;
}

void EditorInteractive::toggle_grid() {
	draw_grid_ = !draw_grid_;
}

bool EditorInteractive::handle_key(bool const down, SDL_Keysym const code) {
	if (down) {
		switch (code.sym) {
		// Sel radius
		case SDLK_1:
			if (code.mod & (KMOD_CTRL)) {
				toggle_buildhelp();
			} else {
				set_sel_radius_and_update_menu(0);
			}
			return true;
		case SDLK_2:
			if (code.mod & (KMOD_CTRL)) {
				toggle_immovables();
			} else {
				set_sel_radius_and_update_menu(1);
			}
			return true;
		case SDLK_3:
			if (code.mod & (KMOD_CTRL)) {
				toggle_bobs();
			} else {
				set_sel_radius_and_update_menu(2);
			}
			return true;
		case SDLK_4:
			if (code.mod & (KMOD_CTRL)) {
				toggle_resources();
			} else {
				set_sel_radius_and_update_menu(3);
			}
			return true;
		case SDLK_5:
			set_sel_radius_and_update_menu(4);
			return true;
		case SDLK_6:
			set_sel_radius_and_update_menu(5);
			return true;
		case SDLK_7:
			set_sel_radius_and_update_menu(6);
			return true;
		case SDLK_8:
			set_sel_radius_and_update_menu(7);
			return true;
		case SDLK_9:
			set_sel_radius_and_update_menu(8);
			return true;
		case SDLK_0:
			if (!(code.mod & KMOD_CTRL)) {
				set_sel_radius_and_update_menu(9);
				return true;
			}
			break;

		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
			if (tools_->use_tool == EditorTool::First)
				select_tool(tools_->current(), EditorTool::Second);
			return true;

		case SDLK_LCTRL:
		case SDLK_RCTRL:
		// TODO(GunChleoc): Keeping ALT and MODE to make the transition easier. Remove for Build 20.
		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_MODE:
			if (tools_->use_tool == EditorTool::First)
				select_tool(tools_->current(), EditorTool::Third);
			return true;

		case SDLK_SPACE:
			toggle_buildhelp();
			return true;

		case SDLK_g:
			toggle_grid();
			return true;

		case SDLK_c:
			set_display_flag(
			   InteractiveBase::dfShowCensus, !get_display_flag(InteractiveBase::dfShowCensus));
			return true;

		case SDLK_h:
			mainmenu_.toggle();
			return true;

		case SDLK_i:
			if (code.mod & KMOD_SHIFT) {
				select_tool(tools_->sc_infra_settings, EditorTool::First);
			} else {
				select_tool(tools_->info, EditorTool::First);
			}
			return true;

		case SDLK_l:
			if (code.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
				menu_windows_.loadmap.toggle();
			}
			return true;

		case SDLK_p:
			tool_windows_.players.toggle();
			return true;

		case SDLK_s:
			if (code.mod & (KMOD_LCTRL | KMOD_RCTRL)) {
				menu_windows_.savemap.toggle();
			} else {
				scenario_toolmenu_.toggle();
			}
			return true;

		case SDLK_t:
			toolmenu_.toggle();
			return true;

		case SDLK_y:
			if (code.mod & (KMOD_LCTRL | KMOD_RCTRL))
				history_->redo_action(egbase().world());
			return true;

		case SDLK_z:
			if ((code.mod & (KMOD_LCTRL | KMOD_RCTRL)) && (code.mod & (KMOD_LSHIFT | KMOD_RSHIFT)))
				history_->redo_action(egbase().world());
			else if (code.mod & (KMOD_LCTRL | KMOD_RCTRL))
				history_->undo_action(egbase().world());
			return true;

		case SDLK_F1:
			menu_windows_.help.toggle();
			return true;

		default:
			break;
		}
	} else {
		// key up events
		switch (code.sym) {
		case SDLK_LSHIFT:
		case SDLK_RSHIFT:
		case SDLK_LCTRL:
		case SDLK_RCTRL:
		// TODO(GunChleoc): Keeping ALT and MODE to make the transition easier. Remove for Build 20.
		case SDLK_LALT:
		case SDLK_RALT:
		case SDLK_MODE:
			if (tools_->use_tool != EditorTool::First)
				select_tool(tools_->current(), EditorTool::First);
			return true;
		default:
			break;
		}
	}
	return InteractiveBase::handle_key(down, code);
}

void EditorInteractive::select_tool(EditorTool& primary, EditorTool::ToolIndex const which) {
	if (which == EditorTool::First && &primary != tools_->current_pointer) {
		if (primary.has_size_one()) {
			set_sel_radius(0);
			if (UI::UniqueWindow* const w = menu_windows_.toolsize.window) {
				EditorToolsizeMenu& toolsize_menu = dynamic_cast<EditorToolsizeMenu&>(*w);
				toolsize_menu.set_buttons_enabled(false);
			}
		} else {
			if (UI::UniqueWindow* const w = menu_windows_.toolsize.window) {
				EditorToolsizeMenu& toolsize_menu = dynamic_cast<EditorToolsizeMenu&>(*w);
				toolsize_menu.update(toolsize_menu.value());
			}
		}
		egbase().mutable_map()->recalc_whole_map(egbase().world());
	}
	tools_->current_pointer = &primary;
	tools_->use_tool = which;

	if (const Image* sel_pic = primary.get_sel(which)) {
		set_sel_picture(sel_pic);
	} else {
		unset_sel_picture();
	}
	set_sel_triangles(primary.operates_on_triangles());
}

void EditorInteractive::run_editor(const std::string& filename, const std::string& script_to_run) {
	Widelands::EditorGameBase egbase(nullptr);
	EditorInteractive& eia = *new EditorInteractive(egbase);
	egbase.set_ibase(&eia);  // TODO(unknown): get rid of this
	{
		UI::ProgressWindow loader_ui("images/loadscreens/editor.jpg");
		std::vector<std::string> tipstext;
		tipstext.push_back("editor");
		GameTips editortips(loader_ui, tipstext);

		{
			if (filename.empty()) {
				loader_ui.step(_("Creating empty map…"));
				egbase.mutable_map()->create_empty_map(
				   egbase.world(), 64, 64, 0,
				   /** TRANSLATORS: Default name for new map */
				   _("No Name"),
				   g_options.pull_section("global").get_string(
				      "realname",
				      /** TRANSLATORS: Map author name when it hasn't been set yet */
				      pgettext("author_name", "Unknown")));

				load_all_tribes(&egbase, &loader_ui);

				egbase.load_graphics(loader_ui);
				loader_ui.step(std::string());
			} else {
				loader_ui.step((boost::format(_("Loading map “%s”…")) % filename).str());
				eia.load(filename);
			}
		}

		egbase.postload();

		eia.start();

		if (!script_to_run.empty()) {
			eia.egbase().lua().run_script(script_to_run);
		}
	}
	eia.run<UI::Panel::Returncodes>();

	egbase.cleanup_objects();
}

void EditorInteractive::map_changed(const MapWas& action) {
	switch (action) {
	case MapWas::kReplaced:
		history_.reset(new EditorHistory(*undo_, *redo_));
		undo_->set_enabled(false);
		redo_->set_enabled(false);

		tools_.reset(new Tools(egbase().map()));
		select_tool(tools_->info, EditorTool::First);
		set_sel_radius(0);

		set_need_save(false);
		show_buildhelp(true);

		// Close all windows.
		for (Panel* child = get_first_child(); child; child = child->get_next_sibling()) {
			if (is_a(UI::Window, child)) {
				child->die();
			}
		}

		// Ensure that there is at least 1 player
		if (egbase().map().get_nrplayers() < 1) {
			Widelands::Map* mutable_map = egbase().mutable_map();
			mutable_map->set_nrplayers(1);
			// Init player 1
			mutable_map->set_scenario_player_ai(1, "");
			mutable_map->set_scenario_player_closeable(1, false);
			/** TRANSLATORS: Default player name, e.g. Player 1 */
			mutable_map->set_scenario_player_name(1, (boost::format(_("Player %u")) % 1).str());
			mutable_map->set_scenario_player_tribe(1, "");
			update_players();
		}

		variables_.clear();
		functions_.clear();
		functions_.emplace(std::make_pair(kMainFunction, Function(true)));

		// Make sure that we will start at coordinates (0,0).
		map_view()->set_view(MapView::View{Vector2f::zero(), 1.f}, MapView::Transition::Jump);
		set_sel_pos(Widelands::NodeAndTriangle<>{
		   Widelands::Coords(0, 0),
		   Widelands::TCoords<>(Widelands::Coords(0, 0), Widelands::TriangleIndex::D)});
		break;

	case MapWas::kGloballyMutated:
		break;
	}
}

void EditorInteractive::update_players() {
	const Widelands::Map& map = egbase().map();
	Widelands::PlayersManager* pm = egbase().player_manager();
	assert(pm);
	pm->cleanup();
	const Widelands::PlayerNumber max = map.get_nrplayers();
	for (Widelands::PlayerNumber p = 1; p <= max; ++p) {
		pm->add_player(p, p, map.get_scenario_player_tribe(p), map.get_scenario_player_name(p));
	}
	egbase().allocate_player_maps();
}

EditorInteractive::Tools* EditorInteractive::tools() {
	return tools_.get();
}

bool EditorInteractive::save_as_scenario() const {
	// NOCOM EditorInteractive::save_as_scenario()
	// return functions_.find(kMainFunction) != functions_.end();
	return true;
}

void EditorInteractive::write_lua(FileWrite& fw) const {
	const Widelands::Map& map = egbase().map();

	// Header
	fw.print_f("-- Automatically created by Widelands %s (%s)\n", build_id().c_str(), build_type().c_str());
	fw.print_f("-- Do not modify this file. "
			"All changes will be discarded the next time this map is saved in the editor.\n");

	// i18n
	fw.print_f("\nset_textdomain(\"scenario_%s.wmf\")\n\n", map.get_name().c_str());

	// Useful includes
	fw.print_f("include \"scripting/coroutine.lua\"\n");
	fw.print_f("include \"scripting/objective_utils.lua\"\n");
	fw.print_f("include \"scripting/infrastructure.lua\"\n");
	fw.print_f("include \"scripting/table.lua\"\n\n");

	// Variables
	for (const auto& pair : variables_) {
		fw.print_f("%s = %s\n", pair.first.c_str(), pair.second.value.c_str());
	}

	// Functions
	for (const auto& pair : functions_) {
		fw.print_f("\nfunction %s(", pair.first.c_str());
		const size_t params = pair.second.parameters.size();
		if (params) {
			auto it = pair.second.parameters.begin();
			fw.print_f("%s", it->c_str());
			for (size_t i = 1; i < params; ++i) {
				++it;
				fw.print_f(", %s", it->c_str());
			}
			assert(it == pair.second.parameters.end());
		}
		fw.print_f(")\n");
		// NOCOM function body
		fw.print_f("   print(\"[NOCOM] Hello world! :)\")\n");
		fw.print_f("end\n");
	}

	// Main function(s) call
	for (const auto& pair : functions_) {
		if (pair.second.autostart) {
			assert(pair.second.parameters.empty());
			fw.print_f("\nrun(%s)", pair.first.c_str());
		}
	}
}

