/**
 * ============================================================================
 * 游戏名称: Boss Chase (追逐大作战)
 * 平台: AM (Abstract Machine) 
 * 描述: 玩家控制绿色小方块躲避红色Boss的追击，生存时间越长分数越高
 * ============================================================================
 */

#include <am.h>
#include <klib.h>
#include <klib-macros.h>

/* ============================================================================
 * 游戏常量配置
 * ============================================================================ */

#define TILE_SIZE 16          // 玩家方块大小（像素）
#define PLAYER_SPEED 3        // 玩家移动速度（像素/帧）
#define BOSS_SPEED_RATIO 2    // Boss速度为玩家的 1/2
#define FPS 30                // 游戏帧率
#define MAX_PARTICLES 50      // 最大粒子数量
#define MAX_OBSTACLES 8       // 最大障碍物数量

/* ============================================================================
 * 颜色定义 (RGB888格式)
 * ============================================================================ */

#define COLOR_BG      0x0a0a1a  // 深蓝色背景
#define COLOR_PLAYER  0x00ff88  // 亮绿色玩家
#define COLOR_BOSS    0xff0044  // 红色Boss
#define COLOR_BORDER  0x4444aa  // 边框颜色
#define COLOR_PARTICLE 0xffaa00 // 橙色粒子
#define COLOR_OBSTACLE 0x666688 // 灰色障碍物

/* ============================================================================
 * 数据结构定义
 * ============================================================================ */

/**
 * 矩形结构体 - 用于碰撞检测和位置表示
 */
typedef struct {
    int x, y;      // 左上角坐标
    int w, h;      // 宽度和高度
} rect_t;

/**
 * 玩家结构体
 */
typedef struct {
    rect_t rect;       // 位置和大小
    int speed;         // 移动速度
    int invincible;    // 无敌帧计数器 (0表示不无敌)
} player_t;

/**
 * Boss结构体
 */
typedef struct {
    rect_t rect;       // 位置和大小
    int speed;         // 移动速度
    int phase;         // AI阶段: 0=追踪, 1=埋伏, 2=冲刺
    int timer;         // 阶段计时器 (用于切换AI)
    int flash;         // 闪烁计数器 (视觉效果)
    int move_pattern;  // 移动模式: 0=水平优先, 1=垂直优先
    int move_counter;  // 徘徊模式计数器
} boss_t;

/**
 * 粒子结构体 - 用于特效
 */
typedef struct {
    int x, y;          // 位置
    int vx, vy;        // 速度
    int life;          // 生命周期 (递减到0消失)
    uint32_t color;    // 颜色
} particle_t;

/**
 * 障碍物结构体
 */
typedef struct {
    rect_t rect;       // 位置和大小
    int active;        // 是否激活 (1=存在, 0=已移除)
} obstacle_t;

/* ============================================================================
 * 全局变量
 * ============================================================================ */

static int screen_w, screen_h;          // 屏幕尺寸
static player_t player;                 // 玩家对象
static boss_t boss;                     // Boss对象
static particle_t particles[MAX_PARTICLES]; // 粒子数组
static obstacle_t obstacles[MAX_OBSTACLES]; // 障碍物数组
static int game_over = 0;               // 游戏结束标志
static int score = 0;                   // 当前分数
static int high_score = 0;              // 最高分
static int combo = 0;                  // 连击数
static int game_time = 0;              // 游戏运行时间 (帧数)
static int difficulty = 1;             // 难度等级

/* ============================================================================
 * 工具函数
 * ============================================================================ */

/**
 * 将值限制在 [min, max] 范围内
 */
