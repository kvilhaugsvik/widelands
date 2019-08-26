-- This include can be removed when all help texts have been defined.
include "tribes/scripting/help/global_helptexts.lua"

function building_helptext_lore()
   -- TRANSLATORS#: Lore helptext for a building
   return no_lore_text_yet()
end

function building_helptext_lore_author()
   -- TRANSLATORS#: Lore author helptext for a building
   return no_lore_author_text_yet()
end

function building_helptext_purpose()
   -- TRANSLATORS: Purpose helptext for a building
   return pgettext("building", "Hunts animals to produce meat. Catches fish in the waters.")
end

function building_helptext_note()
   -- TRANSLATORS: Note helptext for a building
   return pgettext("amazons_building", "The hunter-gatherer’s house needs animals or fish to hunt or catch within the work area.")
end

function building_helptext_performance()
   -- TRANSLATORS: Performance helptext for a building
   return pgettext("amazons_building", "The hunter-gatherer pauses %s before going to work again."):bformat(ngettext("%d second", "%d seconds", 30):bformat(30))
end
