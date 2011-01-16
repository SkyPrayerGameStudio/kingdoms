#include <sstream>
#include <boost/bind.hpp>
#include <boost/lambda/lambda.hpp>
#include "map-astar.h"
#include "city_window.h"
#include "diplomacy_window.h"
#include "discovery_window.h"
#include "production_window.h"
#include "serialize.h"

#include "game_window.h"

game_window::game_window(SDL_Surface* screen_, gui_data& data_, gui_resources& res_,
		ai* ai_, civilization* myciv_)
	: main_window(screen_, data_, res_, 4),
	current_unit(myciv_->units.end()),
	blink_unit(false),
	timer(0),
	myciv(myciv_),
	internal_ai(ai_),
	sidebar_info_display(coord(-1, -1)),
	action_button_action(action_none)
{
}

game_window::~game_window()
{
	clear_action_buttons();
}

int game_window::draw_window()
{
	if (SDL_MUSTLOCK(screen)) {
		if (SDL_LockSurface(screen) < 0) {
			return 1;
		}
	}
	draw_sidebar();
	clear_main_map();
	draw_main_map();
	if(!blink_unit && current_unit != myciv->units.end()) {
		draw_unit(current_unit->second);
	}
	draw_overlays();
	if (SDL_MUSTLOCK(screen)) {
		SDL_UnlockSurface(screen);
	}
	if(SDL_Flip(screen)) {
		fprintf(stderr, "Unable to flip: %s\n", SDL_GetError());
		return 1;
	}
	return 0;
}

char game_window::fog_on_tile(int x, int y) const
{
	if(internal_ai)
		return 2;
	else
		return myciv->fog_at(x, y);
}

bool game_window::city_info_available(const city& c) const
{
	return internal_ai != NULL || c.civ_id == myciv->civ_id;
}

const std::set<unsigned int>* game_window::discovered_advances() const
{
	return internal_ai != NULL ? NULL : &myciv->researched_advances;
}

int game_window::process(int ms)
{
	if(internal_ai)
		return 0;
	int old_timer = timer;
	timer += ms;
	bool old_blink_unit = blink_unit;

	if(timer % 1000 < 300) {
		blink_unit = true;
	}
	else {
		blink_unit = false;
	}
	if(blink_unit != old_blink_unit) {
		draw();
	}
	if(num_subwindows() == 0) {
		if(old_timer / 200 != timer / 200) {
			int x, y;
			SDL_GetMouseState(&x, &y);
			handle_mousemotion(x, y);
		}
	}
	else {
		// check if a unit was activated in a city
		if((current_unit == myciv->units.end()) &&
			(old_timer / 200 != timer / 200)) {
			get_next_free_unit();
		}
	}
	handle_civ_messages(&myciv->messages);
	return 0;
}

int game_window::handle_window_input(const SDL_Event& ev)
{
	city* c = NULL;
	action a = internal_ai ? observer_action(ev) : input_to_action(ev);
	int was_action = try_perform_action(a, &c);
	if(!was_action) {
		check_unit_movement_orders();
		handle_input_gui_mod(ev, &c);
	}
	update_view();
	if(c) {
		add_subwindow(new city_window(screen, data, res, c,
					internal_ai, myciv));
	}
	return a.type == action_give_up;
}

void game_window::handle_input_gui_mod(const SDL_Event& ev, city** c)
{
	main_window::handle_input_gui_mod(ev);
	switch(ev.type) {
		case SDL_KEYDOWN:
			{
				SDLKey k = ev.key.keysym.sym;
				if(k == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL)) {
					save_game("manual", data.r);
					add_gui_msg("Game saved.");
				}
				if(!internal_ai && current_unit != myciv->units.end()) {
					if(k == SDLK_c) {
						center_camera_to_unit(current_unit->second);
					}
					else if(k == SDLK_w) {
						unit_wait();
					}
					else if(k == SDLK_KP_ENTER) {
						*c = data.m.city_on_spot(current_unit->second->xpos, current_unit->second->ypos);
					}
				}
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			handle_mouse_down(ev, c);
			break;
		default:
			break;
	}
}

int game_window::handle_mouse_down(const SDL_Event& ev, city** c)
{
	if(mouse_down_sqx >= 0) {
		try_choose_with_mouse(c);
	}
	return 0;
}

