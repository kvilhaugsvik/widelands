-- =======================================================================
--                                 Player 1
-- =======================================================================
p1:forbid_buildings("all")
p1:allow_buildings{
   "foresters_house",
   "lumberjacks_house",
   "quarry",
   "sawmill",
}
prefilled_buildings(p1,
   {"headquarters", 16, 21,
      wares = {
         helm = 4,
         wood_lance = 5,
         ax = 6,
         bread_paddle = 2,
         basket = 1,
         bread = 8,
         cloth = 5,
         coal = 12,
         fire_tongs = 2,
         fish = 6,
         fishing_rod = 2,
         flour = 4,
         gold = 4,
         grape = 4,
         hammer = 12,
         hunting_spear = 2,
         iron = 12,
         ironore = 5,
         kitchen_tools = 4,
         marble = 25,
         marblecolumn = 6,
         meal = 4,
         meat = 6,
         pick = 14,
         ration = 12,
         saw = 3,
         scythe = 5,
         shovel = 6,
         stone = 40,
         log = 30,
         water = 12,
         wheat = 4,
         wine = 8,
         wood = 45,
         wool = 2,
      },
      workers = {
         armorsmith = 1,
         brewer = 1,
         builder = 10,
         carrier = 40,
         charcoal_burner = 1,
         geologist = 4,
         lumberjack = 3,
         miner = 4,
         stonemason = 2,
         toolsmith = 2,
         weaponsmith = 1,
         donkey = 20,
      },
      soldiers = {
         [{0,0,0,0}] = 45,
      }
   }
)

-- =======================================================================
--                                 Player 2
-- =======================================================================
p2:forbid_buildings("all")
p2:allow_buildings{
   "bakery",
   "barrier",
   "farm",
   "reed_yard",
   "fishers_hut",
   "hardener",
   "hunters_hut",
   "lumberjacks_hut",
   "micro-brewery",
   "rangers_hut",
   "sentry",
   "lime_kiln",
   "tavern",
   "well",
}

prefilled_buildings(p2,
   {"headquarters", 60, 65,
   wares = {
      ax = 6,
      bread_paddle = 2,
      blackwood = 32,
      cloth = 5,
      coal = 12,
      fire_tongs = 2,
      fish = 6,
      fishing_rod = 2,
      gold = 4,
      grout = 12,
      hammer = 12,
      hunting_spear = 2,
      iron = 12,
      ironore = 5,
      kitchen_tools = 4,
      meal = 4,
      meat = 6,
      pick = 14,
      pittabread = 8,
      ration = 12,
      raw_stone = 40,
      scythe = 6,
      shovel = 4,
      snack = 3,
      thatchreed = 24,
      log = 80,
   },
   workers = {
      blacksmith = 2,
      brewer = 1,
      builder = 10,
      carrier = 40,
      charcoal_burner = 1,
      gardener = 1,
      geologist = 4,
      ["lime-burner"] = 1,
      lumberjack = 3,
      miner = 4,
      ranger = 1,
      stonemason = 2,
   },
   soldiers = {
      [{0,0,0,0}] = 45,
   }
})

