/*
 * Copyright (C) 2002-2003, 2006-2011 by the Widelands Development Team
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "player.h"

#include "checkstep.h"
#include "cmd_expire_message.h"
#include "cmd_luacoroutine.h"
#include "constructionsite.h"
#include "economy/flag.h"
#include "economy/road.h"
#include "findimmovable.h"
#include "game.h"
#include "game_data_error.h"
#include "i18n.h"
#include "log.h"
#include "militarysite.h"
#include "soldier.h"
#include "soldiercontrol.h"
#include "sound/sound_handler.h"
#include "scripting/scripting.h"
#include "trainingsite.h"
#include "tribe.h"
#include "warehouse.h"
#include "warning.h"
#include "wexception.h"
#include "widelands_fileread.h"
#include "widelands_filewrite.h"

#include "wui/interactive_player.h"

#include "upcast.h"

namespace Widelands {

extern const Map_Object_Descr g_road_descr;

Player::Player
	(Editor_Game_Base  & the_egbase,
	 Player_Number         const plnum,
	 uint8_t               const initialization_index,
	 Tribe_Descr   const &       tribe_descr,
	 std::string   const &       name,
	 uint8_t       const * const playercolor)
	:
	m_egbase              (the_egbase),
	m_initialization_index(initialization_index),
	m_frontier_style_index(0),
	m_flag_style_index    (0),
	m_team_number(0),
	m_team_player_uptodate(false),
	m_see_all           (false),
	m_plnum             (plnum),
	m_tribe             (tribe_descr),
	m_casualties        (0),
	m_kills             (0),
	m_msites_lost        (0),
	m_msites_defeated    (0),
	m_civil_blds_lost    (0),
	m_civil_blds_defeated(0),
	m_allow_retreat_change(false),
	m_retreat_percentage  (50),
	m_fields            (0),
	m_allowed_worker_types  (tribe_descr.get_nrworkers  (), false),
	m_allowed_building_types(tribe_descr.get_nrbuildings(), true),
	m_ai(""),
	m_current_statistics(tribe_descr.get_nrwares    ()),
	m_ware_productions  (tribe_descr.get_nrwares    ())
{
	for (int32_t i = 0; i < 4; ++i)
		m_playercolor[i] =
			RGBColor
				(playercolor[i * 3 + 0],
				 playercolor[i * 3 + 1],
				 playercolor[i * 3 + 2]);

	set_name(name);
}


Player::~Player() {
	delete[] m_fields;
}


void Player::create_default_infrastructure() {
	const Map & map = egbase().map();
	if (Coords const starting_pos = map.get_starting_pos(m_plnum)) {
		try {
			Tribe_Descr::Initialization const & initialization =
				tribe().initialization(m_initialization_index);

			Game & game = ref_cast<Game, Editor_Game_Base>(egbase());

			// Run the corresponding script
			LuaCoroutine * cr = game.lua().run_script
				(*g_fs, "tribes/" + tribe().name() +
				 "/scripting/" +  initialization.name + ".lua",
				 "tribe_" + tribe().name())
				->get_coroutine("func");
			cr->push_arg(this);
			game.enqueue_command(new Cmd_LuaCoroutine(game.get_gametime(), cr));

			// Check if other starting positions are shared in and initialize them as well
			for (uint8_t n = 0; n < m_further_shared_in_player.size(); ++n) {
				Coords const further_pos = map.get_starting_pos(m_further_shared_in_player.at(n));

				// Run the corresponding script
				LuaCoroutine * ncr = game.lua().run_script
					(*g_fs, "tribes/" + tribe().name() +
					"/scripting/" + tribe().initialization(m_further_initializations.at(n)).name + ".lua",
					 "tribe_" + tribe().name())
					->get_coroutine("func");
				ncr->push_arg(this);
				ncr->push_arg(further_pos);
				game.enqueue_command(new Cmd_LuaCoroutine(game.get_gametime(), ncr));
			}
		} catch (Tribe_Descr::Nonexistent) {
			throw game_data_error
				("the selected initialization index (%u) is outside the range "
				 "(tribe edited between preload and game start?)",
				 m_initialization_index);
		}
	} else
		throw warning
			(_("Missing starting position"),
			 _
			 	("Widelands could not start the game, because player %u has no "
			 	 "starting position.\n"
			 	 "You can manually add a starting position with Widelands Editor, "
			 	 "to fix this problem."),
			 m_plnum);
}


/**
 * Allocate the fields array that contains player-specific field information.
 */