static int clamp(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/**
 * 生成 [min, max] 范围内的随机整数
 */
static int randint(int min, int max) {
    return min + rand() % (max - min + 1);
}

/**
 * 计算曼哈顿距离 (纯整数运算，无需开方)
 * 曼哈顿距离 = |x1-x2| + |y1-y2|
 * 优点: 计算速度快，适合网格移动
 */
static int manhattan_distance(int x1, int y1, int x2, int y2) {
    int dx = x1 - x2;
    int dy = y1 - y2;
    if (dx < 0) dx = -dx;  // 绝对值
    if (dy < 0) dy = -dy;
    return dx + dy;
}

/**
 * 矩形碰撞检测
 * 返回 1 表示两个矩形重叠，0 表示不重叠
 */
static int rect_collide(rect_t *a, rect_t *b) {
    // 如果任一矩形完全在另一个的左边/右边/上边/下边，则不碰撞
    return !(a->x + a->w <= b->x ||  // a 完全在 b 左边
             b->x + b->w <= a->x ||  // a 完全在 b 右边
             a->y + a->h <= b->y ||  // a 完全在 b 上边
             b->y + b->h <= a->y);   // a 完全在 b 下边
}

/* ============================================================================
 * 粒子系统
 * ============================================================================ */

/**
 * 生成粒子特效
 * @param x, y   粒子产生位置
 * @param count  粒子数量
 * @param color  粒子颜色
 */
void spawn_particles(int x, int y, int count, uint32_t color) {
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        // 找到空闲的粒子槽位
        if (!particles[i].life) {
            particles[i].x = x + randint(-8, 8);   // 随机偏移
            particles[i].y = y + randint(-8, 8);
            particles[i].vx = randint(-3, 3);      // 随机速度
            particles[i].vy = randint(-3, 3);
            particles[i].life = randint(15, 30);   // 随机生命周期
            particles[i].color = color;
            count--;
        }
    }
}

/**
 * 更新所有粒子 (物理模拟)
 */
void update_particles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life) {
            // 移动
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            
            // 空气阻力 (整数版本)
            if (particles[i].vx > 0) particles[i].vx--;
            else if (particles[i].vx < 0) particles[i].vx++;
            
            if (particles[i].vy > 0) particles[i].vy--;
            else if (particles[i].vy < 0) particles[i].vy++;
            
            // 重力 (整数)
            particles[i].vy += 1;
            
            // 生命周期递减
            particles[i].life--;
        }
    }
}

/* ============================================================================
 * 障碍物系统
 * ============================================================================ */

/**
 * 在随机位置生成一个障碍物
 * 确保不会覆盖玩家或Boss
 */
void spawn_obstacle() {
    int margin = 40;
    int game_w = screen_w - 2 * margin;
    int game_h = screen_h - 2 * margin;
    
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (!obstacles[i].active) {
            // 随机大小 (20-40像素)
            obstacles[i].rect.w = randint(20, 40);
            obstacles[i].rect.h = randint(20, 40);
            
            // 随机位置
            obstacles[i].rect.x = margin + randint(0, game_w - obstacles[i].rect.w);
            obstacles[i].rect.y = margin + randint(0, game_h - obstacles[i].rect.h);
            
            // 检查是否与玩家或Boss重叠
            if (rect_collide(&obstacles[i].rect, &player.rect) ||
                rect_collide(&obstacles[i].rect, &boss.rect)) {
                continue;  // 如果重叠，重新生成
            }
            
            obstacles[i].active = 1;
            break;
        }
    }
}

/* ============================================================================
 * 游戏初始化
 * ============================================================================ */

/**
 * 初始化游戏状态
 * 在游戏开始或重新开始时调用
 */
void game_init() {
    // 获取屏幕尺寸
    screen_w = io_read(AM_GPU_CONFIG).width;
    screen_h = io_read(AM_GPU_CONFIG).height;
    
    int margin = 30;
    int game_w = screen_w - 2 * margin;
    int game_h = screen_h - 2 * margin;
    
    /* ----- 初始化玩家 ----- */
    player.rect.w = TILE_SIZE;
    player.rect.h = TILE_SIZE;
    player.rect.x = margin + game_w / 4;      // 左上区域
    player.rect.y = margin + game_h / 2;
    player.speed = PLAYER_SPEED;
    player.invincible = 0;                     // 初始不无敌
    
    /* ----- 初始化Boss ----- */
    boss.rect.w = TILE_SIZE * 2;               // Boss是玩家的2倍大
    boss.rect.h = TILE_SIZE * 2;
    boss.rect.x = margin + game_w * 3 / 4 - boss.rect.w;  // 右上区域
    boss.rect.y = margin + game_h / 4;
    boss.speed = PLAYER_SPEED / BOSS_SPEED_RATIO;
    if (boss.speed < 1) boss.speed = 1;        // 速度至少为1
    boss.phase = 0;                            // 初始为追踪模式
    boss.timer = 0;
    boss.flash = 0;
    boss.move_pattern = 0;
    boss.move_counter = 0;
    
    /* ----- 清空粒子与障碍物 ----- */
    memset(particles, 0, sizeof(particles));
    memset(obstacles, 0, sizeof(obstacles));
    
    /* ----- 生成初始障碍物 (3个) ----- */
    for (int i = 0; i < 3; i++) spawn_obstacle();
    
    /* ----- 重置游戏状态 ----- */
    game_over = 0;
    score = 0;
    combo = 0;
    game_time = 0;
    difficulty = 1;
}