void game_window::clear_action_buttons()
{
	while(!action_buttons.empty()) {
		delete action_buttons.front();
		action_buttons.pop_front();
	}
}

void game_window::get_next_free_unit()
{
	clear_action_buttons();
	if(myciv->units.empty())
		return;
	std::map<unsigned int, unit*>::const_iterator uit = current_unit;
	for(++current_unit;
			current_unit != myciv->units.end();
			++current_unit) {
		if(current_unit->second->idle()) {
			try_center_camera_to_unit(current_unit->second);
			draw();
			check_unit_movement_orders();
			update_action_buttons();
			return;
		}
	}

	// run through the first half
	for(current_unit = myciv->units.begin();
			current_unit != uit;
			++current_unit) {
		if(current_unit->second->idle()) {
			try_center_camera_to_unit(current_unit->second);
			draw();
			check_unit_movement_orders();
			update_action_buttons();
			return;
		}
	}
	current_unit = myciv->units.end();
}

int game_window::clear_sidebar() const
{
	SDL_Rect dest;
	dest.x = 0;
	dest.y = 0;
	dest.w = sidebar_size * tile_w;
	dest.h = screen->h;
	Uint32 color = SDL_MapRGB(screen->format, 0, 0, 0);
	SDL_FillRect(screen, &dest, color);
	return 0;
}

int game_window::draw_sidebar() const
{
	clear_sidebar();
	draw_minimap();
	draw_civ_info();
	if(!internal_ai && current_unit != myciv->units.end())
		draw_unit_info();
	else
		draw_eot();
	display_tile_info();
	return 0;
}

int game_window::draw_civ_info() const
{
	draw_text(screen, &res.font, myciv->civname.c_str(), 10, sidebar_size * tile_h / 2 + 40, 255, 255, 255);
	char buf[256];
	buf[255] = '\0';
	snprintf(buf, 255, "Gold: %d", myciv->gold);
	draw_text(screen, &res.font, buf, 10, sidebar_size * tile_h / 2 + 60, 255, 255, 255);
	int lux = 10 - myciv->alloc_gold - myciv->alloc_science;
	snprintf(buf, 255, "%d/%d/%d", 
			myciv->alloc_gold * 10,
			myciv->alloc_science * 10,
			lux);
	draw_text(screen, &res.font, buf, 10, sidebar_size * tile_h / 2 + 80, 255, 255, 255);
	return 0;
}

std::string game_window::unit_strength_info_string(const unit* u) const
{
	if(u->uconf->max_strength == 0)
		return std::string("");
	char buf[256];
	buf[255] = '\0';
	snprintf(buf, 255, "%d.%d/%d.0", u->strength / 10, u->strength % 10,
			u->uconf->max_strength);
	return std::string(buf);
}

int game_window::draw_unit_info() const
{
	if(current_unit == myciv->units.end())
		return 0;
	const unit_configuration* uconf = data.r.get_unit_configuration((current_unit->second)->uconf_id);
	if(!uconf)
		return 1;
	draw_text(screen, &res.font, uconf->unit_name.c_str(), 10, sidebar_size * tile_h / 2 + 100, 255, 255, 255);
	char buf[256];
	buf[255] = '\0';
	snprintf(buf, 255, "Moves: %-2d/%2d", current_unit->second->num_moves(), uconf->max_moves);
	draw_text(screen, &res.font, buf, 10, sidebar_size * tile_h / 2 + 120, 255, 255, 255);
	if(current_unit->second->strength) {
		snprintf(buf, 255, "Unit strength:");
		draw_text(screen, &res.font, buf, 10, sidebar_size * tile_h / 2 + 140, 255, 255, 255);
		draw_text(screen, &res.font, unit_strength_info_string(current_unit->second).c_str(),
				10, sidebar_size * tile_h / 2 + 160, 255, 255, 255);
	}
	int drawn_carried_units = 0;
	for(std::list<unit*>::const_iterator it = current_unit->second->carried_units.begin();
			it != current_unit->second->carried_units.end();
			++it) {
		SDL_Surface* unit_tile = res.get_unit_tile(**it,
				myciv->col);
		draw_image(10, sidebar_size * tile_h / 2 + 180 + drawn_carried_units * 36, unit_tile, screen);
		drawn_carried_units++;
	}
	return 0;
}