void Player::allocate_map()
{
	const Map & map = egbase().map();
	assert(map.get_width ());
	assert(map.get_height());
	m_fields = new Field[map.max_index()];
}

/**
 * Assign the player the given team number.
 */
void Player::set_team_number(TeamNumber team)
{
	m_team_number = team;
	m_team_player_uptodate = false;
}

/**
 * Returns whether this player and the given other player can attack
 * each other.
 */
bool Player::is_hostile(const Player & other) const
{
	return
		&other != this &&
		(!m_team_number || m_team_number != other.m_team_number);
}

/**
 * Updates the vector containing allied players
 */
void Player::update_team_players() {
	m_team_player.clear();
	m_team_player_uptodate = true;

	if (!m_team_number)
		return;

	for (Player_Number i = 1; i <= MAX_PLAYERS; ++i) {
		Player * other = egbase().get_player(i);
		if (!other)
			continue;
		if (other == this)
			continue;
		if (m_team_number == other->m_team_number)
			m_team_player.push_back(other);
	}
}


/*
 * Plays the corresponding sound when a message is received and if sound is
 * enabled.
 */
void Player::play_message_sound(const std::string & sender) {
#define MAYBE_PLAY(a) if (sender == a) { \
	g_sound_handler.play_fx(a, 200, PRIO_ALWAYS_PLAY); \
	return; \
	}

	if (g_options.pull_section("global").get_bool("sound_at_message", true)) {
		MAYBE_PLAY("site_occupied");
		MAYBE_PLAY("under_attack");

		g_sound_handler.play_fx("message", 200, PRIO_ALWAYS_PLAY);
	}
}

Message_Id Player::add_message
	(Game & game, Message & message, bool const popup)
{
	Message_Id const id = messages().add_message(message);
	Duration const duration = message.duration();
	if (duration != Forever())
		game.cmdqueue().enqueue
			(new Cmd_ExpireMessage
			 	(game.get_gametime() + duration, player_number(), id));

	if (Interactive_Player * const iplayer = game.get_ipl())
		if (&iplayer->player() == this) {
			play_message_sound(message.sender());

			if (popup)
				iplayer->popup_message(id, message);
		}

	return id;
}


Message_Id Player::add_message_with_timeout
	(Game & game, Message & m, uint32_t const timeout, uint32_t const radius)
{
	Map const &       map      = game.map         ();
	uint32_t    const gametime = game.get_gametime();
	Coords      const position = m   .position    ();
	container_iterate_const(MessageQueue, messages(), i)
		if
			(i.current->second->sender() == m.sender()                      and
			 gametime < i.current->second->sent() + timeout                 and
			 map.calc_distance(i.current->second->position(), position) <= radius)
		{
			delete &m;
			return Message_Id::Null();
		}
	return add_message(game, m);
}


/*
===============
Return filtered buildcaps that take the player's territory into account.
===============
*/
NodeCaps Player::get_buildcaps(FCoords const fc) const {
	const Map & map = egbase().map();
	uint8_t buildcaps = fc.field->nodecaps();

	if (not fc.field->is_interior(m_plnum))
		buildcaps = 0;

	// Check if a building's flag can't be build due to ownership
	else if (buildcaps & BUILDCAPS_BUILDINGMASK) {
		FCoords flagcoords;
		map.get_brn(fc, &flagcoords);
		if (not flagcoords.field->is_interior(m_plnum))
			buildcaps &= ~BUILDCAPS_BUILDINGMASK;

		//  Prevent big buildings that would swell over borders.
		if
			((buildcaps & BUILDCAPS_BIG) == BUILDCAPS_BIG
			 and
			 (not map.tr_n(fc).field->is_interior(m_plnum)
			  or
			  not map.tl_n(fc).field->is_interior(m_plnum)
			  or
			  not map. l_n(fc).field->is_interior(m_plnum)))
			buildcaps &= ~BUILDCAPS_SMALL;
	}

	return static_cast<NodeCaps>(buildcaps);
}


/**
 * Build a flag, checking that it's legal to do so. Returns
 * the flag in case of success, else returns 0;
 */
Flag * Player::build_flag(Coords const c) {
	int32_t buildcaps = get_buildcaps(egbase().map().get_fcoords(c));

	if (buildcaps & BUILDCAPS_FLAG)
		return new Flag(egbase(), *this, c);
	return 0;
}