/* ============================================================================
 * 游戏逻辑更新
 * ============================================================================ */

/**
 * 根据键盘输入更新玩家位置
 */
void update_player(int keycode) {
    if (game_over || player.invincible) return;
    
    // 计算移动方向
    int dx = 0, dy = 0;
    switch (keycode) {
        case AM_KEY_LEFT:  dx = -player.speed; break;
        case AM_KEY_RIGHT: dx = player.speed; break;
        case AM_KEY_UP:    dy = -player.speed; break;
        case AM_KEY_DOWN:  dy = player.speed; break;
        default: return;
    }
    
    // 边界限制
    int margin = 30;
    int game_w = screen_w - 2 * margin;
    int game_h = screen_h - 2 * margin;
    
    int new_x = clamp(player.rect.x + dx, margin, margin + game_w - player.rect.w);
    int new_y = clamp(player.rect.y + dy, margin, margin + game_h - player.rect.h);
    
    player.rect.x = new_x;
    player.rect.y = new_y;
    
    // 移动时产生轨迹粒子
    if (dx || dy) {
        spawn_particles(player.rect.x + player.rect.w/2, 
                       player.rect.y + player.rect.h/2, 1, 0x00ff8844);
    }
}

/**
 * 更新Boss AI (使用曼哈顿距离，纯整数运算)
 * Boss有三种AI阶段，每3秒切换一次
 */
