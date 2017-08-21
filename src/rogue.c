#include <stdio.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BOARD_W 80
#define BOARD_H 24
#define MAX_ROOMS 100
#define MAX_MOBS 100
#define MIN_ROOM_SIZE 7

void update_visible_board();
void print_board();
void gen_board();
void split_room(int);
int max(int, int);
int min(int, int);
int random_int(int);
bool tile_is_visible_wall(int);
bool combat_paused;
enum mob_type {
	MOB_PLAYER = 0,
	MOB_DEAD,
	MOB_ORK
	
};
enum room_type {
	ROOM_NONE = 0,
	ROOM_FULL,
	ROOM_TUNNEL,
	ROOM_REMOVED
};
enum tile_type {
	TILE_EMPTY = 0,
	TILE_FLOOR,
	TILE_WALL,
	TILE_DOOR,
	TILE_HIDDEN_DOOR,
	TILE_TUNNEL,
	TILE_EXIT,
	TILE_MASK = 63,
	TILE_DISCOVERED = 64,
	TILE_VISIBLE = 128
};
enum tile_color {
	COLOR_DEFAULT = 1,
	TILE_COLOR_FLOOR,
	TILE_COLOR_WALL,
	TILE_COLOR_DOOR,
	TILE_COLOR_VISIBLE,
	TILE_COLOR_INVISIBLE,
	TILE_COLOR_INVISIBLE_FLOOR,
	TILE_COLOR_AVATAR,
	TILE_COLOR_ENEMY
};
enum face {
	FACE_NORTH = 1,
	FACE_EAST = 2,
	FACE_SOUTH = 4,
	FACE_WEST = 8
};
struct room {
	int ax, ay, bx, by;
	int type;
	int cost, from, essential, connected;
};
struct room rooms[MAX_ROOMS];
char touching_rooms[MAX_ROOMS * MAX_ROOMS];
struct mob {
	int x, y, dx, dy, type, turn;
	int level, health, max_health, strength, defense, accuracy, evasion, speed, movement;
};
struct mob mobs[MAX_MOBS];
void roll_mob(struct mob *);
void mob_interact(struct mob *, int, int);
void mob_fight(struct mob *, struct mob *);
void mob_ai(struct mob *);
char *log_message;
char log_buffer[1024];
int mobc;
int roomc;
int tiles[BOARD_W * BOARD_H];
char *mob_name(enum mob_type);

