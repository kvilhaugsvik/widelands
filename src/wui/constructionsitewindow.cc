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

#include "wui/constructionsitewindow.h"

#include <boost/format.hpp>

#include "graphic/graphic.h"
#include "wui/actionconfirm.h"
#include "wui/inputqueuedisplay.h"
#include "wui/interactive_player.h"

static const char pic_tab_wares[] = "images/wui/buildings/menu_tab_wares.png";
static const char pic_tab_settings[] = "images/wui/menus/statistics_stock.png";
static const char pic_tab_settings_wares[] = "images/wui/stats/menu_tab_wares_warehouse.png";
static const char pic_tab_settings_workers[] = "images/wui/stats/menu_tab_workers_warehouse.png";
static const char pic_stock_policy_prefer[] = "images/wui/buildings/stock_policy_prefer.png";
static const char pic_stock_policy_dontstock[] = "images/wui/buildings/stock_policy_dontstock.png";
static const char pic_stock_policy_remove[] = "images/wui/buildings/stock_policy_remove.png";
static const char pic_stock_policy_button_normal[] =
   "images/wui/buildings/stock_policy_button_normal.png";
static const char pic_stock_policy_button_prefer[] =
   "images/wui/buildings/stock_policy_button_prefer.png";
static const char pic_stock_policy_button_dontstock[] =
   "images/wui/buildings/stock_policy_button_dontstock.png";
static const char pic_stock_policy_button_remove[] =
   "images/wui/buildings/stock_policy_button_remove.png";
static const char pic_decrease_capacity[] = "images/wui/buildings/menu_down_train.png";
static const char pic_increase_capacity[] = "images/wui/buildings/menu_up_train.png";
constexpr uint16_t kSoldierCapacityDisplayWidth = 145;

ConstructionSiteWindow::FakeWaresDisplay::FakeWaresDisplay(UI::Panel* parent,
                                                           bool can_act,
                                                           Widelands::ConstructionSite& cs,
                                                           Widelands::WareWorker type)
   : WaresDisplay(parent, 0, 0, cs.owner().tribe(), type, can_act),
     settings_(*dynamic_cast<Widelands::WarehouseSettings*>(cs.get_settings())),
     tribe_(cs.owner().tribe()) {
}

void ConstructionSiteWindow::FakeWaresDisplay::draw_ware(RenderTarget& dst,
                                                         Widelands::DescriptionIndex ware) {
	if (get_type() == Widelands::wwWORKER &&
	    std::find(tribe_.worker_types_without_cost().begin(),
	              tribe_.worker_types_without_cost().end(),
	              ware) != tribe_.worker_types_without_cost().end()) {
		return;
	}
	WaresDisplay::draw_ware(dst, ware);

	const auto& map =
	   get_type() == Widelands::wwWARE ? settings_.ware_preferences : settings_.worker_preferences;
	const auto it = map.find(ware);
	if (it == map.end()) {
		return;
	}
	const Image* pic = nullptr;
	switch (it->second) {
	case Widelands::StockPolicy::kPrefer:
		pic = g_gr->images().get(pic_stock_policy_prefer);
		break;
	case Widelands::StockPolicy::kDontStock:
		pic = g_gr->images().get(pic_stock_policy_dontstock);
		break;
	case Widelands::StockPolicy::kRemove:
		pic = g_gr->images().get(pic_stock_policy_remove);
		break;
	case Widelands::StockPolicy::kNormal:
		// No icon for the normal policy
		return;
	}
	assert(pic);
	dst.blit(ware_position(ware), pic);
}

ConstructionSiteWindow::ConstructionSiteWindow(InteractiveGameBase& parent,
                                               UI::UniqueWindow::Registry& reg,
                                               Widelands::ConstructionSite& cs,
                                               bool avoid_fastclick,
                                               bool workarea_preview_wanted)
   : BuildingWindow(parent, reg, cs, cs.building(), avoid_fastclick),
     construction_site_(&cs),
     progress_(nullptr),
     cs_launch_expedition_(nullptr),
     cs_prefer_heroes_rookies_(nullptr),
     cs_soldier_capacity_decrease_(nullptr),
     cs_soldier_capacity_increase_(nullptr),
     cs_soldier_capacity_display_(nullptr),
     cs_stopped_(nullptr),
     cs_warehouse_wares_(nullptr),
     cs_warehouse_workers_(nullptr) {
	init(avoid_fastclick, workarea_preview_wanted);
}