bool game_window::write_unit_info(const unit* u, int* written_lines) const
{
	if(*written_lines >= 6) {
		draw_text(screen, &res.font, "<More>", 10,
				screen->h - 160 + *written_lines * 16, 255, 255, 255);
		return true;
	}
	std::string strength = unit_strength_info_string(u);
	char buf[256];
	buf[255] = '\0';
	if(!strength.empty())
		snprintf(buf, 255, "%s (%s)",
				u->uconf->unit_name.c_str(),
				strength.c_str());
	else
		snprintf(buf, 255, "%s", u->uconf->unit_name.c_str());
	draw_text(screen, &res.font, buf, 10,
			screen->h - 160 + *written_lines * 16, 255, 255, 255);
	(*written_lines)++;
	if(u->civ_id == (int)myciv->civ_id) {
		for(std::list<unit*>::const_iterator it = u->carried_units.begin();
				it != u->carried_units.end(); ++it) {
			if(write_unit_info(*it, written_lines))
				return true;
		}
	}
	return false;
}

void game_window::display_tile_info() const
{
	if(sidebar_info_display.x < 0)
		return;
	char fog = myciv->fog_at(sidebar_info_display.x, sidebar_info_display.y);
	if(fog == 0)
		return;
	int terr = data.m.get_data(sidebar_info_display.x, sidebar_info_display.y);
	if(terr >= 0 && terr < num_terrain_types) {
		draw_text(screen, &res.font, data.m.resconf.resource_name[terr].c_str(), 10, screen->h - 160, 255, 255, 255);
	}
	if(fog == 1)
		return;
	int written_lines = 1;
	const std::list<unit*>& units = data.m.units_on_spot(sidebar_info_display.x,
			sidebar_info_display.y);
	for(std::list<unit*>::const_iterator it = units.begin();
			it != units.end(); ++it) {
		if(write_unit_info(*it, &written_lines))
			break;
	}
	if(!units.empty()) {
		std::list<unit*>::const_iterator it = units.begin();
		if(current_unit != myciv->units.end() &&
				(*it)->civ_id != (int)myciv->civ_id) {
			unsigned int u1chance, u2chance;
			if(data.r.combat_chances(current_unit->second, *it,
						&u1chance, &u2chance)) {
				if(u1chance || u2chance) {
					char buf[256];
					buf[255] = '\0';
					snprintf(buf, 255, "%d vs %d => %2.2f",
							current_unit->second->strength,
							(*it)->strength, u1chance / (u1chance + (float)u2chance));
					draw_text(screen, &res.font, "Combat:", 10,
							screen->h - 160 + (written_lines + 1) * 16, 255, 255, 255);
					draw_text(screen, &res.font, buf, 10,
							screen->h - 160 + (written_lines + 2) * 16, 255, 255, 255);
				}
			}
		}
	}
}

int game_window::draw_eot() const
{
	return draw_text(screen, &res.font, "End of turn", 10, screen->h - 176, 255, 255, 255);
}

int game_window::draw_line_by_sq(const coord& c1, const coord& c2, int r, int g, int b)
{
	coord start(tile_xcoord_to_pixel(c1.x) + tile_w / 2,
			tile_ycoord_to_pixel(c1.y) + tile_h / 2);
	coord end(tile_xcoord_to_pixel(c2.x) + tile_w / 2,
			tile_ycoord_to_pixel(c2.y) + tile_h / 2);
	draw_line(screen, start.x, start.y, end.x, end.y, color(r, g, b));
	return 0;
}

bool game_window::can_draw_unit(const unit* u) const
{
	if(u->carried()) {
		return false;
	}
	if(internal_ai) {
		return true;
	}
	if(current_unit == myciv->units.end()) {
	       return true;
	}
	if(u == current_unit->second) {
		return false;
	}
	if(u->xpos == current_unit->second->xpos && u->ypos == current_unit->second->ypos) {
		return false;
	}
	return true;
}

void game_window::draw_overlays()
{
	if(!path_to_draw.empty()) {
		std::list<coord>::const_iterator cit = path_to_draw.begin();
		std::list<coord>::const_iterator cit2 = path_to_draw.begin();
		cit2++;
		while(cit2 != path_to_draw.end()) {
			draw_line_by_sq(*cit, *cit2, 255, 255, 255);
			cit++;
			cit2++;
		}
	}
	int xp = sidebar_size * tile_w + 10;
	int yp = screen->h - 32;
	for(std::list<std::string>::const_reverse_iterator it = gui_msg_queue.rbegin();
			it != gui_msg_queue.rend();
			++it) {
		draw_text(screen, &res.font, it->c_str(), xp, yp, 255, 255, 255);
		yp -= 16;
		if(yp < 32)
			break;
	}
	draw_action_buttons();
}