Flag & Player::force_flag(FCoords const c) {
	log("Forcing flag at (%i, %i)\n", c.x, c.y);
	Map const & map = egbase().map();
	if (BaseImmovable * const immovable = c.field->get_immovable()) {
		if (upcast(Flag, existing_flag, immovable)) {
			if (&existing_flag->owner() == this)
				return *existing_flag;
		} else if (not dynamic_cast<Road const *>(immovable)) //  A road is OK.
			immovable->remove(egbase()); //  Make room for the flag.
	}
	MapRegion<Area<FCoords> > mr(map, Area<FCoords>(c, 1));
	do if (upcast(Flag, flag, mr.location().field->get_immovable()))
		flag->remove(egbase()); //  Remove all flags that are too close.
	while (mr.advance(map));

	//  Make sure that the player owns the area around.
	egbase().conquer_area_no_building
		(Player_Area<Area<FCoords> >(player_number(), Area<FCoords>(c, 1)));
	return *new Flag(egbase(), *this, c);
}

/*
===============
Build a road along the given path.
Perform sanity checks (ownership, flags).

Note: the diagnostic log messages aren't exactly errors. They might happen
in some situations over the network.
===============
*/
Road * Player::build_road(const Path & path) {
	Map & map = egbase().map();
	FCoords fc = map.get_fcoords(path.get_start());
	if (upcast(Flag, start, fc.field->get_immovable())) {
		if (upcast(Flag, end, map.get_immovable(path.get_end()))) {

			//  Verify ownership of the path.
			const int32_t laststep = path.get_nsteps() - 1;
			for (int32_t i = 0; i < laststep; ++i) {
				fc = map.get_neighbour(fc, path[i]);

				if (BaseImmovable * const imm = fc.field->get_immovable())
					if (imm->get_size() >= BaseImmovable::SMALL) {
						log
							("%i: building road, immovable in the way, type=%d\n",
							 player_number(), imm->get_type());
						return 0;
					}
				if (!(get_buildcaps(fc) & MOVECAPS_WALK)) {
					log("%i: building road, unwalkable\n", player_number());
					return 0;
				}
			}
			return &Road::create(egbase(), *start, *end, path);
		} else
			log("%i: building road, missed end flag\n", player_number());
	} else
		log("%i: building road, missed start flag\n", player_number());

	return 0;
}


Road & Player::force_road(Path const & path) {
	Map & map = egbase().map();
	FCoords c = map.get_fcoords(path.get_start());
	Flag & start = force_flag(c);
	Flag & end   = force_flag(map.get_fcoords(path.get_end()));

	Path::Step_Vector::size_type const laststep = path.get_nsteps() - 1;
	for (Path::Step_Vector::size_type i = 0; i < laststep; ++i) {
		c = map.get_neighbour(c, path[i]);
		log("Clearing for road at (%i, %i)\n", c.x, c.y);

		//  Make sure that the player owns the area around.
		ref_cast<Game, Editor_Game_Base>(egbase()).conquer_area_no_building
			(Player_Area<Area<FCoords> >(player_number(), Area<FCoords>(c, 1)));

		if (BaseImmovable * const immovable = c.field->get_immovable()) {
			assert(immovable != &start);
			assert(immovable != &end);
			immovable->remove(egbase());
		}
	}
	return Road::create(egbase(), start, end, path);
}


Building & Player::force_building
	(Coords                const location,
	 Building_Index        const idx,
	 bool                  constructionsite)
{
	Map & map = egbase().map();
	FCoords c[4]; //  Big buildings occupy 4 locations.
	c[0] = map.get_fcoords(location);
	map.get_brn(c[0], &c[1]);
	force_flag(c[1]);
	if (BaseImmovable * const immovable = c[0].field->get_immovable())
		immovable->remove(egbase());
	Building_Descr const & descr = *tribe().get_building_descr(idx);
	{
		size_t nr_locations = 1;
		if ((descr.get_size() & BUILDCAPS_SIZEMASK) == BUILDCAPS_BIG) {
			nr_locations = 4;
			map.get_trn(c[0], &c[1]);
			map.get_tln(c[0], &c[2]);
			map.get_ln (c[0], &c[3]);
		}
		for (size_t i = 0; i < nr_locations; ++i) {

			//  Make sure that the player owns the area around.
			egbase().conquer_area_no_building
				(Player_Area<Area<FCoords> >
				 	(player_number(), Area<FCoords>(c[i], 1)));

			if (BaseImmovable * const immovable = c[i].field->get_immovable())
				immovable->remove(egbase());
		}
	}

	if (constructionsite)
		return egbase().warp_constructionsite(c[0], m_plnum, idx);
	else
		return descr.create (egbase(), *this, c[0], false);
}