int main(int argc, char **argv) {
	srandom(time(0));
	gen_board();
	WINDOW *win = initscr();
	start_color();
	if (can_change_color()) init_color(COLOR_BLACK, 0, 0, 0);
	init_pair(COLOR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
	init_pair(TILE_COLOR_FLOOR, COLOR_CYAN, COLOR_BLACK);
	init_pair(TILE_COLOR_WALL, COLOR_CYAN, COLOR_BLACK);
	init_pair(TILE_COLOR_DOOR, COLOR_WHITE, COLOR_BLACK);
	init_pair(TILE_COLOR_VISIBLE, COLOR_WHITE, COLOR_BLACK);
	init_pair(TILE_COLOR_INVISIBLE, 8, COLOR_BLACK);
	init_pair(TILE_COLOR_INVISIBLE_FLOOR, 8, COLOR_BLACK);
	init_pair(TILE_COLOR_AVATAR, COLOR_GREEN, COLOR_BLACK);
	init_pair(TILE_COLOR_ENEMY, COLOR_RED, COLOR_BLACK);
	raw();
	noecho();
	while (true) {
		if (!combat_paused && mobs[0].health <= 0) {
			log_message = "You have died. Hit 'q' to quit.";
			combat_paused = true;
		}
		if (!combat_paused) {
			for (int i = 0; i < mobc; i += 1) {
				if (mobs[i].type != MOB_DEAD) {
					mobs[i].movement += mobs[i].speed;
				}
			}
		}
		if (combat_paused) {
			print_board();
			refresh();
			int ch = getch();
			if (ch == 'q') {
				break;
			}
			combat_paused = false;
		} else if (mobs[0].movement >= 100) {
			mobs[0].turn += 1;
			mobs[0].movement -= 100;
			update_visible_board();
			print_board();
			refresh();
			int ch = getch();
			if (ch == 'q') break;
			switch (ch) {
			case 'h':
				mobs[0].dx = -1;
				break;
			case 'j':
				mobs[0].dy = 1;
				break;
			case 'k':
				mobs[0].dy = -1;
				break;
			case 'l':
				mobs[0].dx = 1;
				break;
			case 'r':
				gen_board();
				break;
			case 'v':
				for (int i = 0; i < BOARD_W * BOARD_H; i += 1) tiles[i] |= TILE_DISCOVERED;
				break;
			}
		}
		if (!combat_paused) {
			for (int i = 1; i < mobc; i += 1) {
				if (mobs[i].type == MOB_DEAD) continue;
				if (mobs[i].movement >= 100) {
					mobs[i].movement -= 100;
					mobs[i].turn += 1;
					mob_ai(&mobs[i]);
				}
			}
		}
		for (int i = 0; i < mobc; i += 1) {
			if (mobs[i].type == MOB_DEAD) continue;
			int dx = mobs[i].dx;
			int dy = mobs[i].dy;
			if (dx == 0 && dy == 0) continue;
			mobs[i].dx = 0;
			mobs[i].dy = 0;
			mob_interact(&mobs[i], dx, dy);
			if (combat_paused) break;
		}
	}
	delwin(win);
	endwin();
	refresh();
	return 0;
}

char *mob_name(enum mob_type mt) {
	switch (mt) {
	case MOB_PLAYER: return "player";
	case MOB_DEAD: return "corpse";
	case MOB_ORK: return "ork";
	default: return "n/a";
	}
}

void gen_board() {
	memset(mobs, 0, sizeof(struct mob) * MAX_MOBS);
	memset(rooms, 0, sizeof(struct room) * MAX_ROOMS);
	memset(touching_rooms, 0, MAX_ROOMS * MAX_ROOMS);
	for (int y = 0; y < BOARD_H; y += 1) {
		for (int x = 0; x < BOARD_W; x += 1) {
			tiles[x + y * BOARD_W] = TILE_EMPTY;
		}
	}
	// Rooms
	int cx = BOARD_W / 2;
	rooms[0].ax = 0;
	rooms[0].ay = 0;
	rooms[0].bx = cx;
	rooms[0].by = BOARD_H - 1;
	roomc = 1;
	split_room(0);
	// Touching rooms
	for (int i = 0; i < roomc; i += 1) {
		for (int j = 0; j < roomc; j += 1) {
			if (i == j) continue;
			if (rooms[i].ay < rooms[j].by - 1 &&
			    rooms[i].by > rooms[j].ay + 1) {
				if (rooms[i].ax == rooms[j].bx) {
					touching_rooms[j + i * MAX_ROOMS] = FACE_WEST;
				} else if (rooms[i].bx == rooms[j].ax) {
					touching_rooms[j + i * MAX_ROOMS] = FACE_EAST;
				} else {
					touching_rooms[j + i * MAX_ROOMS] = 0;
				}
			}
			if (rooms[i].ax < rooms[j].bx - 1 &&
			    rooms[i].bx > rooms[j].ax + 1) {
				if (rooms[i].ay == rooms[j].by) {
					touching_rooms[j + i * MAX_ROOMS] = FACE_NORTH;
				} else if (rooms[i].by == rooms[j].ay) {
					touching_rooms[j + i * MAX_ROOMS] = FACE_SOUTH;
				} else {
					touching_rooms[j + i * MAX_ROOMS] = 0;
				}
			}
		}
	}
	int roomis[roomc];
	for (int i = 0; i < roomc; i += 1) roomis[i] = i;
	for (int i = 0; i < roomc; i += 1) {
		int j = random() % roomc;
		int tmp = roomis[i];
		roomis[i] = roomis[j];
		roomis[j] = tmp;
	}
	int start_room_i = roomis[0];
	rooms[start_room_i].type = ROOM_FULL;
	rooms[start_room_i].essential = 1;
	rooms[start_room_i].cost = 1;
	rooms[start_room_i].connected = 1;
	int finish_room_i = roomc - 1;
	for (int cost = 1; cost > 0; cost += 1) {
		bool found_anew = false;
		for (int i1 = 0; i1 < roomc && cost > 0; i1 += 1) {
			int i = roomis[i1];
			if (rooms[i].cost != cost) continue;
			for (int j1 = 0; j1 < roomc; j1 += 1) {
				int j = roomis[j1];
				if (i == j) continue;
				if (rooms[j].cost != 0) continue;
				if (touching_rooms[j + i * MAX_ROOMS] == 0) continue;
				rooms[j].cost = cost + 1;
				rooms[j].from = i;
				finish_room_i = j;
				found_anew = true;
			}
		}
		if (!found_anew) break;
	}
	rooms[finish_room_i].type = ROOM_FULL;
	rooms[finish_room_i].essential = 1;
	for (int i = finish_room_i; rooms[i].cost > 1 ; i = rooms[i].from) {
		rooms[i].essential = 1;
	}
	for (int i = 0; i < roomc; i += 1) {
		if (rooms[i].type != ROOM_NONE) continue;
		if (rooms[i].essential) {
			switch (random() % 3) {
			case 0:
				rooms[i].type = ROOM_TUNNEL;
				break;
			default:
				rooms[i].type = ROOM_FULL;
			}
		} else {
			switch (random() % 4) {
			case 0:
				rooms[i].type = ROOM_TUNNEL;
				break;
			case 1:
				rooms[i].type = ROOM_REMOVED;
				break;
			default:
				rooms[i].type = ROOM_FULL;
			}
		}
	}
	for (int i = 0; i < roomc; i += 1) {
		if (rooms[i].type == ROOM_FULL) {
			int ax = rooms[i].ax;
			int ay = rooms[i].ay;
			int bx = rooms[i].bx;
			int by = rooms[i].by;
			for (int y = ay; y <= by; y += 1) {
				tiles[ax + y * BOARD_W] = TILE_WALL;
				tiles[bx + y * BOARD_W] = TILE_WALL;
			}
			for (int x = ax; x <= bx; x += 1) {
				tiles[x + ay * BOARD_W] = TILE_WALL;
				tiles[x + by * BOARD_W] = TILE_WALL;
			}
			for (int y = ay + 1; y < by; y += 1) {
				for (int x = ax + 1; x < bx; x += 1) {
					tiles[x + y * BOARD_W] = TILE_FLOOR;
				}
			}
		}
	}
	// Doors
	for (int i1 = 0; i1 < roomc; i1 += 1) {
		int i = roomis[i1];
		int t = rooms[i].type;
		if (t == ROOM_NONE) continue;
		int ax = rooms[i].ax;
		int ay = rooms[i].ay;
		int bx = rooms[i].bx;
		int by = rooms[i].by;
		for (int j1 = 0; j1 < roomc; j1 += 1) {
			int j = roomis[j1];
			if (i == j) continue;
			if (!rooms[j].essential &&
			    rooms[j].connected) continue;
			char dir = (touching_rooms[j + i * MAX_ROOMS]);
			if (!dir) continue;
			if (dir == FACE_WEST) {
				// Connect to left room
				int cy = max(ay, rooms[j].ay) + 1;
				int dy = min(by, rooms[j].by) - 1;
				if (t == ROOM_FULL && rooms[j].type == ROOM_FULL) {
					int y = cy + random_int(dy - cy + 1);
					tiles[ax + y * BOARD_W] = TILE_DOOR;
					rooms[j].connected = true;
				} else if (t == ROOM_TUNNEL) {
					int y = (cy + dy) / 2;
					int mx = (ax + bx) / 2;
					if (rooms[j].type == ROOM_FULL) {
						tiles[ax + y * BOARD_W] = TILE_DOOR;
					} else {
						tiles[ax + y * BOARD_W] = TILE_TUNNEL;
					}
					for (int x = ax + 1; x <= mx; x += 1) {
						tiles[x + y * BOARD_W] = TILE_TUNNEL;
					}
					// Connect to center
					int my = (ay + by) / 2;
					for (int ey = min(y, my); ey <= max(y, my); ey += 1) {
						tiles[mx + ey * BOARD_W] = TILE_TUNNEL;
					}
					rooms[j].connected = true;
				}
			} else if (dir == FACE_EAST) {
				// Connect to right room
				if (t == ROOM_TUNNEL) {
					int cy = max(ay, rooms[j].ay) + 1;
					int dy = min(by, rooms[j].by) - 1;
					int y = (cy + dy) / 2;
					int mx = (ax + bx) / 2;
					if (rooms[j].type == ROOM_FULL) {
						tiles[bx + y * BOARD_W] = TILE_DOOR;
					} else {
						tiles[bx + y * BOARD_W] = TILE_TUNNEL;
					}
					for (int x = mx; x < bx; x += 1) {
						tiles[x + y * BOARD_W] = TILE_TUNNEL;
					}
					// Connect to center
					int my = (ay + by) / 2;
					for (int ey = min(y, my); ey <= max(y, my); ey += 1) {
						tiles[mx + ey * BOARD_W] = TILE_TUNNEL;
					}
					rooms[j].connected = true;
				}
			} else if (dir == FACE_NORTH) {
				// Connect to top room
				int cx = max(ax, rooms[j].ax) + 1;
				int dx = min(bx, rooms[j].bx) - 1;
				if (t == ROOM_FULL && rooms[j].type == ROOM_FULL) {
					int x = cx + random_int(dx - cx + 1);
					tiles[x + ay * BOARD_W] = TILE_DOOR;
					rooms[j].connected = true;
				} else if (t == ROOM_TUNNEL) {
					int x = (cx + dx) / 2;
					if (rooms[j].type == ROOM_FULL) {
						tiles[x + ay * BOARD_W] = TILE_DOOR;
					} else {
						tiles[x + ay * BOARD_W] = TILE_TUNNEL;
					}
					int my = (ay + by) / 2;
					for (int y = ay + 1; y <= my; y += 1) {
						tiles[x + y * BOARD_W] = TILE_TUNNEL;
					}
					// Connect to center
					int mx = (ax + bx) / 2;
					for (int ex = min(x, mx); ex <= max(x, mx); ex += 1) {
						tiles[ex + my * BOARD_W] = TILE_TUNNEL;
					}
					rooms[j].connected = true;
				}
			} else if (dir == FACE_SOUTH) {
				// Connect to bottom room
				int cx = max(ax, rooms[j].ax) + 1;
				int dx = min(bx, rooms[j].bx) - 1;
				if (t == ROOM_TUNNEL) {
					int x = (cx + dx) / 2;
					if (rooms[j].type == ROOM_FULL) {
						tiles[x + by * BOARD_W] = TILE_DOOR;
					} else {
						tiles[x + by * BOARD_W] = TILE_TUNNEL;
					}
					int my = (ay + by) / 2;
					for (int y = my; y < by; y += 1) {
						tiles[x + y * BOARD_W] = TILE_TUNNEL;
					}
					// Connect to center
					int mx = (ax + bx) / 2;
					for (int ex = min(x, mx); ex <= max(x, mx); ex += 1) {
						tiles[ex + my * BOARD_W] = TILE_TUNNEL;
					}
					rooms[j].connected = true;
				}
			}
		}
	}
	// Mobs
	mobs[0].x = rooms[start_room_i].ax + 1 + random_int(rooms[start_room_i].bx - rooms[start_room_i].ax - 2);
	mobs[0].y = rooms[start_room_i].ay + 1 + random_int(rooms[start_room_i].by - rooms[start_room_i].ay - 2);
	mobs[0].type = MOB_PLAYER;
	mobs[0].level = 1;
	roll_mob(&mobs[0]);
	int mobrooms[roomc];
	int mobroomc = 0;
	for (int i = 0; i < roomc; i += 1) {
		if (rooms[i].type == ROOM_FULL) {
			mobrooms[mobroomc++] = i;
		}
	}
	mobc = mobroomc + 1;
	for (int i = 1; i < mobc; i += 1) {
		struct room *room = &rooms[mobrooms[random_int(mobroomc)]];
		struct mob *mob = &mobs[i];
		mob->x = room->ax + 1 + random_int(room->bx - room->ax - 1);
		mob->y = room->ay + 1 + random_int(room->by - room->ay - 1);
		mob->type = MOB_ORK;
		mob->level = 1;
		roll_mob(mob);
	}
	int ex = rooms[finish_room_i].ax + 1 + random_int(rooms[finish_room_i].bx - rooms[finish_room_i].ax - 2);
	int ey = rooms[finish_room_i].ay + 1 + random_int(rooms[finish_room_i].by - rooms[finish_room_i].ay - 2);
	tiles[ex + ey * BOARD_W] = TILE_EXIT;
}

void roll_mob(struct mob *mob) {
	int level = mob->level;
	switch (mob->type) {
	case MOB_PLAYER:
		mob->speed = 50;
		mob->max_health = 100;
		break;
	default:
		mob->speed = 25;
		mob->max_health = 9 + random_int(22);
	}
	mob->health = mob->max_health;
	mob->strength = 6 + random_int(5);
	mob->defense = 1 + random_int(5);
	mob->accuracy = 51 + random_int(50);
	mob->evasion = 1 + random_int(50);
}

void split_room(int index) {
	if (roomc >= MAX_ROOMS - 1) return;
	int ax = rooms[index].ax;
	int ay = rooms[index].ay;
	int bx = rooms[index].bx;
	int by = rooms[index].by;
	int width = bx - ax + 1;
	int height = by - ay + 1;
	if (width <= MIN_ROOM_SIZE * 2 + 1 && height <= MIN_ROOM_SIZE * 2 + 1) {
		return;
	}
	int ori;
	if (width > height) {
		ori = 0;
	} else if (height > width) {
		ori = 1;
	} else {
		ori = random() % 2;
	}
	if (ori == 0) {
		int splitx = ax - 1 + MIN_ROOM_SIZE + (random_int(width - MIN_ROOM_SIZE * 2));
		int index2 = roomc;
		rooms[index].bx = splitx;
		rooms[index2].ax = splitx;
		rooms[index2].ay = ay;
		rooms[index2].bx = bx;
		rooms[index2].by = by;
		roomc += 1;
		split_room(index);
		split_room(index2);
	} else {
		int splity = ay - 1 + MIN_ROOM_SIZE + (random_int(height - MIN_ROOM_SIZE * 2));
		int index2 = roomc;
		rooms[index].by = splity;
		rooms[index2].ax = ax;
		rooms[index2].ay = splity;
		rooms[index2].bx = bx;
		rooms[index2].by = by;
		roomc += 1;
		split_room(index);
		split_room(index2);
	}
}

void update_visible_board() {
	for (int i = 0; i < BOARD_W * BOARD_H; i += 1) tiles[i] &= ~TILE_VISIBLE;
	int px = mobs[0].x;
	int py = mobs[0].y;
	for (int i = 0; i < roomc; i += 1) {
		if (rooms[i].type == ROOM_FULL
		    && rooms[i].ax <= px
		    && rooms[i].bx >= px
		    && rooms[i].ay <= py
		    && rooms[i].by >= py) {
			for (int y = rooms[i].ay; y <= rooms[i].by; y += 1) {
				for (int x = rooms[i].ax; x <= rooms[i].bx; x += 1) {
					tiles[x + y * BOARD_W] |= (TILE_VISIBLE | TILE_DISCOVERED);
				}
			}
		}
	}
	for (int y = py - 1; y <= py + 1; y += 1) {
		for (int x = px - 1; x <= px + 1; x += 1) {
			if (x >= 0 && x < BOARD_W && y >= 0 && y <= BOARD_H) {
				tiles[x + y * BOARD_W] |= (TILE_VISIBLE | TILE_DISCOVERED);
			}
		}
	}
}

bool tile_is_visible_wall(int tile) {
	if ((tile & TILE_DISCOVERED) == 0) return false;
	switch (tile & TILE_MASK) {
	case TILE_WALL:
	case TILE_HIDDEN_DOOR:
		return true;
	default:
		return false;
	}
}

void print_board() {
	clear();
	for (int y = 0; y < BOARD_H; y += 1) {
		for (int x = 0; x < BOARD_W; x += 1) {
			int tile = tiles[x + y * BOARD_W];
			if (!(tile & TILE_DISCOVERED)) continue;
			if (tile == TILE_EMPTY) continue;
			if (!(tile & TILE_VISIBLE)) {
				switch (tile & TILE_MASK) {
				case TILE_FLOOR:
					attron(COLOR_PAIR(TILE_COLOR_INVISIBLE_FLOOR)); break;
				default:
					attron(COLOR_PAIR(TILE_COLOR_INVISIBLE));
				}
			} else {
				switch (tile & TILE_MASK) {
				case TILE_FLOOR:
					attron(COLOR_PAIR(TILE_COLOR_FLOOR)); break;
				case TILE_WALL:
				case TILE_HIDDEN_DOOR:
					attron(COLOR_PAIR(TILE_COLOR_WALL)); break;
				case TILE_DOOR:
					attron(COLOR_PAIR(TILE_COLOR_DOOR)); break;
				default:
					attron(COLOR_PAIR(TILE_COLOR_VISIBLE));
				}
			}
			move(y + 1, x);
			bool left, right, up, down;
			switch (tile & TILE_MASK) {
			case TILE_EMPTY: break;
			case TILE_FLOOR: addch('.'); break;
			case TILE_DOOR: addch('+'); break;
			case TILE_TUNNEL: addch('#'); break;
			case TILE_EXIT: addch('%'); break;
			case TILE_WALL:
			case TILE_HIDDEN_DOOR:
				left = x > 0 && tile_is_visible_wall(tiles[x - 1 + y * BOARD_W]);
				right = x < BOARD_W - 1 && tile_is_visible_wall(tiles[x + 1 + y * BOARD_W]);
				up = y > 0 && tile_is_visible_wall(tiles[x + (y - 1) * BOARD_W]);
				down = y < BOARD_H - 1 && tile_is_visible_wall(tiles[x + (y + 1) * BOARD_W]);
				if (left && right && up && down) {
					addch(ACS_PLUS);
				} else if (left && right && up) {
					addch(ACS_BTEE);
				} else if (left && right && down) {
					addch(ACS_TTEE);
				} else if (left && up && down) {
					addch(ACS_RTEE);
				} else if (right && up && down) {
					addch(ACS_LTEE);
				} else if (left && up) {
					addch(ACS_LRCORNER);
				} else if (right && up) {
					addch(ACS_LLCORNER);
				} else if (right && down) {
					addch(ACS_ULCORNER);
				} else if (left && down) {
					addch(ACS_URCORNER);
				} else if (left && right) {
					addch(ACS_HLINE);
				} else if (up && down) {
					addch(ACS_VLINE);
				} else if (up) {
					addch(ACS_BTEE);
				} else if (down) {
					addch(ACS_TTEE);
				} else if (left) {
					addch(ACS_RTEE);
				} else if (right) {
					addch(ACS_LTEE);
				}
				break;
			}
		}
	}
	attron(COLOR_PAIR(TILE_COLOR_AVATAR));
	mvprintw(mobs[0].y + 1, mobs[0].x, "@");
	attron(COLOR_PAIR(TILE_COLOR_ENEMY));
	for (int i = 1; i < mobc; i += 1) {
		if (mobs[i].type == MOB_DEAD) continue;
		int x = mobs[i].x;
		int y = mobs[i].y;
		if (tiles[x + y * BOARD_W] & TILE_VISIBLE) {
			mvprintw(y + 1, x, "K");
		}
	}
	attron(COLOR_PAIR(COLOR_DEFAULT));
	mvprintw(BOARD_H + 1, 0, "Turn:%d Health:%d/%d Str:%d Def:%d Acc:%d Eva:%d", mobs[0].turn, mobs[0].health, mobs[0].max_health, mobs[0].strength, mobs[0].defense, mobs[0].accuracy, mobs[0].evasion);
	if (log_message != 0) {
		printw(" ");
		mvprintw(0, 0, log_message);
		log_message = 0;
	}
	move(mobs[0].y + 1, mobs[0].x);
}

void mob_ai(struct mob *mob) {
	int x = mob->x;
	int y = mob->y;
	if (mobs[0].x == x) {
		if (mobs[0].y == y - 1) {
			mob->dy = -1;
			return;
		} else if (mobs[0].y == y + 1) {
			mob->dy = 1;
			return;
		}
	} else if (mobs[0].y == y) {
		if (mobs[0].x == x - 1) {
			mob->dx = -1;
			return;
		} else if (mobs[0].x == x + 1) {
			mob->dx = 1;
			return;
		}
	}
	switch (mob->turn % 4) {
	case 0:
		mob->dy = -1;
		break;
	case 1:
		mob->dx = 1;
		break;
	case 2:
		mob->dy = 1;
		break;
	case 3: default:
		mob->dx = -1;
	}
}

void mob_interact(struct mob *mob, int dx, int dy) {
	int x = mob->x + dx;
	int y = mob->y + dy;
	for (int i = 0; i < mobc; i += 1) {
		if (&mobs[i] == mob) continue;
		if (mobs[i].type == MOB_DEAD) continue;
		if (mobs[i].x == x && mobs[i].y == y) {
			mob_fight(mob, &mobs[i]);
			return;
		}
	}
	int tile = tiles[x + y * BOARD_W];
	switch (tile & TILE_MASK) {
	case TILE_FLOOR:
	case TILE_DOOR:
	case TILE_TUNNEL:
		mob->x = x;
		mob->y = y;
		break;
	case TILE_HIDDEN_DOOR:
		if (mob->type == MOB_PLAYER) {
			tiles[x + y * BOARD_W] = TILE_DOOR;
			log_message = "You discover a hidden door.";
			combat_paused = true;
		}
		break;
	default: break;
	}
}

void mob_fight(struct mob* attacker, struct mob* defender) {
	int hit_chance = attacker->accuracy - defender->evasion;
	int hit_roll = random_int(100);
	bool hit = hit_roll < hit_chance;
	if (!hit) {
		if (attacker->type == MOB_PLAYER) {
			sprintf(log_buffer, "You miss the %s.", mob_name(defender->type));
			log_message = log_buffer;
			combat_paused = true;
		} else if (defender->type == MOB_PLAYER) {
			sprintf(log_buffer, "The %s misses you.", mob_name(attacker->type));
			log_message = log_buffer;
			combat_paused = true;
		}
		return;
	}
	int damage = attacker->strength - defender->defense;
	if (damage < 1) damage = 1;
	if (attacker->type == MOB_PLAYER) {
		sprintf(log_buffer, "You hit the %s for %d damage.", mob_name(defender->type), damage);
		log_message = log_buffer;
		combat_paused = true;
	} else if (defender->type == MOB_PLAYER) {
		sprintf(log_buffer, "The %s hits you for %d damage.", mob_name(attacker->type), damage);
		log_message = log_buffer;
		combat_paused = true;
	}
	defender->health -= damage;
	if (defender->health <= 0) {
		defender->health = 0;
		defender->type = MOB_DEAD;
	}
}

int random_int(int i) {
	if (i <= 0) return 0;
	return random() % i;
}

int max(int a, int b) {
	return a > b ? a : b;
}

int min(int a, int b) {
	return a > b ? b : a;
}