void game_window::draw_action_buttons()
{
	std::for_each(action_buttons.begin(), action_buttons.end(), std::bind2nd(std::mem_fun(&button::draw), screen));
}

rect game_window::action_button_dim(int num) const
{
	static const int button_width = 48;
	int button_num = num % 3;
	int button_col = button_num == 0 ? 0 : button_num == 1 ? -1 : 1;
	return rect(screen->w / 2 - button_width / 2 + button_col * button_width,
			screen->h - 64 - 32 * (num / 3),
			button_width,
			32);
}

void game_window::update_action_buttons()
{
	clear_action_buttons();
	if(current_unit != myciv->units.end()) {
		static const color action_button_color(158, 86, 0);
		if(current_unit->second->idle()) {
			// wait
			action_buttons.push_back(new plain_button(action_button_dim(0),
						"Wait", &res.font, action_button_color, color(0, 0, 0),
						boost::bind(&game_window::unit_wait, this)));
			// center
			action_buttons.push_back(new plain_button(action_button_dim(1),
						"Center", &res.font, action_button_color, color(0, 0, 0),
						boost::bind(&game_window::unit_center, this)));
			// skip turn
			action_buttons.push_back(new plain_button(action_button_dim(2),
						"Skip", &res.font, action_button_color, color(0, 0, 0),
						boost::bind(&game_window::unit_skip, this)));
			// fortify
			action_buttons.push_back(new plain_button(action_button_dim(3),
						"Fortify", &res.font, action_button_color, color(0, 0, 0),
						boost::bind(&game_window::unit_fortify, this)));
			// found city
			if(current_unit->second->is_settler() &&
					data.m.can_found_city_on(current_unit->second->xpos,
						current_unit->second->ypos)) {
				action_buttons.push_back(new plain_button(action_button_dim(4),
							"Found", &res.font, action_button_color, color(0, 0, 0),
							boost::bind(&game_window::unit_found_city, this)));
			}
			// load
			if(data.r.can_load_unit(current_unit->second,
						current_unit->second->xpos,
						current_unit->second->ypos)) {
					action_buttons.push_back(new plain_button(action_button_dim(5),
								"Load", &res.font, action_button_color, color(0, 0, 0),
								boost::bind(&game_window::unit_load, this)));
			}
			if(current_unit->second->uconf->worker) {
				// road
				if(data.m.can_improve_terrain(current_unit->second->xpos,
						current_unit->second->ypos,
						myciv->civ_id, improv_road)) {
					action_buttons.push_back(new plain_button(action_button_dim(6),
								"Road", &res.font, action_button_color, color(0, 0, 0),
								boost::bind(&game_window::unit_improve, this, improv_road)));
				}
				// irrigate
				if(data.m.can_improve_terrain(current_unit->second->xpos,
						current_unit->second->ypos,
						myciv->civ_id, improv_irrigation)) {
					action_buttons.push_back(new plain_button(action_button_dim(7),
								"Irrigate", &res.font, action_button_color, color(0, 0, 0),
								boost::bind(&game_window::unit_improve, this, improv_irrigation)));
				}
				// mine
				if(data.m.can_improve_terrain(current_unit->second->xpos,
						current_unit->second->ypos,
						myciv->civ_id, improv_mine)) {
					action_buttons.push_back(new plain_button(action_button_dim(8),
								"Mine", &res.font, action_button_color, color(0, 0, 0),
								boost::bind(&game_window::unit_improve, this, improv_mine)));
				}
			}
			// unload
			if(current_unit->second->carrying()) {
					action_buttons.push_back(new plain_button(action_button_dim(9),
								"Unload", &res.font, action_button_color, color(0, 0, 0),
								boost::bind(&game_window::unit_unload, this)));
			}
		}
	}
}

int game_window::unit_wait()
{
	std::map<unsigned int, unit*>::const_iterator old_it = current_unit;
	get_next_free_unit();
	if(current_unit == myciv->units.end()) {
		current_unit = old_it;
		update_action_buttons();
	}
	return 0;
}

