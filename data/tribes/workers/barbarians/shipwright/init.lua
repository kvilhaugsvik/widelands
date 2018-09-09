dirname = path.dirname(__file__)

animations = {}
add_animation(animations, "idle", dirname, "idle", {11, 23})
add_animation(animations, "work", dirname, "work", {11, 26}, 10)
animations["work"]["sound_effect"] = {
   name = "hammering",
   directory = "sound/hammering"
}
add_walking_animations(animations, "walk", dirname, "walk", {9, 24}, 10)
add_walking_animations(animations, "walkload", dirname, "walkload", {11, 22}, 10)


tribes:new_worker_type {
   msgctxt = "barbarians_worker",
   name = "barbarians_shipwright",
   -- TRANSLATORS: This is a worker name used in lists of workers
   descname = pgettext("barbarians_worker", "Shipwright"),
   helptext_script = dirname .. "helptexts.lua",
   icon = dirname .. "menu.png",
   vision_range = 2,

   buildcost = {
      barbarians_carrier = 1,
      hammer = 1
   },

   programs = {
      buildship = {
         "walk=object-or-coords",
         "plant=attrib:shipconstruction unless object",
         "playsound=sound/sawmill sawmill 230",
         "animate=work 500",
         "construct",
         "animate=work 5000",
         "return"
      }
   },

   animations = animations,
}
