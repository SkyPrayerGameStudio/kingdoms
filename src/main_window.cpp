#include <boost/bind.hpp>
#include <boost/lambda/lambda.hpp>
#include "main_window.h"
#include "map-astar.h"
#include "city_window.h"
#include "diplomacy_window.h"
#include "discovery_window.h"
#include "production_window.h"
#include "serialize.h"

main_window::main_window(SDL_Surface* screen_, int x, int y, gui_data& data_, gui_resources& res_,
		ai* ai_, civilization* myciv_)
	: window(screen_, x, y, data_, res_),
	tile_w(32),
	tile_h(32),
	cam_total_tiles_x((screen_w + tile_w - 1) / tile_w),
	cam_total_tiles_y((screen_h + tile_h - 1) / tile_h),
	sidebar_size(4),
	current_unit(myciv_->units.end()),
	blink_unit(false),
	timer(0),
	myciv(myciv_),
	mouse_down_sqx(-1),
	mouse_down_sqy(-1),
	internal_ai(ai_),
	sidebar_info_display(coord(-1, -1))
{
	cam.cam_x = cam.cam_y = 0;
}

main_window::~main_window()
{
}

void main_window::get_next_free_unit()
{
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
			return;
		}
	}
	current_unit = myciv->units.end();
}

int main_window::draw_window()
{
	if (SDL_MUSTLOCK(screen)) {
		if (SDL_LockSurface(screen) < 0) {
			return 1;
		}
	}
	draw_sidebar();
	clear_main_map();
	draw_main_map();
	if (SDL_MUSTLOCK(screen)) {
		SDL_UnlockSurface(screen);
	}
	if(SDL_Flip(screen)) {
		fprintf(stderr, "Unable to flip: %s\n", SDL_GetError());
		return 1;
	}
	return 0;
}

int main_window::clear_main_map() const
{
	SDL_Rect dest;
	dest.x = sidebar_size * tile_w;
	dest.y = 0;
	dest.w = screen_w - sidebar_size * tile_w;
	dest.h = screen_h;
	Uint32 color = SDL_MapRGB(screen->format, 0, 0, 0);
	SDL_FillRect(screen, &dest, color);
	return 0;
}

int main_window::clear_sidebar() const
{
	SDL_Rect dest;
	dest.x = 0;
	dest.y = 0;
	dest.w = sidebar_size * tile_w;
	dest.h = screen_h;
	Uint32 color = SDL_MapRGB(screen->format, 0, 0, 0);
	SDL_FillRect(screen, &dest, color);
	return 0;
}

int main_window::draw_sidebar() const
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

int main_window::draw_minimap() const
{
	const int minimap_w = sidebar_size * tile_w;
	const int minimap_h = sidebar_size * tile_h / 2;
	for(int i = 0; i < minimap_h; i++) {
		int y = i * data.m.size_y() / minimap_h;
		for(int j = 0; j < minimap_w; j++) {
			int x = j * data.m.size_x() / minimap_w;
			color c = get_minimap_color(x, y);
			if((c.r == 255 && c.g == 255 && c.b == 255) || 
					internal_ai || 
					myciv->fog_at(x, y) > 0) {
				sdl_put_pixel(screen, j, i, c);
			}
		}
	}
	SDL_UpdateRect(screen, 0, 0, minimap_w, minimap_h);

	return 0;
}

int main_window::draw_civ_info() const
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

std::string main_window::unit_strength_info_string(const unit* u) const
{
	if(u->uconf->max_strength == 0)
		return std::string("");
	char buf[256];
	buf[255] = '\0';
	snprintf(buf, 255, "%d.%d/%d.0", u->strength / 10, u->strength % 10,
			u->uconf->max_strength);
	return std::string(buf);
}

int main_window::draw_unit_info() const
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