int game_window::unit_center()
{
	center_camera_to_unit(current_unit->second);
	return 0;
}

int game_window::unit_skip()
{
	action_button_action = unit_action(action_skip, current_unit->second);
	return 1;
}

int game_window::unit_fortify()
{
	action_button_action = unit_action(action_fortify, current_unit->second);
	return 1;
}

int game_window::unit_found_city()
{
	action_button_action = unit_action(action_found_city, current_unit->second);
	return 1;
}

int game_window::unit_improve(improvement_type i)
{
	action_button_action = improve_unit_action(current_unit->second, i);
	return 1;
}

int game_window::unit_load()
{
	action_button_action = unit_action(action_load, current_unit->second);
	return 1;
}

int game_window::unit_unload()
{
	action_button_action = unit_action(action_unload, current_unit->second);
	return 1;
}

void game_window::update_tile_info(int x, int y)
{
	int sqx, sqy;
	mouse_coord_to_tiles(x, y, &sqx, &sqy);
	if(sqx >= 0) {
		sidebar_info_display = coord(sqx, sqy);
		draw_sidebar();
	}
}

int game_window::handle_mousemotion(int x, int y)
{
	const int border = tile_w;
	try_move_camera(x >= sidebar_size * tile_w && x < sidebar_size * tile_w + border,
			x > screen->w - border,
			y < border,
			y > screen->h - border);
	check_line_drawing(x, y);
	update_tile_info(x, y);
	return 0;
}

int game_window::check_line_drawing(int x, int y)
{
	if(current_unit == myciv->units.end())
		return 0;
	if(mouse_down_sqx >= 0) {
		int curr_sqx, curr_sqy;
		mouse_coord_to_tiles(x, y, &curr_sqx, &curr_sqy);
		coord curr(curr_sqx, curr_sqy);
		if(curr_sqx != mouse_down_sqx || curr_sqy != mouse_down_sqy) {
			if(path_to_draw.empty() || 
					path_to_draw.back() != curr) {
				path_to_draw = map_astar(*myciv, *current_unit->second, 
						false,
						coord(current_unit->second->xpos,
							current_unit->second->ypos),
						curr);
				mouse_down_sqx = curr_sqx;
				mouse_down_sqy = curr_sqy;
			}
		}
	}
	return 0;
}

void game_window::numpad_to_move(SDLKey k, int* chx, int* chy) const
{
	*chx = 0; *chy = 0;
	switch(k) {
		case SDLK_KP4:
			*chx = -1;
			break;
		case SDLK_KP6:
			*chx = 1;
			break;
		case SDLK_KP8:
			*chy = -1;
			break;
		case SDLK_KP2:
			*chy = 1;
			break;
		case SDLK_KP1:
			*chx = -1;
			*chy = 1;
			break;
		case SDLK_KP3:
			*chx = 1;
			*chy = 1;
			break;
		case SDLK_KP7:
			*chx = -1;
			*chy = -1;
			break;
		case SDLK_KP9:
			*chx = 1;
			*chy = -1;
			break;
		default:
			break;
	}
}