void update_boss() {
    if (game_over) return;
    
    // 闪烁计数器 (用于视觉效果)
    boss.flash = (boss.flash + 1) % 10;
    
    /* ----- 计算玩家和Boss的中心点 ----- */
    int cx = player.rect.x + player.rect.w/2;
    int cy = player.rect.y + player.rect.h/2;
    int bx = boss.rect.x + boss.rect.w/2;
    int by = boss.rect.y + boss.rect.h/2;
    
    // 计算方向和距离 (曼哈顿距离)
    int dx = cx - bx;
    int dy = cy - by;
    int abs_dx = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    int dist = abs_dx + abs_dy;
    
    /* ----- AI阶段切换 (每3秒) ----- */
    boss.timer++;
    if (boss.timer > FPS * 3) {
        boss.phase = (boss.phase + 1) % 3;  // 0->1->2->0 循环
        boss.timer = 0;
        boss.move_pattern = randint(0, 1);   // 随机切换移动模式
        boss.move_counter = 0;
        // 阶段切换特效
        spawn_particles(bx, by, 10, 0xff004488);
    }
    
    /* ----- Boss移动决策 ----- */
    int move_x = 0, move_y = 0;
    int margin = 30;
    int game_w = screen_w - 2 * margin;
    int game_h = screen_h - 2 * margin;
    
    switch (boss.phase) {
        /* ----- 阶段0: 追踪模式 ----- */
        case 0:
            if (dist > 0) {
                if (boss.move_pattern == 0) {
                    // 水平优先: 先对齐x坐标
                    if (abs_dx >= abs_dy || (abs_dx == abs_dy && randint(0, 1))) {
                        move_x = (dx > 0) ? boss.speed : -boss.speed;
                        if (abs_dx < boss.speed) move_x = dx;
                    } else {
                        move_y = (dy > 0) ? boss.speed : -boss.speed;
                        if (abs_dy < boss.speed) move_y = dy;
                    }
                } else {
                    // 垂直优先: 先对齐y坐标
                    if (abs_dy >= abs_dx || (abs_dy == abs_dx && randint(0, 1))) {
                        move_y = (dy > 0) ? boss.speed : -boss.speed;
                        if (abs_dy < boss.speed) move_y = dy;
                    } else {
                        move_x = (dx > 0) ? boss.speed : -boss.speed;
                        if (abs_dx < boss.speed) move_x = dx;
                    }
                }
            }
            break;
            
        /* ----- 阶段1: 埋伏模式 ----- */
        case 1:
            boss.move_counter++;
            if (dist < 100) {
                // 太靠近玩家时后退
                move_x = (dx > 0) ? -boss.speed : boss.speed;
                move_y = (dy > 0) ? -boss.speed : boss.speed;
            } else {
                // 在玩家周围画方形路径徘徊 (替代sin/cos)
                int cycle = boss.move_counter % 20;
                if (cycle < 5) {
                    move_x = (boss.move_pattern == 0) ? boss.speed : 0;
                    move_y = (boss.move_pattern == 1) ? boss.speed : 0;
                } else if (cycle < 10) {
                    move_x = (boss.move_pattern == 0) ? -boss.speed : 0;
                    move_y = (boss.move_pattern == 1) ? -boss.speed : 0;
                } else if (cycle < 15) {
                    move_x = (boss.move_pattern == 0) ? -boss.speed : 0;
                    move_y = (boss.move_pattern == 1) ? -boss.speed : 0;
                } else {
                    move_x = (boss.move_pattern == 0) ? boss.speed : 0;
                    move_y = (boss.move_pattern == 1) ? boss.speed : 0;
                }
            }
            break;
            
        /* ----- 阶段2: 冲刺模式 ----- */
        case 2:
            if (dist > 0) {
                int speed = boss.speed * 2;  // 速度翻倍
                // 选择曼哈顿距离中较大的方向冲刺
                if (abs_dx >= abs_dy) {
                    move_x = (dx > 0) ? speed : -speed;
                    // 如果x方向快到了，补一点y方向
                    if (abs_dx < speed && abs_dy > 0) {
                        move_y = (dy > 0) ? boss.speed : -boss.speed;
                    }
                } else {
                    move_y = (dy > 0) ? speed : -speed;
                    if (abs_dy < speed && abs_dx > 0) {
                        move_x = (dx > 0) ? boss.speed : -boss.speed;
                    }
                }
                // 冲刺粒子特效
                spawn_particles(bx, by, 2, 0xff004466);
            }
            break;
    }
    
    /* ----- 应用移动并限制边界 ----- */
    int new_x = clamp(boss.rect.x + move_x, margin, margin + game_w - boss.rect.w);
    int new_y = clamp(boss.rect.y + move_y, margin, margin + game_h - boss.rect.h);
    boss.rect.x = new_x;
    boss.rect.y = new_y;
    
    // Boss移动时产生粒子
    if (move_x || move_y) {
        spawn_particles(bx, by, 1, 0xff004466);
    }
}

/**
 * 碰撞检测处理
 * 检测玩家与Boss、玩家与障碍物的碰撞
 */
void check_collisions() {
    if (game_over) return;
    
    /* ----- 玩家 vs Boss ----- */
    if (rect_collide(&player.rect, &boss.rect)) {
        if (player.invincible <= 0) {
            // 死亡特效
            spawn_particles(player.rect.x + player.rect.w/2, 
                          player.rect.y + player.rect.h/2, 30, 0xff0044);
            game_over = 1;
            if (score > high_score) high_score = score;
            return;
        }
    }
    
    /* ----- 玩家 vs 障碍物 ----- */
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active && rect_collide(&player.rect, &obstacles[i].rect)) {
            // 碰撞特效
            spawn_particles(player.rect.x + player.rect.w/2, 
                          player.rect.y + player.rect.h/2, 15, 0x00ff8844);
            player.invincible = FPS;  // 1秒无敌
            combo = 0;               // 连击中断
            
            /* 
             * 将玩家传送到最近的角落 (使用曼哈顿距离)
             * 四个可选位置: 左上、右上、左下、右下
             */
            int margin = 30;
            int game_w = screen_w - 2 * margin;
            int game_h = screen_h - 2 * margin;
            
            int positions[4][2] = {
                {margin + game_w / 4, margin + game_h / 2},
                {margin + game_w * 3 / 4, margin + game_h / 2},
                {margin + game_w / 2, margin + game_h / 4},
                {margin + game_w / 2, margin + game_h * 3 / 4}
            };
            
            int best_idx = 0;
            int best_dist = 999999;
            for (int j = 0; j < 4; j++) {
                int dist = manhattan_distance(player.rect.x, player.rect.y, 
                                            positions[j][0], positions[j][1]);
                if (dist < best_dist) {
                    best_dist = dist;
                    best_idx = j;
                }
            }
            
            player.rect.x = positions[best_idx][0];
            player.rect.y = positions[best_idx][1];
            break;
        }
    }
}