bool main_window::write_unit_info(const unit* u, int* written_lines) const
{
	if(*written_lines >= 6) {
		draw_text(screen, &res.font, "<More>", 10,
				screen_h - 160 + *written_lines * 16, 255, 255, 255);
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
			screen_h - 160 + *written_lines * 16, 255, 255, 255);
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

void main_window::display_tile_info() const
{
	if(sidebar_info_display.x < 0)
		return;
	char fog = myciv->fog_at(sidebar_info_display.x, sidebar_info_display.y);
	if(fog == 0)
		return;
	int terr = data.m.get_data(sidebar_info_display.x, sidebar_info_display.y);
	if(terr >= 0 && terr < num_terrain_types) {
		draw_text(screen, &res.font, data.m.resconf.resource_name[terr].c_str(), 10, screen_h - 160, 255, 255, 255);
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
							screen_h - 160 + (written_lines + 1) * 16, 255, 255, 255);
					draw_text(screen, &res.font, buf, 10,
							screen_h - 160 + (written_lines + 2) * 16, 255, 255, 255);
				}
			}
		}
	}
}

int main_window::draw_eot() const
{
	return draw_text(screen, &res.font, "End of turn", 10, screen_h - 176, 255, 255, 255);
}

int main_window::draw_tile(const SDL_Surface* surf, int x, int y) const
{
	if(tile_visible(x, y)) {
		return draw_image(tile_xcoord_to_pixel(x),
				tile_ycoord_to_pixel(y),
				surf, screen);
	}
	return 0;
}

int main_window::draw_city(const city& c) const
{
	if(!tile_visible(c.xpos, c.ypos))
		return 0;

	if(draw_tile(res.city_images[c.civ_id], c.xpos, c.ypos))
		return 1;

	// don't draw the text on cities on gui border
	if(!tile_visible(c.xpos + 1, c.ypos) ||
	   !tile_visible(c.xpos - 1, c.ypos) ||
	   !tile_visible(c.xpos, c.ypos + 1))
		return 0;

	char buf[64];
	if(internal_ai || c.civ_id == myciv->civ_id) {
		unsigned int num_turns = data.r.get_city_growth_turns(&c);
		if(num_turns == 0)
			snprintf(buf, 63, "%d %s (-)", c.get_city_size(),
					c.cityname.c_str());
		else
			snprintf(buf, 63, "%d %s (%d)", c.get_city_size(),
					c.cityname.c_str(), num_turns);
	}
	else {
		snprintf(buf, 63, "%d %s", c.get_city_size(),
				c.cityname.c_str());
	}
	buf[63] = '\0';
	if(draw_text(screen, &res.font, buf, tile_xcoord_to_pixel(c.xpos) + tile_h / 2,
			tile_ycoord_to_pixel(c.ypos) + tile_w,
			255, 255, 255, true))
		return 1;

	if(internal_ai || c.civ_id == myciv->civ_id) {
		const std::string* producing = NULL;
		unsigned int num_turns_prod = 0;
		if(c.production.current_production_id >= 0) {
			if(c.production.producing_unit) {
				unit_configuration_map::const_iterator it = 
					data.r.uconfmap.find(c.production.current_production_id);
				if(it != data.r.uconfmap.end()) {
					producing = &it->second.unit_name;
					num_turns_prod = data.r.get_city_production_turns(&c,
							it->second);
				}
			}
			else {
				city_improv_map::const_iterator it = 
					data.r.cimap.find(c.production.current_production_id);
				if(it != data.r.cimap.end()) {
					producing = &it->second.improv_name;
					num_turns_prod = data.r.get_city_production_turns(&c,
							it->second);
				}
			}
		}
		if(producing) {
			if(num_turns_prod) {
				snprintf(buf, 63, "%s (%d)", producing->c_str(), 
						num_turns_prod);
			}
			else {
				snprintf(buf, 63, "%s (-)", producing->c_str());
			}
		}
		else {
			snprintf(buf, 63, "(-)");
		}
		if(draw_text(screen, &res.font, buf, tile_xcoord_to_pixel(c.xpos) + tile_h / 2,
					tile_ycoord_to_pixel(c.ypos) + 1.5 * tile_w,
					255, 255, 255, true)) {
			fprintf(stderr, "Could not draw production text.\n");
			return 1;
		}
	}
	return 0;
}

int main_window::tile_ycoord_to_pixel(int y) const
{
	return data.m.wrap_y(y - cam.cam_y) * tile_h;
}

int main_window::tile_xcoord_to_pixel(int x) const
{
	return data.m.wrap_x(x + sidebar_size - cam.cam_x) * tile_w;
}