/*
===============
Place a construction site or building, checking that it's legal to do so.
===============
*/
Building * Player::build
	(Coords c, Building_Index const idx, bool constructionsite)
{
	int32_t buildcaps;

	// Validate building type
	if (not (idx and idx < tribe().get_nrbuildings()))
		return 0;
	Building_Descr const & descr = *tribe().get_building_descr(idx);

	if (!descr.is_buildable())
		return 0;


	// Validate build position
	const Map & map = egbase().map();
	map.normalize_coords(c);
	buildcaps = get_buildcaps(map.get_fcoords(c));

	if (descr.get_ismine()) {
		if (!(buildcaps & BUILDCAPS_MINE))
			return 0;
	} else if
		((buildcaps & BUILDCAPS_SIZEMASK)
		 <
		 descr.get_size() - BaseImmovable::SMALL + 1)
		return 0;

	if (constructionsite)
		return &egbase().warp_constructionsite(c, m_plnum, idx);
	else {
		return &descr.create(egbase(), *this, c, false);
	}
}


/*
===============
Bulldoze the given road, flag or building.
===============
*/
void Player::bulldoze(PlayerImmovable & _imm, bool const recurse)
{
	std::vector<OPtr<PlayerImmovable> > bulldozelist;
	bulldozelist.push_back(&_imm);

	while (bulldozelist.size()) {
		PlayerImmovable * imm = bulldozelist.back().get(egbase());
		bulldozelist.pop_back();
		if (!imm)
			continue;

		// General security check
		if (imm->get_owner() != this)
			return;

		// Destroy, after extended security check
		if (upcast(Building, building, imm)) {
			if (!(building->get_playercaps() & (1 << Building::PCap_Bulldoze)))
				return;

			Flag & flag = building->base_flag();
			building->destroy(egbase());
			//  Now imm and building are dangling reference/pointer! Do not use!

			if (recurse && flag.is_dead_end())
				bulldozelist.push_back(&flag);
		} else if (upcast(Flag, flag, imm)) {
			if (Building * const flagbuilding = flag->get_building())
				if
					(!
					 (flagbuilding->get_playercaps() &
					  (1 << Building::PCap_Bulldoze)))
				{
					log
						("Player trying to rip flag (%u) with undestroyable "
						 "building (%u)\n",
						 flag->serial(), flagbuilding->serial());
					return;
				}

			OPtr<Flag> flagcopy = flag;
			if (recurse) {
				for
					(uint8_t primary_road_id = 6; primary_road_id; --primary_road_id)
				{
					// Recursive bulldoze calls may cause flag to disappear
					if (!flagcopy.get(egbase()))
						return;

					if (Road * const primary_road = flag->get_road(primary_road_id))
					{
						Flag & primary_start =
							primary_road->get_flag(Road::FlagStart);
						Flag & primary_other =
							flag == &primary_start ?
							primary_road->get_flag(Road::FlagEnd) : primary_start;
						primary_road->destroy(egbase());
						log
							("destroying road from (%i, %i) going in dir %u\n",
							 flag->get_position().x, flag->get_position().y,
							 primary_road_id);
						//  The primary road is gone. Now see if the flag at the other
						//  end of it is a dead-end.
						if (primary_other.is_dead_end())
							bulldozelist.push_back(&primary_other);
					}
				}
			}

			// Recursive bulldoze calls may cause flag to disappear
			if (flagcopy.get(egbase()))
				flag->destroy(egbase());
		} else if (upcast(Road, road, imm)) {
			Flag & start = road->get_flag(Road::FlagStart);
			Flag & end = road->get_flag(Road::FlagEnd);

			road->destroy(egbase());
			//  Now imm and road are dangling reference/pointer! Do not use!

			if (recurse) {
				// Destroy all roads between the flags, not just selected
				while (Road * const r = start.get_road(end))
					r->destroy(egbase());

				OPtr<Flag> endcopy = &end;
				if (start.is_dead_end())
					bulldozelist.push_back(&start);
				// At this point, end may have become dangling
				if (Flag * pend = endcopy.get(egbase())) {
					if (pend->is_dead_end())
						bulldozelist.push_back(&end);
				}
			}
		} else
			throw wexception
				("Player::bulldoze(%u): bad immovable type", imm->serial());
	}
}


void Player::start_stop_building(PlayerImmovable & imm) {
	if (&imm.owner() == this)
		if (upcast(ProductionSite, productionsite, &imm))
			productionsite->set_stopped(!productionsite->is_stopped());
}


/*
 * enhance this building, remove it, but give the constructionsite
 * an idea of enhancing
 */