void ConstructionSiteWindow::init(bool avoid_fastclick, bool workarea_preview_wanted) {
	Widelands::ConstructionSite* construction_site = construction_site_.get(igbase()->egbase());
	assert(construction_site != nullptr);
	set_building_descr_for_help(&construction_site->building());

	BuildingWindow::init(avoid_fastclick, workarea_preview_wanted);
	UI::Box& box = *new UI::Box(get_tabs(), 0, 0, UI::Box::Vertical);

	// Add the progress bar
	progress_ = new UI::ProgressBar(&box, 0, 0, UI::ProgressBar::DefaultWidth,
	                                UI::ProgressBar::DefaultHeight, UI::ProgressBar::Horizontal);
	progress_->set_total(1 << 16);
	box.add(progress_, UI::Box::Resizing::kAlign, UI::Align::kCenter);

	box.add_space(8);

	// Add the wares queue
	for (uint32_t i = 0; i < construction_site->get_nrwaresqueues(); ++i)
		box.add(new InputQueueDisplay(
		   &box, 0, 0, *igbase(), *construction_site, *construction_site->get_waresqueue(i)));

	get_tabs()->add("wares", g_gr->images().get(pic_tab_wares), &box, _("Building materials"));

	if (construction_site->get_settings()) {
		const bool can_act = igbase()->can_act(construction_site->owner().player_number());
		// Create the settings. Since we don't access an actual building, we create
		// a simplified faksimile of the later building window that contains only
		// the relevant options.
		bool nothing_added = false;
		UI::Box& settings_box = *new UI::Box(get_tabs(), 0, 0, UI::Box::Vertical);
		if (upcast(Widelands::ProductionsiteSettings, ps, construction_site->get_settings())) {
			for (const auto& pair : ps->ware_queues) {
				InputQueueDisplay* queue = new InputQueueDisplay(
				   &settings_box, 0, 0, *igbase(), *construction_site, Widelands::wwWARE, pair.first);
				settings_box.add(queue);
				settings_box.add_space(8);
				cs_ware_queues_.push_back(queue);
			}
			for (const auto& pair : ps->worker_queues) {
				InputQueueDisplay* queue = new InputQueueDisplay(
				   &settings_box, 0, 0, *igbase(), *construction_site, Widelands::wwWORKER, pair.first);
				settings_box.add(queue);
				settings_box.add_space(8);
				cs_ware_queues_.push_back(queue);
			}
			if (upcast(Widelands::TrainingsiteSettings, ts, ps)) {
				UI::Box& soldier_capacity_box = *new UI::Box(&settings_box, 0, 0, UI::Box::Horizontal);
				settings_box.add(&soldier_capacity_box, UI::Box::Resizing::kAlign, UI::Align::kCenter);
				cs_soldier_capacity_decrease_ = new UI::Button(
				   &soldier_capacity_box, "decrease", 0, 0, 32, 32, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_decrease_capacity),
				   _("Decrease capacity. Hold down Ctrl to set the capacity to the lowest value"));
				cs_soldier_capacity_increase_ = new UI::Button(
				   &soldier_capacity_box, "increase", 0, 0, 32, 32, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_increase_capacity),
				   _("Increase capacity. Hold down Ctrl to set the capacity to the highest value"));
				cs_soldier_capacity_display_ =
				   new UI::Textarea(&soldier_capacity_box, "", UI::Align::kCenter);
				cs_soldier_capacity_decrease_->set_repeating(true);
				cs_soldier_capacity_increase_->set_repeating(true);
				cs_soldier_capacity_decrease_->set_enabled(can_act);
				cs_soldier_capacity_increase_->set_enabled(can_act);
				cs_soldier_capacity_decrease_->sigclicked.connect([this, ts]() {
					igbase()->game().send_player_change_soldier_capacity(
					   *construction_site_.get(igbase()->egbase()),
					   SDL_GetModState() & KMOD_CTRL ? 0 : ts->desired_capacity - 1);
				});
				cs_soldier_capacity_increase_->sigclicked.connect([this, ts]() {
					igbase()->game().send_player_change_soldier_capacity(
					   *construction_site_.get(igbase()->egbase()),
					   SDL_GetModState() & KMOD_CTRL ? ts->max_capacity : ts->desired_capacity + 1);
				});
				soldier_capacity_box.add(cs_soldier_capacity_decrease_);
				soldier_capacity_box.add(
				   cs_soldier_capacity_display_, UI::Box::Resizing::kAlign, UI::Align::kCenter);
				soldier_capacity_box.add(cs_soldier_capacity_increase_);
				cs_soldier_capacity_display_->set_fixed_width(kSoldierCapacityDisplayWidth);
				settings_box.add_space(8);
			}
			cs_stopped_ = new UI::Checkbox(&settings_box, Vector2i::zero(), _("Stopped"),
			                               _("Stop this building’s work after completion"));
			cs_stopped_->clickedto.connect([this, ps](bool stop) {
				if (stop != ps->stopped) {
					igbase()->game().send_player_start_stop_building(
					   *construction_site_.get(igbase()->egbase()));
				}
			});
			settings_box.add(cs_stopped_, UI::Box::Resizing::kFullSize);
			settings_box.add_space(8);
			cs_stopped_->set_enabled(can_act);
		} else if (upcast(Widelands::MilitarysiteSettings, ms, construction_site->get_settings())) {
			UI::Box& soldier_capacity_box = *new UI::Box(&settings_box, 0, 0, UI::Box::Horizontal);
			settings_box.add(&soldier_capacity_box, UI::Box::Resizing::kAlign, UI::Align::kCenter);
			cs_soldier_capacity_decrease_ = new UI::Button(
			   &soldier_capacity_box, "decrease", 0, 0, 32, 32, UI::ButtonStyle::kWuiMenu,
			   g_gr->images().get(pic_decrease_capacity),
			   _("Decrease capacity. Hold down Ctrl to set the capacity to the lowest value"));
			cs_soldier_capacity_increase_ = new UI::Button(
			   &soldier_capacity_box, "increase", 0, 0, 32, 32, UI::ButtonStyle::kWuiMenu,
			   g_gr->images().get(pic_increase_capacity),
			   _("Increase capacity. Hold down Ctrl to set the capacity to the highest value"));
			cs_soldier_capacity_display_ =
			   new UI::Textarea(&soldier_capacity_box, "", UI::Align::kCenter);
			cs_soldier_capacity_decrease_->set_repeating(true);
			cs_soldier_capacity_increase_->set_repeating(true);
			cs_soldier_capacity_decrease_->set_enabled(can_act);
			cs_soldier_capacity_increase_->set_enabled(can_act);
			cs_soldier_capacity_decrease_->sigclicked.connect([this, ms]() {
				igbase()->game().send_player_change_soldier_capacity(
				   *construction_site_.get(igbase()->egbase()),
				   SDL_GetModState() & KMOD_CTRL ? 1 : ms->desired_capacity - 1);
			});
			cs_soldier_capacity_increase_->sigclicked.connect([this, ms]() {
				igbase()->game().send_player_change_soldier_capacity(
				   *construction_site_.get(igbase()->egbase()),
				   SDL_GetModState() & KMOD_CTRL ? ms->max_capacity : ms->desired_capacity + 1);
			});
			soldier_capacity_box.add(cs_soldier_capacity_decrease_);
			soldier_capacity_box.add(
			   cs_soldier_capacity_display_, UI::Box::Resizing::kAlign, UI::Align::kCenter);
			soldier_capacity_box.add(cs_soldier_capacity_increase_);
			cs_soldier_capacity_display_->set_fixed_width(kSoldierCapacityDisplayWidth);
			settings_box.add_space(8);

			UI::Box& soldier_preference_box = *new UI::Box(&settings_box, 0, 0, UI::Box::Horizontal);
			settings_box.add(&soldier_preference_box, UI::Box::Resizing::kAlign, UI::Align::kCenter);
			UI::Panel& soldier_preference_panel =
			   *new UI::Panel(&soldier_preference_box, 0, 0, 64, 32);
			soldier_preference_box.add(&soldier_preference_panel);
			cs_prefer_heroes_rookies_.reset(new UI::Radiogroup());
			cs_prefer_heroes_rookies_->add_button(
			   &soldier_preference_panel, Vector2i::zero(),
			   g_gr->images().get("images/wui/buildings/prefer_heroes.png"), _("Prefer heroes"));
			cs_prefer_heroes_rookies_->add_button(
			   &soldier_preference_panel, Vector2i(32, 0),
			   g_gr->images().get("images/wui/buildings/prefer_rookies.png"), _("Prefer rookies"));
			cs_prefer_heroes_rookies_->set_state(ms->prefer_heroes ? 0 : 1);
			if (can_act) {
				cs_prefer_heroes_rookies_->changedto.connect([this](int32_t state) {
					igbase()->game().send_player_militarysite_set_soldier_preference(
					   *construction_site_.get(igbase()->egbase()),
					   state ? Widelands::SoldierPreference::kRookies :
					           Widelands::SoldierPreference::kHeroes);
				});
			}
			settings_box.add_space(8);
		} else if (upcast(Widelands::WarehouseSettings, ws, construction_site->get_settings())) {
			auto add_tab = [this, construction_site, can_act](
			                  Widelands::WareWorker ww, FakeWaresDisplay** display) {
				UI::Box& mainbox = *new UI::Box(get_tabs(), 0, 0, UI::Box::Vertical);
				*display = new FakeWaresDisplay(&mainbox, can_act, *construction_site, ww);
				mainbox.add(*display, UI::Box::Resizing::kFullSize);
				mainbox.add_space(8);
				UI::Box& buttonsbox = *new UI::Box(&mainbox, 0, 0, UI::Box::Horizontal);
				mainbox.add(&buttonsbox, UI::Box::Resizing::kAlign, UI::Align::kCenter);
				mainbox.add_space(8);
				UI::Button& sp_normal = *new UI::Button(
				   &buttonsbox, "stock_policy_normal", 0, 0, 34, 34, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_stock_policy_button_normal), _("Normal policy"));
				UI::Button& sp_prefer = *new UI::Button(
				   &buttonsbox, "stock_policy_prefer", 0, 0, 34, 34, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_stock_policy_button_prefer),
				   _("Preferably store selected wares here"));
				UI::Button& sp_dont = *new UI::Button(
				   &buttonsbox, "stock_policy_dontstock", 0, 0, 34, 34, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_stock_policy_button_dontstock),
				   _("Do not store selected wares here"));
				UI::Button& sp_remove = *new UI::Button(
				   &buttonsbox, "stock_policy_remove", 0, 0, 34, 34, UI::ButtonStyle::kWuiMenu,
				   g_gr->images().get(pic_stock_policy_button_remove),
				   _("Remove selected wares from here"));
				sp_remove.sigclicked.connect(boost::bind(
				   &ConstructionSiteWindow::change_policy, this, ww, Widelands::StockPolicy::kRemove));
				sp_dont.sigclicked.connect(boost::bind(&ConstructionSiteWindow::change_policy, this, ww,
				                                       Widelands::StockPolicy::kDontStock));
				sp_prefer.sigclicked.connect(boost::bind(
				   &ConstructionSiteWindow::change_policy, this, ww, Widelands::StockPolicy::kPrefer));
				sp_normal.sigclicked.connect(boost::bind(
				   &ConstructionSiteWindow::change_policy, this, ww, Widelands::StockPolicy::kNormal));
				sp_normal.set_enabled(can_act);
				sp_dont.set_enabled(can_act);
				sp_remove.set_enabled(can_act);
				sp_prefer.set_enabled(can_act);
				buttonsbox.add(&sp_normal);
				buttonsbox.add_space(8);
				buttonsbox.add(&sp_prefer);
				buttonsbox.add_space(8);
				buttonsbox.add(&sp_dont);
				buttonsbox.add_space(8);
				buttonsbox.add(&sp_remove);
				if (ww == Widelands::wwWARE) {
					get_tabs()->add("warehouse_wares", g_gr->images().get(pic_tab_settings_wares),
					                &mainbox, _("Ware settings to apply after construction"));
				} else {
					get_tabs()->add("warehouse_workers", g_gr->images().get(pic_tab_settings_workers),
					                &mainbox, _("Worker settings to apply after construction"));
				}
			};
			add_tab(Widelands::wwWARE, &cs_warehouse_wares_);
			add_tab(Widelands::wwWORKER, &cs_warehouse_workers_);
			if (construction_site->get_info().becomes->get_isport()) {
				cs_launch_expedition_ =
				   new UI::Checkbox(&settings_box, Vector2i::zero(), _("Start an expedition"),
				                    _("Start an expedition from this port after completion"));
				cs_launch_expedition_->clickedto.connect([this, ws](bool launch) {
					if (launch != ws->launch_expedition) {
						igbase()->game().send_player_start_or_cancel_expedition(
						   *construction_site_.get(igbase()->egbase()));
					}
				});
				settings_box.add(cs_launch_expedition_, UI::Box::Resizing::kFullSize);
				settings_box.add_space(8);
				cs_launch_expedition_->set_enabled(can_act);
			} else {
				nothing_added = true;
			}
		} else {
			NEVER_HERE();
		}

		if (!nothing_added) {
			get_tabs()->add("settings", g_gr->images().get(pic_tab_settings), &settings_box,
			                _("Settings to apply after construction"));
		}
	}

	set_title((boost::format("(%s)") % construction_site->building().descname()).str());
	think();
}