int main_window::tile_visible(int x, int y) const
{
	bool vis_no_wrap = (in_bounds(cam.cam_x, x, cam.cam_x + cam_total_tiles_x) &&
			in_bounds(cam.cam_y, y, cam.cam_y + cam_total_tiles_y));
	if(vis_no_wrap)
		return vis_no_wrap;
	else // handle X wrapping
		return (in_bounds(cam.cam_x, x + data.m.size_x(), cam.cam_x + cam_total_tiles_x) &&
				in_bounds(cam.cam_y, y, cam.cam_y + cam_total_tiles_y));
}

int main_window::draw_line_by_sq(const coord& c1, const coord& c2, int r, int g, int b)
{
	coord start(tile_xcoord_to_pixel(c1.x) + tile_w / 2,
			tile_ycoord_to_pixel(c1.y) + tile_h / 2);
	coord end(tile_xcoord_to_pixel(c2.x) + tile_w / 2,
			tile_ycoord_to_pixel(c2.y) + tile_h / 2);
	draw_line(screen, start.x, start.y, end.x, end.y, color(r, g, b));
	return 0;
}

int main_window::draw_unit(const unit* u)
{
	if(!tile_visible(u->xpos, u->ypos)) {
		return 0;
	}
	if(u->carried() && (current_unit == myciv->units.end() || current_unit->second != u)) {
		return 0;
	}
	SDL_Surface* surf = res.get_unit_tile(*u, data.r.civs[u->civ_id]->col);
	return draw_tile(surf, u->xpos, u->ypos);
}

int main_window::draw_complete_tile(int x, int y, int shx, int shy, bool terrain,
		bool improvements, bool borders,
		boost::function<bool(const unit*)> unit_predicate,
		bool cities)
{
	char fog = myciv->fog_at(x, y);
	if(fog == 0 && !internal_ai)
		return 0;
	if(terrain) {
		if(show_terrain_image(x, y, shx, shy, improvements, !internal_ai && fog == 1))
			return 1;
	}
	if(borders) {
		if(test_draw_border(x, y, shx, shy))
			return 1;
	}
	const std::list<unit*>& units = data.m.units_on_spot(x, y);
	for(std::list<unit*>::const_iterator it = units.begin();
			it != units.end(); ++it) {
		if((internal_ai || fog == 2) && unit_predicate(*it)) {
			if(draw_unit(*it))
				return 1;
		}
	}
	if(cities) {
		city* c = data.m.city_on_spot(x, y);
		if(c) {
			if(draw_city(*c))
				return 1;
		}
	}
	return 0;
}

bool main_window::draw_gui_unit(const unit* u) const
{
	if(current_unit == myciv->units.end())
	       return true;
	else
		return u != current_unit->second &&
			(u->xpos != current_unit->second->xpos ||
			 u->ypos != current_unit->second->ypos);
}

int main_window::draw_main_map()
{
	int imax = cam.cam_y + cam_total_tiles_y;
	int jmax = cam.cam_x + cam_total_tiles_x;
	// bottom-up in y dimension for correct rendering of city names
	for(int i = imax - 1, y = cam_total_tiles_y - 1; i >= cam.cam_y; i--, y--) {
		for(int j = cam.cam_x, x = sidebar_size; j < jmax; j++, x++) {
			if(draw_complete_tile(data.m.wrap_x(j),
						data.m.wrap_y(i),
						x, y,
						true, true, true,
						boost::bind(&main_window::draw_gui_unit,
							this, boost::lambda::_1),
						true))
				return 1;
		}
	}

	if(!internal_ai && current_unit != myciv->units.end() && !blink_unit) {
		if(draw_unit(current_unit->second))
			return 1;
	}
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
	return 0;
}

int main_window::show_terrain_image(int x, int y, int xpos, int ypos,
		bool draw_improvements, bool shade)
{
	return draw_terrain_tile(x, y, xpos * tile_w, ypos * tile_h, shade,
			data.m, res.terrains, draw_improvements, screen);
}

