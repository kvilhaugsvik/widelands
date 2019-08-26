dirname = path.dirname (__file__)

tribes:new_productionsite_type {
   msgctxt = "amazons_building",
   name = "amazons_hunter_gatherers_hut",
   -- TRANSLATORS: This is a building name used in lists of buildings
   descname = pgettext ("amazons_building", "Hunter-Gatherer's Hut"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   size = "small",

   buildcost = {
      log = 3,
      rope = 1
   },
   return_on_dismantle = {
      log = 1,
      rope = 1
   },

   animations = {
      idle = {
         pictures = path.list_files (dirname .. "idle_??.png"),
         hotspot = {49, 86},
         fps = 10,
      },
      unoccupied = {
         pictures = path.list_files (dirname .. "unoccupied_?.png"),
         hotspot = {49, 64},
      },
   },

   aihints = {
      collects_ware_from_map = "meat",
      collects_ware_from_map = "fish",
      prohibited_till = 480
   },

   indicate_workarea_overlaps = {
      amazons_hunter_gatherers_hut = false,
      amazons_wilderness_keepers_tent = true
   },

   working_positions = {
      amazons_hunter_gatherer = 1
   },

   outputs = {
      "meat",
      "fish"
   },

   programs = {
      work = {
         -- TRANSLATORS: Completed/Skipped/Did not start hunting because ...
         descname = _"gathering",
         actions = {
            "callworker=hunt",
            "sleep=30000",
            "callworker=fish",
            "sleep=30000",
         }
      },
   },
   out_of_resource_notification = {
      -- Translators: Short for "Out of Game and out of fish" for a resource
      title = _"No Game, No Fish",
      -- TRANSLATORS: "Game" means animals that you can hunt
      heading = _"Out of Game and Fish",
      -- TRANSLATORS: "game" means animals that you can hunt
      message = pgettext("amazons_building", "The hunter-gatherer working out of this hunter-gatherer’s hut can’t find any game or fish in his work area."),
      productivity_threshold = 33
   },
}