void ConstructionSiteWindow::change_policy(Widelands::WareWorker ww, Widelands::StockPolicy p) {
	Widelands::ConstructionSite* construction_site = construction_site_.get(igbase()->egbase());
	assert(construction_site);
	upcast(Widelands::WarehouseSettings, ws, construction_site->get_settings());
	assert(ws);
	if (ww == Widelands::wwWARE) {
		for (const auto& pair : ws->ware_preferences) {
			if (cs_warehouse_wares_->ware_selected(pair.first)) {
				igbase()->game().send_player_set_stock_policy(
				   *construction_site, Widelands::wwWARE, pair.first, p);
			}
		}
	} else {
		for (const auto& pair : ws->worker_preferences) {
			if (cs_warehouse_workers_->ware_selected(pair.first)) {
				igbase()->game().send_player_set_stock_policy(
				   *construction_site, Widelands::wwWORKER, pair.first, p);
			}
		}
	}
}

/*
===============
Make sure the window is redrawn when necessary.
===============
*/
void ConstructionSiteWindow::think() {
	// BuildingWindow::think() will call die in case we are no longer in
	// existance.
	BuildingWindow::think();

	Widelands::ConstructionSite* construction_site = construction_site_.get(igbase()->egbase());
	if (construction_site == nullptr) {
		return;
	}

	progress_->set_state(construction_site->get_built_per64k());

	const bool can_act = igbase()->can_act(construction_site->owner().player_number());
	// InputQueueDisplay and FakeWaresDisplay update themselves – we need to refresh the other
	// settings
	if (upcast(Widelands::ProductionsiteSettings, ps, construction_site->get_settings())) {
		assert(cs_stopped_);
		cs_stopped_->set_state(ps->stopped);
	}
	if (upcast(Widelands::TrainingsiteSettings, ts, construction_site->get_settings())) {
		assert(cs_soldier_capacity_decrease_);
		assert(cs_soldier_capacity_increase_);
		assert(cs_soldier_capacity_display_);
		cs_soldier_capacity_display_->set_text(
		   (boost::format(ngettext("%u soldier", "%u soldiers", ts->desired_capacity)) %
		    ts->desired_capacity)
		      .str());
		cs_soldier_capacity_decrease_->set_enabled(can_act && ts->desired_capacity > 0);
		cs_soldier_capacity_increase_->set_enabled(can_act &&
		                                           ts->desired_capacity < ts->max_capacity);
	} else if (upcast(Widelands::MilitarysiteSettings, ms, construction_site->get_settings())) {
		assert(cs_soldier_capacity_decrease_);
		assert(cs_soldier_capacity_increase_);
		assert(cs_soldier_capacity_display_);
		assert(cs_prefer_heroes_rookies_);
		cs_soldier_capacity_display_->set_text(
		   (boost::format(ngettext("%u soldier", "%u soldiers", ms->desired_capacity)) %
		    ms->desired_capacity)
		      .str());
		cs_soldier_capacity_decrease_->set_enabled(can_act && ms->desired_capacity > 1);
		cs_soldier_capacity_increase_->set_enabled(can_act &&
		                                           ms->desired_capacity < ms->max_capacity);
		// Since this function triggers a changedto signal resulting in a playercommand,
		// we can update the view only for spectators (which may be a problem if a script
		// changes the state while the window is open for the interactive player, but this
		// was never yet mentioned as a problem in militarysitewindows either).
		if (!can_act) {
			cs_prefer_heroes_rookies_->set_state(ms->prefer_heroes ? 0 : 1);
		}
	} else if (upcast(Widelands::WarehouseSettings, ws, construction_site->get_settings())) {
		if (cs_launch_expedition_) {
			cs_launch_expedition_->set_state(ws->launch_expedition);
		}
#ifndef NDEBUG
		else {
			assert(!ws->launch_expedition);
		}
#endif
	}
}