void Player::enhance_building
	(Building * building, Building_Index const index_of_new_building)
{
	if
		(&building->owner() == this
		 and
		 building->descr().enhancements().count(index_of_new_building))
	{
		Building_Index const index_of_old_building =
			tribe().building_index(building->name().c_str());
		const Coords position = building->get_position();

		//  Get workers and soldiers
		//  Make copies of the vectors, because the originals are destroyed with
		//  the building.
		const std::vector<Worker  *> workers  = building->get_workers();

		building->remove(egbase()); //  no fire or stuff
		//  Hereafter the old building does not exist and building is a dangling
		//  pointer.
		building =
			&egbase().warp_constructionsite
				(position, m_plnum, index_of_new_building, index_of_old_building);
		//  Hereafter building points to the new building.

		// Reassign the workers and soldiers.
		// Note that this will make sure they stay within the economy;
		// However, they are no longer associated with the building as
		// workers of that buiding, which is why they will leave for a
		// warehouse.
		container_iterate_const(std::vector<Worker *>, workers, i)
			(*i.current)->set_location(building);
	}
}


/*
===============
Perform an action on the given flag.
===============
*/
void Player::flagaction(Flag & flag)
{
	if (&flag.owner() == this) //  Additional security check.
		flag.add_flag_job
			(ref_cast<Game, Editor_Game_Base>(egbase()),
			 tribe().worker_index("geologist"),
			 "expedition");
}


void Player::allow_worker_type(Ware_Index const i, bool const allow) {
	assert(i.value() < m_allowed_worker_types.size());
	assert(not allow or tribe().get_worker_descr(i)->is_buildable());
	m_allowed_worker_types[i] = allow;
}


/*
 * allow building
 *
 * Disable or enable a building for a player
 */
void Player::allow_building_type(Building_Index const i, bool const allow) {
	assert(i.value() < m_allowed_building_types.size());
	m_allowed_building_types[i] = allow;
}

/*
 * Economy stuff below
 */
void Player::add_economy(Economy & economy)
{
	if (not has_economy(economy))
		m_economies.push_back(&economy);
}


void Player::remove_economy(Economy & economy) {
	container_iterate(Economies, m_economies, i)
		if (*i.current == &economy) {
			m_economies.erase(i.current);
			return;
		}
}

bool Player::has_economy(Economy & economy) const throw () {
	container_iterate_const(Economies, m_economies, i)
		if (*i.current == &economy)
			return true;
	return false;
}

Player::Economies::size_type Player::get_economy_number
	(Economy const * const economy) const
throw ()
{
	Economies::const_iterator const
		economies_end = m_economies.end(), economies_begin = m_economies.begin();
	for
		(Economies::const_iterator it = economies_begin;
		 it != economies_end;
		 ++it)
		if (*it == economy)
			return it - economies_begin;
	assert(false); // never here
	return 0;
}

/************  Military stuff  **********/

/*
==========
Change the training priotity values
==========
*/
void Player::change_training_options
	(TrainingSite & trainingsite, int32_t const atr, int32_t const val)
{
	if (trainingsite.get_owner() == this) {
		tAttribute const attr = static_cast<tAttribute>(atr);
		trainingsite.set_pri(attr, trainingsite.get_pri(attr) + val);
	}
}

/*
===========
Forces the drop of given soldier at given house
===========
*/
void Player::drop_soldier(PlayerImmovable & imm, Soldier & soldier) {
	if (&imm.owner() != this)
		return;
	if (soldier.get_worker_type() != Worker_Descr::SOLDIER)
		return;
	if (upcast(SoldierControl, ctrl, &imm))
		ctrl->dropSoldier(soldier);
}

/*
===========
===========
*/
void Player::allow_retreat_change(bool allow) {
	m_allow_retreat_change = allow;
}

/**
 *   Added check limits of valid values. Percentage limits are configured at
 * tribe level
 * Automatically adjust value if out of limits.
 */
void Player::set_retreat_percentage(uint8_t percentage) {

	if (tribe().get_military_data().get_min_retreat() > percentage)
		m_retreat_percentage = tribe().get_military_data().get_min_retreat();
	else
	if (tribe().get_military_data().get_max_retreat() < percentage)
		m_retreat_percentage = tribe().get_military_data().get_max_retreat();
	else
		m_retreat_percentage = percentage;
}

/**
 * Get a list of soldiers that this player can be used to attack the
 * building at the given flag.
 *
 * The default attack should just take the first N soldiers of the
 * returned array.
 *
 * \todo Perform a meaningful sort on the soldiers array.
 */