int main_window::test_draw_border(int x, int y, int xpos, int ypos)
{
	int this_owner = data.m.get_land_owner(x, y);
	int w_owner = data.m.get_land_owner(x - 1, y);
	int e_owner = data.m.get_land_owner(x + 1, y);
	int n_owner = data.m.get_land_owner(x,     y - 1);
	int s_owner = data.m.get_land_owner(x,     y + 1);
	for(int i = 0; i < 4; i++) {
		int oth, sx, sy, ex, ey;
		switch(i) {
			case 0:
				oth = w_owner;
				sx = 0; sy = 0; ex = 0; ey = 1;
				break;
			case 1:
				oth = e_owner;
				sx = 1; sy = 0; ex = 1; ey = 1;
				break;
			case 2:
				oth = n_owner;
				sx = 0; sy = 0; ex = 1; ey = 0;
				break;
			default:
				oth = s_owner;
				sx = 0; sy = 1; ex = 1; ey = 1;
				break;
		}
		if(this_owner != oth) {
			const color& col = this_owner == -1 ? data.r.civs[oth]->col :
				data.r.civs[this_owner]->col;
			draw_line(screen, (xpos + sx) * tile_w, (ypos + sy) * tile_h, 
					(xpos + ex) * tile_w, (ypos + ey) * tile_h,
					col);
		}
	}
	return 0;
}

color main_window::get_minimap_color(int x, int y) const
{
	// minimap rectangle - no wrapping
	if((x >= cam.cam_x && x <= cam.cam_x + cam_total_tiles_x - 1) &&
	   (y >= cam.cam_y && y <= cam.cam_y + cam_total_tiles_y - 1) &&
	   (x == cam.cam_x || x == cam.cam_x + cam_total_tiles_x - 1 ||
	    y == cam.cam_y || y == cam.cam_y + cam_total_tiles_y - 1))
		return color(255, 255, 255);

	// minimap rectangle - wrapped in X
	if((cam.cam_x > data.m.wrap_x(cam.cam_x + cam_total_tiles_x - 1) && 
	    data.m.wrap_x(x) <= data.m.wrap_x(cam.cam_x + cam_total_tiles_x - 1)) &&
	   (y >= cam.cam_y && y <= cam.cam_y + cam_total_tiles_y - 1) &&
	   (x == cam.cam_x || data.m.wrap_x(x) == data.m.wrap_x(cam.cam_x + cam_total_tiles_x - 1) ||
	    y == cam.cam_y || y == cam.cam_y + cam_total_tiles_y - 1))
		return color(255, 255, 255);

	int val = data.m.get_data(x, y);
	if(val < 0 || val >= (int)res.terrains.textures.size()) {
		fprintf(stderr, "Terrain at %d not loaded at (%d, %d)\n", val,
				x, y);
		return color(0, 0, 0);
	}
	return sdl_get_pixel(res.terrains.textures[val], 16, 16);
}

int main_window::try_move_camera(bool left, bool right, bool up, bool down)
{
	bool redraw = false;
	if(left) {
		if(cam.cam_x > 0 || data.m.x_wrapped()) {
			cam.cam_x = data.m.wrap_x(cam.cam_x - 1);
			redraw = true;
		}
	}
	else if(right) {
		if(cam.cam_x < data.m.size_x() - cam_total_tiles_x || data.m.x_wrapped()) {
			cam.cam_x = data.m.wrap_x(cam.cam_x + 1);
			redraw = true;
		}
	}
	if(up) {
		if(cam.cam_y > 0 || data.m.y_wrapped()) {
			cam.cam_y = data.m.wrap_y(cam.cam_y - 1);
			redraw = true;
		}
	}
	else if(down) {
		if(cam.cam_y < data.m.size_y() - cam_total_tiles_y || data.m.y_wrapped()) {
			cam.cam_y = data.m.wrap_y(cam.cam_y + 1);
			redraw = true;
		}
	}
	if(redraw) {
		draw();
	}
	return redraw;
}

void main_window::center_camera_to_unit(const unit* u)
{
	cam.cam_x = clamp(0, data.m.wrap_x(u->xpos - (-sidebar_size + cam_total_tiles_x) / 2), 
			data.m.size_x() - (data.m.x_wrapped() ? 0 : cam_total_tiles_x));
	cam.cam_y = clamp(0, data.m.wrap_y(u->ypos - cam_total_tiles_y / 2), 
			data.m.size_y() - (data.m.y_wrapped() ? 0 : cam_total_tiles_y));
}