action game_window::input_to_action(const SDL_Event& ev)
{
	switch(ev.type) {
		case SDL_QUIT:
			return action(action_give_up);
		case SDL_KEYDOWN:
			{
				SDLKey k = ev.key.keysym.sym;
				if(k == SDLK_ESCAPE || k == SDLK_q)
					return action(action_give_up);
				else if((k == SDLK_RETURN || k == SDLK_KP_ENTER) && 
						(current_unit == myciv->units.end() || (ev.key.keysym.mod & (KMOD_LSHIFT | KMOD_RSHIFT)))) {
					return action(action_eot);
				}
				else if(current_unit != myciv->units.end()) {
					if(k == SDLK_b) {
						return unit_action(action_found_city, current_unit->second);
					}
					else if(k == SDLK_SPACE) {
						return unit_action(action_skip, current_unit->second);
					}
					else if(k == SDLK_i) {
						return improve_unit_action(current_unit->second, improv_irrigation);
					}
					else if(k == SDLK_m) {
						return improve_unit_action(current_unit->second, improv_mine);
					}
					else if(k == SDLK_r) {
						return improve_unit_action(current_unit->second, improv_road);
					}
					else if(k == SDLK_f) {
						return unit_action(action_fortify, current_unit->second);
					}
					else if(k == SDLK_l) {
						return unit_action(action_load, current_unit->second);
					}
					else if(k == SDLK_u) {
						return unit_action(action_unload, current_unit->second);
					}
					else {
						int chx, chy;
						numpad_to_move(k, &chx, &chy);
						if(chx || chy) {
							return move_unit_action(current_unit->second, chx, chy);
						}
					}
				}
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			handle_action_mouse_down(ev);
			break;
		case SDL_MOUSEBUTTONUP:
			return handle_action_mouse_up(ev);
		default:
			break;
	}
	return action_none;
}

action game_window::observer_action(const SDL_Event& ev)
{
	switch(ev.type) {
		case SDL_QUIT:
			return action(action_give_up);
		case SDL_KEYDOWN:
			{
				SDLKey k = ev.key.keysym.sym;
				if(k == SDLK_ESCAPE || k == SDLK_q) {
					return action(action_give_up);
				}
				else if(k == SDLK_RETURN || k == SDLK_KP_ENTER) {
					internal_ai->play();
				}
			}
		default:
			break;
	}
	return action_none;
}

void game_window::handle_successful_action(const action& a, city** c)
{
	switch(a.type) {
		case action_eot:
			// end of turn for this civ
			get_next_free_unit();
			break;
		case action_unit_action:
			switch(a.data.unit_data.uatype) {
				case action_move_unit:
					if(current_unit != myciv->units.end()) {
						if(current_unit->second->num_moves() == 0 && 
								current_unit->second->num_road_moves() == 0) {
							current_unit = myciv->units.end();
						}
					}
					break;
				case action_found_city:
					current_unit = myciv->units.end();
					if(c)
						*c = myciv->cities.rbegin()->second;
					// fall through
				case action_improvement:
				case action_skip:
				case action_fortify:
				case action_load:
				case action_unload:
					get_next_free_unit();
					break;
				default:
					break;
			}
		default:
			break;
	}
}

void game_window::update_view()
{
	blink_unit = true;
	draw();
}

int game_window::try_perform_action(const action& a, city** c)
{
	if(a.type != action_none) {
		// save the iterator - performing an action may destroy
		// the current unit
		bool already_begin = current_unit == myciv->units.begin();
		if(!already_begin) {
			current_unit--;
		}
		bool success = data.r.perform_action(myciv->civ_id, a);
		if(!already_begin) {
			current_unit++;
		}
		else {
			current_unit = myciv->units.begin();
		}
		if(success) {
			handle_successful_action(a, c);
			path_to_draw.clear();
			update_action_buttons();
		}
		else {
			printf("Unable to perform action.\n");
		}
		if(!internal_ai && current_unit == myciv->units.end()) {
			get_next_free_unit();
		}
		return success ? 2 : 1;
	}
	return 0;
}

void game_window::add_gui_msg(const std::string& s)
{
	gui_msg_queue.push_back(s);
	if(gui_msg_queue.size() >= 7)
		gui_msg_queue.pop_front();
}

int game_window::handle_civ_messages(std::list<msg>* messages)
{
	while(!messages->empty()) {
		msg& m = messages->front();
		switch(m.type) {
			case msg_new_unit:
				{
					unit_configuration_map::const_iterator it = data.r.uconfmap.find(m.msg_data.city_prod_data.prod_id);
					if(it != data.r.uconfmap.end()) {
						std::stringstream s;
						s << "New unit '" << it->second.unit_name << "' produced.";
						add_gui_msg(s.str());
					}
				}
				break;
			case msg_civ_discovery:
				{
					std::stringstream s;
					s << "Discovered the civilization of the " << data.r.civs[m.msg_data.discovered_civ_id]->civname << ".";
					add_gui_msg(s.str());
					add_subwindow(new diplomacy_window(screen, data, res, myciv,
								m.msg_data.discovered_civ_id));
				}
				break;
			case msg_new_advance:
				{
					unsigned int adv_id = m.msg_data.new_advance_id;
					advance_map::const_iterator it = data.r.amap.find(adv_id);
					if(it != data.r.amap.end()) {
						std::stringstream s;
						s << "Discovered advance '" << it->second.advance_name << "'.";
						add_gui_msg(s.str());
					}
					if(myciv->cities.size() > 0)
						add_subwindow(new discovery_window(screen,
									data, res, myciv,
									m.msg_data.new_advance_id));
					else
						myciv->research_goal_id = 0;
				}
				break;
			case msg_new_city_improv:
				{
					city_improv_map::const_iterator it = data.r.cimap.find(m.msg_data.city_prod_data.prod_id);
					if(it != data.r.cimap.end()) {
						std::stringstream s;
						s << "New improvement '" << it->second.improv_name << "' built.";
						add_gui_msg(s.str());
					}
					std::map<unsigned int, city*>::const_iterator c =
						myciv->cities.find(m.msg_data.city_prod_data.building_city_id);
					if(c != myciv->cities.end()) {
						if(try_center_camera_at(c->second->xpos, c->second->ypos)) {
							draw();
						}
						char buf[256];
						buf[255] = '\0';
						snprintf(buf, 256, "%s has built a %s.\n\n"
								"What should be our next production goal, sire?",
								c->second->cityname.c_str(),
								it == data.r.cimap.end() ? "<something>" :
								it->second.improv_name.c_str());
						add_subwindow(new production_window(screen,
									data, res, c->second,
									myciv,
									rect(screen->w * 0.6,
										screen->h * 0.15,
										screen->w * 0.35,
										screen->h * 0.7),
									color(50, 200, 255),
									std::string(buf), true));
					}
				}
				break;
			case msg_unit_disbanded:
				add_gui_msg("Unit disbanded.");
				break;
			case msg_new_relationship:
				{
					std::stringstream s;
					s << "The " << data.r.civs[m.msg_data.relationship_data.other_civ_id]->civname;
					switch(m.msg_data.relationship_data.new_relationship) {
						case relationship_war:
							s << " are in war with us!";
							break;
						case relationship_peace:
							s << " are in peace with us.";
							break;
						case relationship_unknown:
						default:
							s << " are forgotten.";
							break;
					}
					add_gui_msg(s.str());
				}
				break;
			default:
				fprintf(stderr, "Unknown message received: %d\n",
						m.type);
				break;
		}
		messages->pop_front();
	}
	return 0;
}

void game_window::check_unit_movement_orders()
{
	while(1) {
		if(current_unit == myciv->units.end()) {
			return;
		}
		std::map<unsigned int, std::list<coord> >::iterator path =
			unit_movement_orders.find(current_unit->second->unit_id);
		if(path == unit_movement_orders.end()) {
			return;
		}
		if(!current_unit->second->idle()) {
			return;
		}
		if(path->second.empty()) {
			unit_movement_orders.erase(path);
			return;
		}
		for(int i = -1; i <= 1; i++) {
			for(int j = -1; j <= 1; j++) {
				int resident = data.m.get_spot_resident(current_unit->second->xpos + i,
						current_unit->second->ypos + j);
				if(resident != -1 && resident != (int)myciv->civ_id) {
					// next to someone else
					unit_movement_orders.erase(path);
					return;
				}
			}
		}
		coord c = path->second.front();
		path->second.pop_front();
		int chx, chy;
		chx = data.m.vector_from_to_x(c.x, current_unit->second->xpos);
		chy = data.m.vector_from_to_y(c.y, current_unit->second->ypos);
		if(abs(chx) <= 1 && abs(chy) <= 1) {
			if(chx == 0 && chy == 0)
				continue;
			action a = move_unit_action(current_unit->second, chx, chy);
			if(try_perform_action(a, NULL) != 2) {
				// could not perform action
				unit_movement_orders.erase(path);
				return;
			}
		}
		else {
			// invalid path
			unit_movement_orders.erase(path);
			return;
		}
	}
}

action game_window::handle_action_mouse_up(const SDL_Event& ev)
{
	if(!path_to_draw.empty() && current_unit != myciv->units.end()) {
		unit_movement_orders[current_unit->second->unit_id] = path_to_draw;
	}
	path_to_draw.clear();
	if(current_unit != myciv->units.end()) {
		if(check_button_click(action_buttons, ev)) {
			action a = action_button_action;
			action_button_action = action_none;
			return a;
		}
	}
	return action_none;
}

void game_window::handle_action_mouse_down(const SDL_Event& ev)
{
	path_to_draw.clear();
}

int game_window::try_choose_with_mouse(city** c)
{
	// choose city
	city* cn = data.m.city_on_spot(mouse_down_sqx, mouse_down_sqy);
	if(cn && cn->civ_id == myciv->civ_id) {
		*c = cn;
		mouse_down_sqx = mouse_down_sqy = -1;
	}

	// if no city chosen, choose unit
	if(!*c && !internal_ai) {
		for(std::map<unsigned int, unit*>::iterator it = myciv->units.begin();
				it != myciv->units.end();
				++it) {
			unit* u = it->second;
			if(u->xpos == mouse_down_sqx && u->ypos == mouse_down_sqy) {
				u->wake_up();
				unit_movement_orders.erase(u->unit_id);
				if(u->num_moves() > 0 || u->num_road_moves() > 0) {
					current_unit = it;
					blink_unit = false;
					mouse_down_sqx = mouse_down_sqy = -1;
					update_action_buttons();
				}
			}
		}
	}
	return 0;
}

void game_window::init_turn()
{
	gui_msg_queue.clear();
	draw_window();
	if(internal_ai) {
		if(data.r.get_round_number() == 0 && myciv->units.begin() != myciv->units.end())
			try_center_camera_to_unit(myciv->units.begin()->second);
		return;
	}
	else {
		if(data.r.get_round_number() == 0) {
			current_unit = myciv->units.begin();
			try_center_camera_to_unit(current_unit->second);
			update_action_buttons();
		}
		else {
			// initial research goal
			if(myciv->research_goal_id == 0 &&
					myciv->cities.size() > 0 &&
					myciv->researched_advances.empty()) {
				add_subwindow(new discovery_window(screen,
							data, res, myciv,
							0));
			}
			current_unit = myciv->units.end();
			get_next_free_unit();
		}
	}
}

void game_window::handle_action(const visible_move_action& a)
{
	if(abs(a.change.x) > 1 || abs(a.change.y) > 1)
		return;
	int newx = data.m.wrap_x(a.u->xpos + a.change.x);
	int newy = data.m.wrap_y(a.u->ypos + a.change.y);
	if((myciv->fog_at(a.u->xpos, a.u->ypos) != 2 || myciv->fog_at(newx, newy) != 2) &&
			!internal_ai)
		return;
	if(!internal_ai && a.u->civ_id != (int)myciv->civ_id) {
		try_center_camera_to_unit(a.u);
		draw();
	}
	if(!tile_visible(a.u->xpos, a.u->ypos) || !tile_visible(newx, newy)) {
		return;
	}
	std::vector<coord> redrawable_tiles;
	redrawable_tiles.push_back(coord(a.u->xpos, a.u->ypos));
	redrawable_tiles.push_back(coord(newx, newy));
	if(a.u->xpos != newx || a.u->ypos != newy) {
		redrawable_tiles.push_back(coord(a.u->xpos, newy));
		redrawable_tiles.push_back(coord(newx, a.u->ypos));
	}
	SDL_Surface* surf = res.get_unit_tile(*a.u, data.r.civs[a.u->civ_id]->col);
	float xpos = tile_xcoord_to_pixel(a.u->xpos);
	float ypos = tile_ycoord_to_pixel(a.u->ypos);
	const int steps = 30;
	float xdiff = tile_w * a.change.x / steps;
	float ydiff = tile_h * a.change.y / steps;
	for(int i = 0; i <= steps; i++) {
		for(std::vector<coord>::const_iterator it = redrawable_tiles.begin();
				it != redrawable_tiles.end();
				++it) {
			if(tile_visible(it->x, it->y)) {
				int shx = data.m.wrap_x(it->x - cam.cam_x + sidebar_size);
				int shy = data.m.wrap_y(it->y - cam.cam_y);
				draw_complete_tile(it->x, it->y, shx, shy,
						true, true, true, true,
						boost::bind(&game_window::unit_not_at, 
							this,
							a.u->xpos, a.u->ypos, 
							boost::lambda::_1), 
						true);
			}
		}
		draw_image((int)xpos, (int)ypos, surf, screen);
		xpos += xdiff;
		ypos += ydiff;
		if(SDL_Flip(screen)) {
			fprintf(stderr, "Unable to flip: %s\n", SDL_GetError());
			return;
		}
	}
}

bool game_window::unit_not_at(int x, int y, const unit* u) const
{
	return u->xpos != x && u->ypos != y;
}