uint32_t Player::findAttackSoldiers
	(Flag & flag, std::vector<Soldier *> * soldiers, uint32_t nr_wanted)
{
	uint32_t count = 0;

	if (soldiers)
		soldiers->clear();

	Map & map = egbase().map();
	std::vector<BaseImmovable *> immovables;

	map.find_reachable_immovables_unique
		(Area<FCoords>(map.get_fcoords(flag.get_position()), 25),
		 immovables,
		 CheckStepWalkOn(MOVECAPS_WALK, false),
		 FindImmovablePlayerMilitarySite(*this));

	if (immovables.empty())
		return 0;

	container_iterate_const(std::vector<BaseImmovable *>, immovables, i) {
		MilitarySite const & ms =
			ref_cast<MilitarySite, BaseImmovable>(**i.current);
		std::vector<Soldier *> const present = ms.presentSoldiers();
		uint32_t const nr_staying = ms.minSoldierCapacity();
		uint32_t const nr_present = present.size();
		if (nr_staying < nr_present) {
			uint32_t const nr_taken =
				std::min(nr_wanted, nr_present - nr_staying);
			if (soldiers)
				soldiers->insert
					(soldiers->end(),
					 present.begin(), present.begin() + nr_taken);
			count     += nr_taken;
			nr_wanted -= nr_taken;
			if (not nr_wanted)
				break;
		}
	}

	return count;
}


/**
 * \todo Clean this mess up. The only action we really have right now is
 * to attack, so pretending we have more types is pointless.
 */
void Player::enemyflagaction
	(Flag & flag, Player_Number const attacker, uint32_t const count,
	 uint8_t retreat)
{
	if      (attacker != player_number())
		log
			("Player (%d) is not the sender of an attack (%d)\n",
			 attacker, player_number());
	else if (count == 0)
		log("enemyflagaction: count is 0\n");
	else if (is_hostile(flag.owner()))
		if (Building * const building = flag.get_building())
			if (upcast(Attackable, attackable, building))
				if (attackable->canAttack()) {
					std::vector<Soldier *> attackers;
					findAttackSoldiers(flag, &attackers, count);
					assert(attackers.size() <= count);

					retreat = std::max
						(retreat, tribe().get_military_data().get_min_retreat());
					retreat = std::min
						(retreat, tribe().get_military_data().get_max_retreat());

					container_iterate_const(std::vector<Soldier *>, attackers, i)
						ref_cast<MilitarySite, PlayerImmovable>
							(*(*i.current)->get_location(egbase()))
						.sendAttacker(**i.current, *building, retreat);
				}
}


void Player::rediscover_node
	(Map              const &       map,
	 Widelands::Field const &       first_map_field,
	 FCoords          const f)
throw ()
{

	assert(0 <= f.x);
	assert(f.x < map.get_width());
	assert(0 <= f.y);
	assert(f.y < map.get_height());
	assert(&map[0] <= f.field);
	assert(f.field < &map[0] + map.max_index());

	Field & field = m_fields[f.field - &first_map_field];

	assert(m_fields <= &field);
	assert(&field < m_fields + map.max_index());

	{ // discover everything (above the ground) in this field
		field.terrains = f.field->get_terrains();
		field.roads    = f.field->get_roads   ();
		field.owner    = f.field->get_owned_by();
		{ //  map_object_descr[TCoords::None]

			const Map_Object_Descr * map_object_descr;
			std::string building_animation;
			uint32_t cs_animation_frame = std::numeric_limits<uint32_t>::max();
			if (BaseImmovable * base_immovable = f.field->get_immovable()) {
				map_object_descr = &base_immovable->descr();

				if (Road::IsRoadDescr(map_object_descr))
					map_object_descr = 0;
				else if (upcast(Building, building, base_immovable)) {
					if (building->get_position() != f)
						//  TODO This is not the building's main position so we can
						//  TODO not see it. But it should be possible to see it from
						//  TODO a distance somehow.
						map_object_descr = 0;
					else {
						if (upcast(ConstructionSite, cs, building)) {
							building_animation = cs->get_animation();
							cs_animation_frame = cs->get_animation_frame();
						} else {
							try {
								building->descr().get_animation("unoccupied");
								building_animation = "unoccupied";
							} catch (Map_Object_Descr::Animation_Nonexistent) {
								building->descr().get_animation("idle");
								building_animation = "idle";
							}
						}
					}
				}
			} else
				map_object_descr = 0;
			field.map_object_descr[TCoords<>::None] = map_object_descr;
			field.building_animation = building_animation;
			field.cs_animation_frame = cs_animation_frame;
		}
	}
	{ //  discover the D triangle and the SW edge of the top right neighbour
		FCoords tr = map.tr_n(f);
		Field & tr_field = m_fields[tr.field - &first_map_field];
		if (tr_field.vision <= 1) {
			tr_field.terrains.d = tr.field->terrain_d();
			tr_field.roads &= ~(Road_Mask << Road_SouthWest);
			tr_field.roads |= Road_Mask << Road_SouthWest & tr.field->get_roads();
		}
	}
	{ //  discover both triangles and the SE edge of the top left  neighbour
		FCoords tl = map.tl_n(f);
		Field & tl_field = m_fields[tl.field - &first_map_field];
		if (tl_field.vision <= 1) {
			tl_field.terrains = tl.field->get_terrains();
			tl_field.roads &= ~(Road_Mask << Road_SouthEast);
			tl_field.roads |= Road_Mask << Road_SouthEast & tl.field->get_roads();
		}
	}
	{ //  discover the R triangle and the  E edge of the     left  neighbour
		FCoords l = map.l_n(f);
		Field & l_field = m_fields[l.field - &first_map_field];
		if (l_field.vision <= 1) {
			l_field.terrains.r = l.field->terrain_r();
			l_field.roads &= ~(Road_Mask << Road_East);
			l_field.roads |= Road_Mask << Road_East & l.field->get_roads();
		}
	}
}