int main_window::try_center_camera_to_unit(const unit* u)
{
	const int border = 3;
	if(!in_bounds(cam.cam_x + border, u->xpos, cam.cam_x - sidebar_size + cam_total_tiles_x - border) ||
	   !in_bounds(cam.cam_y + border, u->ypos, cam.cam_y + cam_total_tiles_y - border)) {
		center_camera_to_unit(u);
		return true;
	}
	return false;
}

int main_window::process(int ms)
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
	handle_civ_messages(&myciv->messages);
	return 0;
}

int main_window::handle_mousemotion(int x, int y)
{
	const int border = tile_w;
	try_move_camera(x >= sidebar_size * tile_w && x < sidebar_size * tile_w + border,
			x > screen_w - border,
			y < border,
			y > screen_h - border);
	check_line_drawing(x, y);
	update_tile_info(x, y);
	return 0;
}

void main_window::update_tile_info(int x, int y)
{
	int sqx, sqy;
	mouse_coord_to_tiles(x, y, &sqx, &sqy);
	if(sqx >= 0) {
		sidebar_info_display = coord(sqx, sqy);
		draw_sidebar();
	}
}

int main_window::check_line_drawing(int x, int y)
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

void main_window::numpad_to_move(SDLKey k, int* chx, int* chy) const
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

action main_window::input_to_action(const SDL_Event& ev)
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
		case SDL_MOUSEBUTTONUP:
			handle_action_mouse_up(ev);
		default:
			break;
	}
	return action_none;
}

action main_window::observer_action(const SDL_Event& ev)
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
				else if(k == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL)) {
					printf("Saving.\n");
					save_game("manual", data.r);
					return action_none;
				}
			}
		default:
			break;
	}
	return action_none;
}

