#include <stdio.h>

#include "ai-objective.h"
#include "ai-debug.h"

unit_configuration_map::const_iterator choose_best_unit(const round& r, 
		const civilization& myciv, const city& c,
		unit_comp_func_t comp_func, unit_accept_func_t accept_func)
{
	unit_configuration_map::const_iterator chosen = r.uconfmap.end();
	for(unit_configuration_map::const_iterator it = r.uconfmap.begin();
			it != r.uconfmap.end();
			++it) {
		if(!myciv.can_build_unit(it->second, c))
			continue;
		if(accept_func(it->second)) {
			if(chosen == r.uconfmap.end() || 
				comp_func(it->second, chosen->second)) {
				chosen = it;
			}
		}
	}
	return chosen;
}

objective::objective(round* r_, civilization* myciv_, const std::string& obj_name_)
	: r(r_), myciv(myciv_), obj_name(obj_name_)
{
}

city_production objective::best_unit_production(const city& c, int* points,
		unit_comp_func_t comp_func, unit_accept_func_t accept_func) const
{
	unit_configuration_map::const_iterator chosen = choose_best_unit(*r,
			*myciv, c, comp_func,
			accept_func);
	if(chosen != r->uconfmap.end()) {
		unit dummy(0, chosen->first, c.xpos, c.ypos, myciv->civ_id,
				chosen->second, r->get_num_road_moves());
		*points = get_unit_points(dummy);
		ai_debug_printf(myciv->civ_id, "%s: %s: %d\n",
				obj_name.c_str(),
				chosen->second.unit_name.c_str(), *points);
		return city_production(true, chosen->first);
	}
	else {
		return city_production(true, -1);
	}
}

void objective::process(std::set<unsigned int>* freed_units)
{
	ordersmap_t::iterator oit = ordersmap.begin();
	while(oit != ordersmap.end()) {
		std::map<unsigned int, unit*>::iterator uit = myciv->units.find(oit->first);
		if(uit == myciv->units.end()) {
			// unit lost
			ordersmap.erase(oit++);
		}
		else {
			if(oit->second->finished()) {
				oit->second->replan();
			}
			action a = oit->second->get_action();
			int success = r->perform_action(myciv->civ_id, a);
			if(!success) {
				ai_debug_printf(myciv->civ_id, "%s - %s - %d: could not perform action: %s.\n", 
						obj_name.c_str(), uit->second->uconf.unit_name.c_str(),
						uit->second->unit_id, a.to_string().c_str());
				oit->second->replan();
				action a = oit->second->get_action();
				success = r->perform_action(myciv->civ_id, a);
				if(!success) {
					ai_debug_printf(myciv->civ_id, "%s: still could not perform action: %s.\n",
							obj_name.c_str(), a.to_string().c_str());
					freed_units->insert(oit->first);
					ordersmap.erase(oit++);
				}
				else {
					++oit;
				}
			}
			else {
				oit->second->drop_action();
				++oit;
			}
		}
	}
}

const std::string& objective::get_name() const
{
	return obj_name;
}