void Player::see_node
	(Map              const &       map,
	 Widelands::Field const &       first_map_field,
	 FCoords                  const f,
	 Time                     const gametime,
	 bool                     const forward)
throw ()
{
	assert(0 <= f.x);
	assert(f.x < map.get_width());
	assert(0 <= f.y);
	assert(f.y < map.get_height());
	assert(&map[0] <= f.field);
	assert           (f.field < &first_map_field + map.max_index());

	//  If this is not already a forwarded call, we should inform allied players
	//  as well of this change.
	if (!m_team_player_uptodate)
		update_team_players();
	if (!forward && m_team_player.size()) {
		for (uint8_t j = 0; j < m_team_player.size(); ++j)
			m_team_player[j]->see_node(map, first_map_field, f, gametime, true);
	}

	Field & field = m_fields[f.field - &first_map_field];
	assert(m_fields <= &field);
	assert            (&field < m_fields + map.max_index());
	Vision fvision = field.vision;
	if (fvision == 0)
		fvision = 1;
	if (fvision == 1)
		rediscover_node(map, first_map_field, f);
	++fvision;
	field.vision = fvision;
}

void Player::unsee_node
	(Map_Index const i, Time const gametime, bool const forward)
throw ()
{
	Field & field = m_fields[i];
	if (field.vision <= 1) { //  Already does not see this
		//  FIXME This must never happen!
		log("ERROR: Decreasing vision for node that is not seen. Report bug!\n");
		return;
	}

	//  If this is not already a forwarded call, we should inform allied players
	//  as well of this change.
	if (!m_team_player_uptodate)
		update_team_players();
	if (!forward && m_team_player.size()) {
		for (uint8_t j = 0; j < m_team_player.size(); ++j)
			m_team_player[j]->unsee_node(i, gametime, true);
	}

	--field.vision;
	if (field.vision == 1)
		field.time_node_last_unseen = gametime;
	assert(1 <= field.vision);
}


/**
 * Called by Game::think to sample statistics data in regular intervals.
 */
void Player::sample_statistics()
{
	assert (m_ware_productions.size() == tribe().get_nrwares().value());

	for (uint32_t i = 0; i < m_ware_productions.size(); ++i) {
		m_ware_productions[i].push_back(m_current_statistics[i]);
		m_current_statistics[i] = 0;
	}
}


/**
 * A ware was produced. Update the corresponding statistics.
 */
void Player::ware_produced(Ware_Index const wareid) {
	assert (m_ware_productions.size() == tribe().get_nrwares().value());
	assert(wareid.value() < tribe().get_nrwares().value());

	++m_current_statistics[wareid];
}


/**
 * Get current ware production statistics
 */
const std::vector<uint32_t> * Player::get_ware_production_statistics
		(Ware_Index const ware) const
{
	assert(ware.value() < m_ware_productions.size());

	return &m_ware_productions[ware];
}


/**
 * Add or remove the given building from building statistics.
 * Only to be called by \ref receive
 */