/* ============================================================================
 * 渲染函数
 * ============================================================================ */

/**
 * 绘制填充矩形
 */
void draw_rect(int x, int y, int w, int h, uint32_t color) {
    uint32_t *line = (uint32_t *)malloc(w * sizeof(uint32_t));
    if (!line) return;
    
    // 填充一行像素
    for (int i = 0; i < w; i++) line[i] = color;
    
    // 逐行绘制
    for (int row = 0; row < h; row++) {
        io_write(AM_GPU_FBDRAW, x, y + row, line, w, 1, false);
    }
    
    free(line);
}

/**
 * 绘制矩形边框
 */
void draw_rect_border(int x, int y, int w, int h, uint32_t color, int border) {
    draw_rect(x, y, w, border, color);                    // 上边
    draw_rect(x, y + h - border, w, border, color);       // 下边
    draw_rect(x, y, border, h, color);                    // 左边
    draw_rect(x + w - border, y, border, h, color);       // 右边
}

/**
 * 绘制所有粒子
 */
void draw_particles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life) {
            // 粒子大小随生命周期变化
            int size = particles[i].life / 5 + 1;
            draw_rect(particles[i].x, particles[i].y, size, size, particles[i].color);
        }
    }
}

/**
 * 绘制所有障碍物
 */
void draw_obstacles() {
    for (int i = 0; i < MAX_OBSTACLES; i++) {
        if (obstacles[i].active) {
            draw_rect(obstacles[i].rect.x, obstacles[i].rect.y, 
                     obstacles[i].rect.w, obstacles[i].rect.h, COLOR_OBSTACLE);
            // 边框高亮
            draw_rect_border(obstacles[i].rect.x, obstacles[i].rect.y,
                           obstacles[i].rect.w, obstacles[i].rect.h, 0x8888aa44, 1);
        }
    }
}

/**
 * 主渲染函数 - 绘制所有游戏元素
 */
void render() {
    /* ----- 清屏 (绘制背景) ----- */
    uint32_t *bg = (uint32_t *)malloc(screen_w * sizeof(uint32_t));
    if (!bg) return;
    for (int i = 0; i < screen_w; i++) bg[i] = COLOR_BG;
    for (int row = 0; row < screen_h; row++) {
        io_write(AM_GPU_FBDRAW, 0, row, bg, screen_w, 1, false);
    }
    free(bg);
    
    /* ----- 绘制游戏区域边框 ----- */
    int margin = 30;
    int game_w = screen_w - 2 * margin;
    int game_h = screen_h - 2 * margin;
    draw_rect_border(margin, margin, game_w, game_h, COLOR_BORDER, 2);
    
    /* ----- 绘制游戏元素 ----- */
    draw_obstacles();
    draw_particles();
    
    // 绘制Boss (闪烁效果)
    if (boss.flash < 7 || game_over) {
        draw_rect(boss.rect.x, boss.rect.y, boss.rect.w, boss.rect.h, COLOR_BOSS);
        draw_rect_border(boss.rect.x, boss.rect.y, boss.rect.w, boss.rect.h, 0xff448844, 2);
    }
    
    // 绘制玩家 (无敌时闪烁)
    if (player.invincible <= 0 || (player.invincible / 5) % 2 == 0) {
        draw_rect(player.rect.x, player.rect.y, player.rect.w, player.rect.h, COLOR_PLAYER);
        // 发光边框
        draw_rect_border(player.rect.x - 2, player.rect.y - 2, 
                        player.rect.w + 4, player.rect.h + 4, 0x00ff8844, 1);
    }
    
    /* ----- 刷新屏幕 ----- */
    io_write(AM_GPU_FBDRAW, 0, 0, NULL, 0, 0, true);
    
    /* ----- 显示HUD信息 ----- */
    printf("\033[0;0H");  // 光标回到左上角
    printf("\033[36m===== BOSS CHASE =====\033[0m\n");
    printf("Score: \033[32m%d\033[0m  High: \033[33m%d\033[0m  ", score, high_score);
    printf("Combo: \033[35m%d\033[0m  ", combo);
    printf("Time: \033[33m%ds\033[0m\n", game_time / FPS);
    printf("Boss AI: ");
    switch (boss.phase) {
        case 0: printf("\033[31mChase\033[0m"); break;
        case 1: printf("\033[33mAmbush\033[0m"); break;
        case 2: printf("\033[35mDash\033[0m"); break;
    }
    printf("  Diff: \033[31m%d\033[0m\n", difficulty);
    
    if (game_over) {
        printf("\n\033[31mGAME OVER!\033[0m Press 'R' to restart, 'Q' to quit\n");
    } else {
        printf("\n\033[37mArrow Keys: Move\033[0m | \033[33mAvoid obstacles!\033[0m\n");
    }
}