void main_window::handle_successful_action(const action& a, city** c)
{
	switch(a.type) {
		case action_eot:
			// end of turn for this civ
			if(data.r.get_round_number() % 4 == 0) {
				printf("Auto-saving.\n");
				save_game("auto", data.r);
			}
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

void main_window::handle_input_gui_mod(const SDL_Event& ev, city** c)
{
	switch(ev.type) {
		case SDL_KEYDOWN:
			{
				SDLKey k = ev.key.keysym.sym;
				if(k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN) {
					try_move_camera(k == SDLK_LEFT, k == SDLK_RIGHT, k == SDLK_UP, k == SDLK_DOWN);
				}
				if(k == SDLK_s && (ev.key.keysym.mod & KMOD_CTRL)) {
					printf("Saving.\n");
					save_game("manual", data.r);
				}
				if(!internal_ai && current_unit != myciv->units.end()) {
					if(k == SDLK_c) {
						center_camera_to_unit(current_unit->second);
					}
					else if(k == SDLK_w) {
						std::map<unsigned int, unit*>::const_iterator old_it = current_unit;
						get_next_free_unit();
						if(current_unit == myciv->units.end())
							current_unit = old_it;
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
		case SDL_MOUSEBUTTONUP:
			handle_mouse_up(ev);
			break;
		default:
			break;
	}
}

void main_window::update_view()
{
	blink_unit = true;
	draw();
}

int main_window::try_perform_action(const action& a, city** c)
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

int main_window::handle_window_input(const SDL_Event& ev)
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
		add_subwindow(new city_window(screen, screen_w, screen_h, data, res, c,
					internal_ai, myciv));
	}
	return a.type == action_give_up;
}

int main_window::handle_civ_messages(std::list<msg>* messages)
{
	while(!messages->empty()) {
		msg& m = messages->front();
		switch(m.type) {
			case msg_new_unit:
				{
					unit_configuration_map::const_iterator it = data.r.uconfmap.find(m.msg_data.city_prod_data.prod_id);
					if(it != data.r.uconfmap.end()) {
						printf("New unit '%s' produced.\n",
								it->second.unit_name.c_str());
					}
				}
				break;
			case msg_civ_discovery:
				printf("Discovered civilization '%s'.\n",
						data.r.civs[m.msg_data.discovered_civ_id]->civname.c_str());
				add_subwindow(new diplomacy_window(screen, screen_w, screen_h, data, res, myciv,
							m.msg_data.discovered_civ_id));
				break;
			case msg_new_advance:
				{
					unsigned int adv_id = m.msg_data.new_advance_id;
					advance_map::const_iterator it = data.r.amap.find(adv_id);
					if(it != data.r.amap.end()) {
						printf("Discovered advance '%s'.\n",
								it->second.advance_name.c_str());
					}
					if(myciv->cities.size() > 0)
						add_subwindow(new discovery_window(screen, screen_w, screen_h,
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
						printf("New improvement '%s' built.\n",
								it->second.improv_name.c_str());
					}
					std::map<unsigned int, city*>::const_iterator c =
						myciv->cities.find(m.msg_data.city_prod_data.building_city_id);
					if(c != myciv->cities.end()) {
						char buf[256];
						buf[255] = '\0';
						snprintf(buf, 256, "%s has built a %s.\n\n"
								"What should be our next production goal, sire?",
								c->second->cityname.c_str(),
								it == data.r.cimap.end() ? "<something>" :
								it->second.improv_name.c_str());
						add_subwindow(new production_window(screen,
									screen_w, screen_h,
									data, res, c->second,
									myciv,
									rect(screen_w * 0.6,
										screen_h * 0.15,
										screen_w * 0.35,
										screen_h * 0.7),
									color(50, 200, 255),
									std::string(buf), true));
					}
				}
				break;
			case msg_unit_disbanded:
				printf("Unit disbanded.\n");
				break;
			default:
				printf("Unknown message received: %d\n",
						m.type);
				break;
		}
		messages->pop_front();
	}
	return 0;
}

void main_window::mouse_coord_to_tiles(int mx, int my, int* sqx, int* sqy)
{
	*sqx = (mx - sidebar_size * tile_w) / tile_w;
	*sqy = my / tile_h;
	if(*sqx >= 0) {
		*sqx = data.m.wrap_x(*sqx + cam.cam_x);
		*sqy = data.m.wrap_y(*sqy + cam.cam_y);
	}
	else {
		*sqx = -1;
		*sqy = -1;
	}
}

void main_window::check_unit_movement_orders()
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

void main_window::handle_action_mouse_up(const SDL_Event& ev)
{
	if(!path_to_draw.empty() && current_unit != myciv->units.end()) {
		unit_movement_orders[current_unit->second->unit_id] = path_to_draw;
	}
	path_to_draw.clear();
}

void main_window::handle_action_mouse_down(const SDL_Event& ev)
{
	path_to_draw.clear();
}

int main_window::handle_mouse_up(const SDL_Event& ev)
{
	mouse_down_sqx = mouse_down_sqy = -1;
	return 0;
}

int main_window::handle_mouse_down(const SDL_Event& ev, city** c)
{
	mouse_coord_to_tiles(ev.button.x, ev.button.y, &mouse_down_sqx, &mouse_down_sqy);
	if(mouse_down_sqx >= 0)
		try_choose_with_mouse(c);
	return 0;
}

int main_window::try_choose_with_mouse(city** c)
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
				}
			}
		}
	}
	return 0;
}

void main_window::init_turn()
{
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
		}
		else {
			// initial research goal
			if(myciv->research_goal_id == 0 &&
					myciv->cities.size() > 0 &&
					myciv->researched_advances.empty()) {
				add_subwindow(new discovery_window(screen, screen_w, screen_h,
							data, res, myciv,
							0));
			}
			current_unit = myciv->units.end();
			get_next_free_unit();
		}
	}
}

bool main_window::unit_not_at(int x, int y, const unit* u) const
{
	return u->xpos != x && u->ypos != y;
}

void main_window::handle_action(const visible_move_action& a)
{
	if(abs(a.change.x) > 1 || abs(a.change.y) > 1)
		return;
	int newx = data.m.wrap_x(a.u->xpos + a.change.x);
	int newy = data.m.wrap_y(a.u->ypos + a.change.y);
	if((myciv->fog_at(a.u->xpos, a.u->ypos) != 2 || myciv->fog_at(newx, newy) != 2) &&
			!internal_ai)
		return;
	if(a.u->civ_id != (int)myciv->civ_id) {
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
						true, true, true,
						boost::bind(&main_window::unit_not_at, 
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