void Player::update_building_statistics
	(Building & building, losegain_t const lg)
{
	upcast(ConstructionSite const, constructionsite, &building);
	const std::string & building_name =
		constructionsite ?
		constructionsite->building().name() : building.name();

	Building_Index const nr_buildings = tribe().get_nrbuildings();

	// Get the valid vector for this
	if (m_building_stats.size() < nr_buildings.value())
		m_building_stats.resize(nr_buildings.value());

	std::vector<Building_Stats> & stat =
		m_building_stats[tribe().building_index(building_name.c_str())];

	if (lg == GAIN) {
		Building_Stats new_building;
		new_building.is_constructionsite = constructionsite;
		new_building.pos = building.get_position();
		stat.push_back(new_building);
	} else {
		Coords const building_position = building.get_position();
		for (uint32_t i = 0; i < stat.size(); ++i) {
			if (stat[i].pos == building_position) {
				stat.erase(stat.begin() + i);
				return;
			}
		}

		throw wexception
			("Interactive_Player::loose_immovable(): A building should be "
			 "removed at (%i, %i), but nothing is known about this building!",
			 building_position.x, building_position.y);
	}
}


void Player::receive(NoteImmovable const & note)
{
	if (upcast(Building, building, note.pi))
		update_building_statistics(*building, note.lg);

	NoteSender<NoteImmovable>::send(note);
}


void Player::receive(NoteFieldPossession const & note)
{
	NoteSender<NoteFieldPossession>::send(note);
}

void Player::setAI(const std::string & ai)
{
	m_ai = ai;
}

const std::string & Player::getAI() const
{
	return m_ai;
}

/**
 * Read statistics data from a file.
 *
 * \param fr source stream
 * \param version indicates the kind of statistics file, which may be
 *   0 - old style statistics (before WiHack 2010)
 *   1 - statistics with ware names
 */
void Player::ReadStatistics(FileRead & fr, uint32_t const version)
{
	if (version == 1) {
		uint16_t nr_wares = fr.Unsigned16();
		uint16_t nr_entries = fr.Unsigned16();

		for (uint32_t i = 0; i < m_current_statistics.size(); ++i)
			m_ware_productions[i].resize(nr_entries);

		for (uint16_t i = 0; i < nr_wares; ++i) {
			std::string name = fr.CString();
			Ware_Index idx = tribe().ware_index(name);
			if (!idx) {
				log
					("Player %u statistics: unknown ware name %s",
					 player_number(), name.c_str());
				continue;
			}

			m_current_statistics[idx] = fr.Unsigned32();

			for (uint32_t j = 0; j < nr_entries; ++j)
				m_ware_productions[idx][j] = fr.Unsigned32();
		}
	} else if (version == 0) {
		uint16_t nr_wares = fr.Unsigned16();
		uint16_t nr_entries = fr.Unsigned16();

		if (nr_wares > 0) {
			if (nr_wares == tribe().get_nrwares().value()) {
				assert(m_ware_productions.size() == nr_wares);
				assert(m_current_statistics.size() == nr_wares);

				for (uint32_t i = 0; i < m_current_statistics.size(); ++i) {
					m_current_statistics[i] = fr.Unsigned32();
					m_ware_productions[i].resize(nr_entries);

					for (uint32_t j = 0; j < m_ware_productions[i].size(); ++j)
						m_ware_productions[i][j] = fr.Unsigned32();
				}
			} else {
				log
					("Statistics for player %u (%s) has %u ware types "
					 "(should be %u). Statistics will be discarded.",
					 player_number(), tribe().name().c_str(),
					 nr_wares, tribe().get_nrwares().value());

				// Eat and discard all data
				for (uint32_t i = 0; i < nr_wares; ++i) {
					fr.Unsigned32();

					for (uint32_t j = 0; j < nr_entries; ++j)
						fr.Unsigned32();
				}
			}
		}
	} else
		throw wexception("Unsupported version %i", version);
}


/**
 * Write statistics data to the give file
 */
void Player::WriteStatistics(FileWrite & fw) const {
	fw.Unsigned16(m_current_statistics.size());
	fw.Unsigned16(m_ware_productions[0].size());

	for (uint8_t i = 0; i < m_current_statistics.size(); ++i) {
		fw.CString
			(tribe().get_ware_descr
			 (Ware_Index(static_cast<Ware_Index::value_t>(i)))->name());
		fw.Unsigned32(m_current_statistics[i]);
		for (uint32_t j = 0; j < m_ware_productions[i].size(); ++j)
			fw.Unsigned32(m_ware_productions[i][j]);
	}
}

}