/* ============================================================================
 * 主函数和游戏循环
 * ============================================================================ */

/**
 * 主函数 - 游戏入口点
 */
int main() {

    // 调试：打印 AM_HOME 环境变量
    char* am_home = getenv("AM_HOME");
    printf("AM_HOME = %s\n", am_home ? am_home : "NOT SET!");

    /* ----- 初始化AM I/O系统 ----- */
    ioe_init();
    
    /* ----- 检查必要设备 ----- */
    panic_on(!io_read(AM_TIMER_CONFIG).present, "requires timer");
    panic_on(!io_read(AM_INPUT_CONFIG).present, "requires keyboard");
    panic_on(!io_read(AM_GPU_CONFIG).present, "requires GPU");
    
    /* ----- 初始化随机数种子 ----- */
    srand(io_read(AM_TIMER_UPTIME).us);
    
    /* ----- 初始化游戏 ----- */
    game_init();
    render();
    
    printf("=== BOSS CHASE GAME ===\n");
    printf("Press 'R' to restart, 'Q' or ESC to quit\n\n");
    
    /* ----- 游戏主循环变量 ----- */
    int current = 0, rendered = 0;
    uint64_t t0 = io_read(AM_TIMER_UPTIME).us;
    int frame_count = 0;
    
    /* ----- 游戏主循环 ----- */
    while (1) {
        /*
         * 帧率控制: 计算应该更新的帧数
         * 采用"帧追赶"机制，防止游戏卡顿
         */
        int frames = (io_read(AM_TIMER_UPTIME).us - t0) / (1000000 / FPS);
        
        /* ----- 处理键盘输入 (非阻塞) ----- */
        while (1) {
            AM_INPUT_KEYBRD_T ev = io_read(AM_INPUT_KEYBRD);
            if (ev.keycode == AM_KEY_NONE) break;  // 无按键
            
            if (ev.keydown) {
                // 退出游戏
                if (ev.keycode == AM_KEY_ESCAPE || ev.keycode == AM_KEY_Q) {
                    halt(0);
                }
                
                // 重新开始 (仅在游戏结束时)
                if (ev.keycode == AM_KEY_R && game_over) {
                    game_init();
                    render();
                    frame_count = 0;
                    current = 0;
                    rendered = 0;
                    t0 = io_read(AM_TIMER_UPTIME).us;
                    break;
                }
                
                // 玩家控制
                if (!game_over) {
                    update_player(ev.keycode);
                }
            }
        }
        
        /* ----- 更新游戏逻辑 ----- */
        if (!game_over && current < frames) {
            for (; current < frames; current++) {
                update_boss();          // 更新Boss AI
                update_particles();     // 更新粒子
                check_collisions();     // 碰撞检测
                
                frame_count++;
                game_time++;
                
                /* ----- 动态难度系统 ----- */
                // 每5秒生成一个新障碍物
                if (frame_count % (FPS * 5) == 0 && !game_over) {
                    spawn_obstacle();
                    difficulty = game_time / (FPS * 10) + 1;
                    boss.speed = (PLAYER_SPEED / BOSS_SPEED_RATIO) + difficulty / 2;
                    if (boss.speed > PLAYER_SPEED) boss.speed = PLAYER_SPEED;
                }
                
                /* ----- 计分系统 ----- */
                if (!game_over) {
                    score += difficulty;  // 难度越高得分越快
                    combo++;
                }
            }
        } else if (current < frames) {
            // 游戏结束后保持时间同步
            current = frames;
        }
        
        /* ----- 渲染 (有新帧才渲染) ----- */
        if (current > rendered) {
            render();
            rendered = current;
        }
    }
    
    return 0;
}